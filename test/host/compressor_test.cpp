#include "DynamicRangeCompressor.hpp"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

using Espressif::Wrappers::Audio::BasicDynamicRangeCompressor;
using Espressif::Wrappers::Audio::DynamicRangeCompressor;
using Espressif::Wrappers::Audio::EnvelopeRoot;
using Espressif::Wrappers::Audio::kNativeEnvelopeRoot;
using Espressif::Wrappers::Audio::squareRootFloor;

namespace {

constexpr int kSamplesPerRun = 44100;
constexpr int kSinePeriod = 44;
constexpr int kSquareHalfPeriod = 300;
constexpr int kBurstLength = 2500;
constexpr int kVolumeChangePeriod = 1000;
constexpr int kVolumeChangeSamples = 200000;
constexpr int kBoundaryNoiseAmplitude = 3000;
constexpr uint32_t kExhaustiveRootLimit = 1U << 22;
constexpr uint32_t kRootEqualityLimit = 1U << 20;
constexpr int kRandomRootSamples = 10000000;
constexpr int kMaxIntegerError = 16;
constexpr double kMaxIntegerRmsError = 0.5;

using FloatCompressor = BasicDynamicRangeCompressor<EnvelopeRoot::Float>;
using IntegerCompressor = BasicDynamicRangeCompressor<EnvelopeRoot::Integer>;

static_assert(std::is_same_v<DynamicRangeCompressor, BasicDynamicRangeCompressor<kNativeEnvelopeRoot>>);
static_assert(squareRootFloor(0) == 0 && squareRootFloor(3) == 1 && squareRootFloor(4) == 2);
static_assert(squareRootFloor(UINT32_MAX) == 65535);

constexpr std::array<int32_t, 9> kVolumes = {0, 1, 100, 600, 800, 1000, 2000, 13107, 65535};
constexpr std::array<int32_t, 6> kAmplitudes = {100, 3000, 32767, 300000, 1 << 20, (1 << 22) - 1};

enum class Signal { Sine, Noise, Square, Bursts };
constexpr std::array<Signal, 4> kSignals = {Signal::Sine, Signal::Noise, Signal::Square, Signal::Bursts};

const char* signalName(Signal signal) {
    switch (signal) {
        case Signal::Sine: return "sine";
        case Signal::Noise: return "noise";
        case Signal::Square: return "square";
        case Signal::Bursts: return "bursts";
    }
    return "?";
}

// Verbatim copy of DynamicRangeCompressor::process() before the 32-bit fast path (64-bit division).
class ReferenceCompressor {
public:
    explicit ReferenceCompressor(int32_t volume) : _volume(volume) {}

    int16_t process(int32_t v) {
        _vol_avg += static_cast<uint32_t>(v < 0 ? -v : v);
        _vol_avg -= (_vol_avg + 255) >> 8;

        int32_t divisor = static_cast<int32_t>(std::sqrt(static_cast<float>(_vol_avg))) + 100;
        if (divisor < _volume) divisor = _volume;

        int32_t out = static_cast<int32_t>((static_cast<int64_t>(v) * _volume) / divisor);
        return clampToInt16(out);
    }

    void setVolume(int32_t volume) { _volume = volume; }

private:
    static int16_t clampToInt16(int32_t x) {
        if (x > 32767) return 32767;
        if (x < -32768) return -32768;
        return static_cast<int16_t>(x);
    }

    int32_t _volume;
    uint32_t _vol_avg = 0;
};

class Xorshift {
public:
    explicit Xorshift(uint32_t seed) : _state(seed) {}

    uint32_t next() {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        return _state;
    }

    int32_t nextIn(int32_t amplitude) {
        const uint64_t span = 2 * static_cast<uint64_t>(amplitude) + 1;
        return static_cast<int32_t>(static_cast<int64_t>(next() % span) - amplitude);
    }

private:
    uint32_t _state;
};

class SignalSource {
public:
    SignalSource(Signal signal, int32_t amplitude) : _signal(signal), _amplitude(amplitude), _noise(0x2545F491U) {
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < kSinePeriod; ++i) {
            _sine[i] = static_cast<int32_t>(std::lround(amplitude * std::sin(2.0 * kPi * i / kSinePeriod)));
        }
    }

    int32_t sample(int index) {
        switch (_signal) {
            case Signal::Sine: return _sine[index % kSinePeriod];
            case Signal::Noise: return _noise.nextIn(_amplitude);
            case Signal::Square: return ((index / kSquareHalfPeriod) % 2 == 0) ? _amplitude : -_amplitude;
            case Signal::Bursts: return ((index / kBurstLength) % 2 == 0) ? 0 : _sine[index % kSinePeriod];
        }
        return 0;
    }

private:
    Signal _signal;
    int32_t _amplitude;
    Xorshift _noise;
    std::array<int32_t, kSinePeriod> _sine{};
};

class Tally {
public:
    virtual ~Tally() = default;

    void beginRun(const char* context) {
        std::snprintf(_context, sizeof(_context), "%s", context);
        _run_max = 0;
        _run_squares = 0;
        _run_samples = 0;
    }

    void record(int16_t actual, int16_t expected, int32_t x, int32_t volume, int index) {
        ++_samples;
        ++_run_samples;
        if (static_cast<int64_t>(x) * volume != static_cast<int32_t>(static_cast<int64_t>(x) * volume)) {
            ++_wide_products;
        }
        const int error = std::abs(static_cast<int>(actual) - static_cast<int>(expected));
        if (error == 0) return;
        ++_mismatches;
        _run_squares += static_cast<double>(error) * error;
        if (error > _run_max) _run_max = error;
        onMismatch(actual, expected, index);
    }

    virtual void endRun() {}

    uint64_t samples() const { return _samples; }
    uint64_t wideProducts() const { return _wide_products; }
    uint64_t mismatches() const { return _mismatches; }

protected:
    static constexpr int kMaxReports = 20;

    virtual void onMismatch(int16_t, int16_t, int) {}

    char _context[96] = {};
    int _run_max = 0;
    double _run_squares = 0;
    uint64_t _run_samples = 0;
    uint64_t _samples = 0;
    uint64_t _wide_products = 0;
    uint64_t _mismatches = 0;
};

class ExactTally : public Tally {
public:
    bool passed() const { return _mismatches == 0; }

private:
    void onMismatch(int16_t actual, int16_t expected, int index) override {
        if (_mismatches <= kMaxReports) {
            std::printf("FAIL %s index %d: got %d, reference %d\n", _context, index, actual, expected);
        }
    }
};

class ToleranceTally : public Tally {
public:
    void endRun() override {
        const double rms = (_run_samples == 0) ? 0 : std::sqrt(_run_squares / static_cast<double>(_run_samples));
        if (_run_max > _worst_max) {
            _worst_max = _run_max;
            std::snprintf(_worst_max_context, sizeof(_worst_max_context), "%s", _context);
        }
        if (rms > _worst_rms) {
            _worst_rms = rms;
            std::snprintf(_worst_rms_context, sizeof(_worst_rms_context), "%s", _context);
        }
        if (_run_max > kMaxIntegerError || rms > kMaxIntegerRmsError) {
            if (_failed_runs++ < kMaxReports) {
                std::printf("FAIL %s: max error %d LSB, RMS %.4f LSB\n", _context, _run_max, rms);
            }
        }
    }

    bool passed() const { return _failed_runs == 0; }
    int worstMax() const { return _worst_max; }
    double worstRms() const { return _worst_rms; }
    const char* worstMaxContext() const { return _worst_max_context; }
    const char* worstRmsContext() const { return _worst_rms_context; }

private:
    int _failed_runs = 0;
    int _worst_max = 0;
    double _worst_rms = 0;
    char _worst_max_context[96] = "none";
    char _worst_rms_context[96] = "none";
};

template <typename Compressor>
void checkSignals(Tally& tally) {
    char context[96];
    for (const int32_t volume : kVolumes) {
        for (const Signal signal : kSignals) {
            for (const int32_t amplitude : kAmplitudes) {
                std::snprintf(context, sizeof(context), "volume %" PRId32 " %s amplitude %" PRId32, volume,
                              signalName(signal), amplitude);
                tally.beginRun(context);
                Compressor compressor(volume);
                ReferenceCompressor reference(volume);
                SignalSource source(signal, amplitude);
                for (int i = 0; i < kSamplesPerRun; ++i) {
                    const int32_t x = source.sample(i);
                    tally.record(compressor.process(x), reference.process(x), x, volume, i);
                }
                tally.endRun();
            }
        }
    }
}

int boundaryInputs(int32_t volume, std::array<int32_t, 12>& inputs) {
    const int64_t edge = std::numeric_limits<int32_t>::max() / volume;
    int count = 0;
    for (const int64_t magnitude : {edge - 1, edge, edge + 1, edge + 2}) {
        for (const int64_t sign : {int64_t{1}, int64_t{-1}}) {
            const int64_t value = sign * magnitude;
            if (value > std::numeric_limits<int32_t>::max() || value <= std::numeric_limits<int32_t>::min()) {
                continue;
            }
            inputs[count++] = static_cast<int32_t>(value);
        }
    }
    return count;
}

template <typename Compressor>
void checkOverflowBoundary(Tally& tally) {
    char context[96];
    for (const int32_t volume : kVolumes) {
        if (volume == 0) continue;
        std::snprintf(context, sizeof(context), "boundary volume %" PRId32, volume);
        tally.beginRun(context);
        std::array<int32_t, 12> inputs{};
        const int count = boundaryInputs(volume, inputs);
        Compressor compressor(volume);
        ReferenceCompressor reference(volume);
        Xorshift noise(0x9E3779B9U);
        for (int i = 0; i < kSamplesPerRun; ++i) {
            const int32_t x = (i % 2 == 0) ? noise.nextIn(kBoundaryNoiseAmplitude) : inputs[(i / 2) % count];
            tally.record(compressor.process(x), reference.process(x), x, volume, i);
        }
        tally.endRun();
    }
}

template <typename Compressor>
void checkVolumeChanges(Tally& tally) {
    tally.beginRun("volume changes");
    Compressor compressor(kVolumes[0]);
    ReferenceCompressor reference(kVolumes[0]);
    Xorshift noise(0xC0FFEE11U);
    size_t volume_index = 0;
    for (int i = 0; i < kVolumeChangeSamples; ++i) {
        if (i % kVolumeChangePeriod == 0) {
            volume_index = (volume_index + 1) % kVolumes.size();
            compressor.setVolume(kVolumes[volume_index]);
            reference.setVolume(kVolumes[volume_index]);
        }
        const int32_t amplitude = kAmplitudes[(i / (kVolumeChangePeriod * kVolumes.size())) % kAmplitudes.size()];
        const int32_t x = noise.nextIn(amplitude);
        tally.record(compressor.process(x), reference.process(x), x, kVolumes[volume_index], i);
    }
    tally.endRun();
}

template <typename Compressor>
void checkAll(Tally& tally) {
    checkSignals<Compressor>(tally);
    checkOverflowBoundary<Compressor>(tally);
    checkVolumeChanges<Compressor>(tally);
}

class RootCheck {
public:
    void check(uint32_t x) {
        ++_values;
        const uint32_t root = squareRootFloor(x);
        const uint64_t r = root;
        if (r * r > x || (r + 1) * (r + 1) <= x) {
            if (_floor_failures++ < 20) std::printf("FAIL squareRootFloor(%" PRIu32 ") = %" PRIu32 "\n", x, root);
        }

        const auto float_root = static_cast<uint32_t>(std::sqrt(static_cast<float>(x)));
        const uint32_t difference = (root > float_root) ? root - float_root : float_root - root;
        if (difference > 1) {
            if (_agreement_failures++ < 20) {
                std::printf("FAIL root agreement at %" PRIu32 ": integer %" PRIu32 ", float %" PRIu32 "\n", x, root,
                            float_root);
            }
        }
        if (difference != 0 && x < _smallest_difference) _smallest_difference = x;
    }

    bool passed() const {
        return _floor_failures == 0 && _agreement_failures == 0 && _smallest_difference >= kRootEqualityLimit;
    }

    void report() const {
        std::printf("compressor_test: squareRootFloor checked on %" PRIu64 " values, %d floor errors, %d roots more "
                    "than 1 from sqrtf; smallest x where they differ: ",
                    _values, _floor_failures, _agreement_failures);
        if (_smallest_difference == UINT32_MAX) {
            std::printf("none\n");
        } else {
            std::printf("%" PRIu32 " (must be >= %" PRIu32 ")\n", _smallest_difference, kRootEqualityLimit);
        }
    }

private:
    uint64_t _values = 0;
    int _floor_failures = 0;
    int _agreement_failures = 0;
    uint32_t _smallest_difference = UINT32_MAX;
};

bool checkSquareRoot() {
    RootCheck roots;
    for (uint32_t x = 0; x < kExhaustiveRootLimit; ++x) roots.check(x);
    for (uint32_t k = 0; k <= 65535; ++k) {
        const uint32_t square = k * k;
        if (k != 0) roots.check(square - 1);
        roots.check(square);
        if (square != UINT32_MAX) roots.check(square + 1);
    }
    roots.check(UINT32_MAX);
    Xorshift random(0x5EED1234U);
    for (int i = 0; i < kRandomRootSamples; ++i) roots.check(random.next());
    roots.report();
    return roots.passed();
}

static_assert(kAmplitudes.back() < (1 << 24), "signal inputs stay below 2^24");

}  // namespace

int main() {
    ExactTally float_tally;
    checkAll<FloatCompressor>(float_tally);
    std::printf("compressor_test: float root: %" PRIu64 " samples compared (%" PRIu64
                " with v * volume beyond 32 bits), %" PRIu64 " mismatches\n",
                float_tally.samples(), float_tally.wideProducts(), float_tally.mismatches());

    const bool roots_passed = checkSquareRoot();

    ToleranceTally integer_tally;
    checkAll<IntegerCompressor>(integer_tally);
    std::printf("compressor_test: integer root: %" PRIu64 " samples compared, %" PRIu64
                " differ; worst max error %d LSB (%s), worst RMS %.4f LSB (%s); bounds %d LSB, %.1f LSB RMS\n",
                integer_tally.samples(), integer_tally.mismatches(), integer_tally.worstMax(),
                integer_tally.worstMaxContext(), integer_tally.worstRms(), integer_tally.worstRmsContext(),
                kMaxIntegerError, kMaxIntegerRmsError);

    std::printf("compressor_test: native root: %s\n",
                kNativeEnvelopeRoot == EnvelopeRoot::Float ? "float" : "integer");

    if (!float_tally.passed() || !roots_passed || !integer_tally.passed()) {
        std::printf("compressor_test: FAIL\n");
        return 1;
    }
    std::printf("compressor_test: PASS\n");
    return 0;
}
