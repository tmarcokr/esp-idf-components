#include "DcBlocker.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>

using Espressif::Wrappers::Audio::DcBlocker;

namespace {

constexpr int kSampleRate = 44100;
constexpr int32_t kFullScaleSum = 32 * 32767;
constexpr int kSquareSamples = 1000;

constexpr std::array<DcBlocker::CutoffPreset, 5> kPresets = {
    DcBlocker::CutoffPreset::Hz150, DcBlocker::CutoffPreset::Hz120, DcBlocker::CutoffPreset::Hz80,
    DcBlocker::CutoffPreset::Hz50,  DcBlocker::CutoffPreset::Hz35,
};

class Checker {
public:
    void expect(bool ok, int32_t coeff, const char* what, int64_t value) {
        if (ok) return;
        ++_failures;
        std::printf("FAIL R=%" PRId32 ": %s (got %" PRId64 ")\n", coeff, what, value);
    }

    void expect(bool ok, int32_t coeff, const char* what, int32_t value) {
        expect(ok, coeff, what, static_cast<int64_t>(value));
    }

    void expect(bool ok, int32_t coeff, const char* what, double value) {
        if (ok) return;
        ++_failures;
        std::printf("FAIL R=%" PRId32 ": %s (got %.6f)\n", coeff, what, value);
    }

    int failures() const { return _failures; }

private:
    int _failures = 0;
};

template <bool kTruncating>
class ReferenceDcBlocker {
public:
    explicit ReferenceDcBlocker(DcBlocker::CutoffPreset preset) : _coeff(static_cast<int32_t>(preset)) {}

    int32_t process(int32_t x) {
        const int64_t product = static_cast<int64_t>(_y_prev) * _coeff;
        const int64_t feedback = kTruncating ? product / 32768 : product >> 15;
        const int64_t y = static_cast<int64_t>(x) - _x_prev + feedback;
        _x_prev = x;
        _y_prev = static_cast<int32_t>(y);
        return _y_prev;
    }

private:
    int32_t _coeff;
    int32_t _x_prev = 0;
    int32_t _y_prev = 0;
};

using FlooringDcBlocker = ReferenceDcBlocker<false>;
using DividingDcBlocker = ReferenceDcBlocker<true>;

template <typename Filter>
int32_t impulseTail(DcBlocker::CutoffPreset preset, int32_t amplitude) {
    Filter filter(preset);
    int32_t y = filter.process(amplitude);
    for (int i = 0; i < kSampleRate; ++i) {
        y = filter.process(0);
    }
    return y;
}

int32_t stepTail(DcBlocker::CutoffPreset preset, int32_t level) {
    DcBlocker filter(preset);
    int32_t y = 0;
    for (int i = 0; i < kSampleRate; ++i) {
        y = filter.process(level);
    }
    return y;
}

int32_t firstDivisionMismatch(DcBlocker::CutoffPreset preset) {
    DcBlocker filter(preset);
    DividingDcBlocker reference(preset);
    uint32_t state = 0x2545F491U;
    for (int i = 0; i < kSampleRate; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const auto x = static_cast<int32_t>(static_cast<int16_t>(state >> 16));
        if (filter.process(x) != reference.process(x)) {
            return i;
        }
    }
    return -1;
}

int32_t settle(DcBlocker& filter) {
    int32_t y = 0;
    for (int i = 0; i < kSampleRate; ++i) {
        y = filter.process(0);
    }
    return y;
}

void checkFullScaleStep(Checker& checker, DcBlocker::CutoffPreset preset, int32_t level) {
    const auto coeff = static_cast<int32_t>(preset);
    DcBlocker filter(preset);

    const int32_t rise = filter.process(level);
    checker.expect(rise == level, coeff, "full-scale step: first output equals the step", rise);
    int32_t y = rise;
    for (int i = 1; i < kSampleRate; ++i) {
        y = filter.process(level);
    }
    checker.expect(y == 0, coeff, "full-scale step: held input decays to exactly 0", y);

    const int32_t fall = filter.process(0);
    checker.expect(fall == -level, coeff, "full-scale step: return to 0 mirrors the step", fall);
    y = settle(filter);
    checker.expect(y == 0, coeff, "full-scale step: decays to exactly 0 after returning to 0", y);
}

void checkFullScaleSquare(Checker& checker, DcBlocker::CutoffPreset preset) {
    const auto coeff = static_cast<int32_t>(preset);
    DcBlocker filter(preset);

    int64_t peak = 0;
    bool signs_follow_input = true;
    int32_t x_prev = 0;
    for (int i = 0; i < kSquareSamples; ++i) {
        const int32_t x = (i % 2 == 0) ? kFullScaleSum : -kFullScaleSum;
        const int32_t y = filter.process(x);
        peak = std::max<int64_t>(peak, y < 0 ? -static_cast<int64_t>(y) : y);
        signs_follow_input = signs_follow_input && ((x > x_prev) == (y > 0));
        x_prev = x;
    }
    checker.expect(peak <= 2 * static_cast<int64_t>(kFullScaleSum), coeff,
                   "full-scale square: |y| stays within 2 x full scale (no int32 wrap)", peak);
    checker.expect(signs_follow_input, coeff, "full-scale square: output sign follows each input edge",
                   static_cast<int64_t>(signs_follow_input));

    const int32_t y = settle(filter);
    checker.expect(y == 0, coeff, "full-scale square: decays to exactly 0 after returning to 0", y);
}

template <typename Filter>
double sineRms(Filter& filter) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFrequency = 1000.0;
    constexpr double kAmplitude = 10000.0;
    double sum = 0.0;
    int counted = 0;
    for (int i = 0; i < kSampleRate; ++i) {
        const auto x = static_cast<int32_t>(std::lround(kAmplitude * std::sin(2.0 * kPi * kFrequency * i / kSampleRate)));
        const int32_t y = filter.process(x);
        if (i >= kSampleRate / 2) {
            sum += static_cast<double>(y) * y;
            ++counted;
        }
    }
    return std::sqrt(sum / counted);
}

}  // namespace

int main() {
    Checker checker;
    for (const DcBlocker::CutoffPreset preset : kPresets) {
        const auto coeff = static_cast<int32_t>(preset);

        const int32_t impulse_pos = impulseTail<DcBlocker>(preset, 20000);
        const int32_t impulse_neg = impulseTail<DcBlocker>(preset, -20000);
        const int32_t flooring_tail = impulseTail<FlooringDcBlocker>(preset, 20000);
        const int32_t step_pos = stepTail(preset, 1000);
        const int32_t step_neg = stepTail(preset, -1000);
        const int32_t division_mismatch = firstDivisionMismatch(preset);

        DcBlocker truncating(preset);
        FlooringDcBlocker flooring(preset);
        const double rms_new = sineRms(truncating);
        const double rms_old = sineRms(flooring);
        const double deviation = std::fabs(rms_new - rms_old) / rms_old;

        std::printf("R=%" PRId32 " impulse +/-20000 -> %" PRId32 "/%" PRId32 ", step +/-1000 -> %" PRId32 "/%" PRId32
                    ", 1 kHz RMS %.2f vs %.2f (%.4f%%), /32768 mismatch at %" PRId32
                    " (old shift: +20000 impulse ends at %" PRId32 ")\n",
                    coeff, impulse_pos, impulse_neg, step_pos, step_neg, rms_new, rms_old, deviation * 100.0,
                    division_mismatch, flooring_tail);

        checker.expect(impulse_pos == 0, coeff, "+20000 impulse decays to exactly 0", impulse_pos);
        checker.expect(impulse_neg == 0, coeff, "-20000 impulse decays to exactly 0", impulse_neg);
        checker.expect(step_pos == 0, coeff, "+1000 step decays to exactly 0", step_pos);
        checker.expect(step_neg == 0, coeff, "-1000 step decays to exactly 0", step_neg);
        checker.expect(deviation <= 0.01, coeff, "1 kHz RMS within 1% of the old filter", deviation);
        checker.expect(division_mismatch == -1, coeff, "output equals the / 32768 reference on noise",
                       static_cast<int64_t>(division_mismatch));

        checkFullScaleStep(checker, preset, kFullScaleSum);
        checkFullScaleStep(checker, preset, -kFullScaleSum);
        checkFullScaleSquare(checker, preset);
    }

    if (checker.failures() != 0) {
        std::printf("dc_blocker_test: FAIL (%d checks)\n", checker.failures());
        return 1;
    }
    std::printf("dc_blocker_test: PASS\n");
    return 0;
}
