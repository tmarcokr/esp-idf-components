#include "DynamicRangeCompressor.hpp"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using Espressif::Wrappers::Audio::DynamicRangeCompressor;

namespace {

constexpr int kSamplesPerRun = 44100;
constexpr int kSinePeriod = 44;
constexpr int kSquareHalfPeriod = 300;
constexpr int kBurstLength = 2500;
constexpr int kVolumeChangePeriod = 1000;
constexpr int kVolumeChangeSamples = 200000;
constexpr int kBoundaryNoiseAmplitude = 3000;

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
    void compare(DynamicRangeCompressor& compressor, ReferenceCompressor& reference, int32_t x, int32_t volume,
                 int index, const char* context) {
        ++_samples;
        if (static_cast<int64_t>(x) * volume != static_cast<int32_t>(static_cast<int64_t>(x) * volume)) {
            ++_wide_products;
        }
        const int16_t actual = compressor.process(x);
        const int16_t expected = reference.process(x);
        if (actual == expected) return;
        if (_mismatches++ < kMaxReports) {
            std::printf("FAIL %s index %d: got %d, reference %d\n", context, index, actual, expected);
        }
    }

    uint64_t samples() const { return _samples; }
    uint64_t wideProducts() const { return _wide_products; }
    uint64_t mismatches() const { return _mismatches; }

private:
    static constexpr int kMaxReports = 20;
    uint64_t _samples = 0;
    uint64_t _wide_products = 0;
    uint64_t _mismatches = 0;
};

void checkSignals(Tally& tally) {
    char context[96];
    for (const int32_t volume : kVolumes) {
        for (const Signal signal : kSignals) {
            for (const int32_t amplitude : kAmplitudes) {
                std::snprintf(context, sizeof(context), "volume %" PRId32 " %s amplitude %" PRId32, volume,
                              signalName(signal), amplitude);
                DynamicRangeCompressor compressor(volume);
                ReferenceCompressor reference(volume);
                SignalSource source(signal, amplitude);
                for (int i = 0; i < kSamplesPerRun; ++i) {
                    const int32_t x = source.sample(i);
                    tally.compare(compressor, reference, x, volume, i, context);
                }
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

void checkOverflowBoundary(Tally& tally) {
    char context[96];
    for (const int32_t volume : kVolumes) {
        if (volume == 0) continue;
        std::snprintf(context, sizeof(context), "boundary volume %" PRId32, volume);
        std::array<int32_t, 12> inputs{};
        const int count = boundaryInputs(volume, inputs);
        DynamicRangeCompressor compressor(volume);
        ReferenceCompressor reference(volume);
        Xorshift noise(0x9E3779B9U);
        for (int i = 0; i < kSamplesPerRun; ++i) {
            const int32_t x = (i % 2 == 0) ? noise.nextIn(kBoundaryNoiseAmplitude) : inputs[(i / 2) % count];
            tally.compare(compressor, reference, x, volume, i, context);
        }
    }
}

void checkVolumeChanges(Tally& tally) {
    DynamicRangeCompressor compressor(kVolumes[0]);
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
        tally.compare(compressor, reference, x, kVolumes[volume_index], i, "volume changes");
    }
}

static_assert(kAmplitudes.back() < (1 << 24), "signal inputs stay below 2^24");

}  // namespace

int main() {
    Tally tally;
    checkSignals(tally);
    checkOverflowBoundary(tally);
    checkVolumeChanges(tally);

    std::printf("compressor_test: %" PRIu64 " samples compared (%" PRIu64 " with v * volume beyond 32 bits), %" PRIu64
                " mismatches\n",
                tally.samples(), tally.wideProducts(), tally.mismatches());
    if (tally.mismatches() != 0) {
        std::printf("compressor_test: FAIL\n");
        return 1;
    }
    std::printf("compressor_test: PASS\n");
    return 0;
}
