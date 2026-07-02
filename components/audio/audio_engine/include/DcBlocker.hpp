#pragma once

#include <cstdint>

namespace Espressif::Wrappers::Audio {

/**
 * @brief One-pole DC blocker and sub-bass high-pass filter.
 *
 * Difference equation:  y[n] = x[n] - x[n-1] + R * y[n-1]
 *
 * Small speakers (e.g., 4 Ohm / 3 W in small enclosures) cannot reproduce sub-bass.
 * Below resonance, the cone suffers mechanical distortion without producing audible bass.
 * Filtering out this low-frequency energy before amplification prevents speaker
 * distortion and maximizes perceived loudness.
 */
class DcBlocker {
public:
    /**
     * @brief Cutoff frequency presets for the DC blocker.
     *
     * Defines coefficient R in Q15 format (R * 32768) for different -3 dB cutoff
     * frequencies at a 44.1 kHz sample rate.
     */
    enum class CutoffPreset : int32_t {
        Hz150 = 32068, ///< Safest cutoff (~150 Hz), preserves tiny speakers. R = 0.9786.
        Hz120 = 32207, ///< Balanced cutoff (~120 Hz). R = 0.9829.
        Hz80  = 32394, ///< Moderate cutoff (~80 Hz), more bass. R = 0.9886.
        Hz50  = 32534, ///< Low cutoff (~50 Hz), near full-range. R = 0.9929.
        Hz35  = 32604  ///< Maximum bass response (~35 Hz), high distortion risk. R = 0.9950.
    };

    /**
     * @brief Construct a new Dc Blocker object with a cutoff preset.
     * @param preset The desired high-pass filter cutoff preset.
     */
    explicit DcBlocker(CutoffPreset preset = CutoffPreset::Hz150)
        : _cutoff_coeff(static_cast<int32_t>(preset)) {}

    /**
     * @brief Process a single 32-bit audio sample.
     * @param x Input sample.
     * @return Filtered output sample.
     */
    int32_t process(int32_t x) {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        int64_t y = static_cast<int64_t>(x) - _x_prev
                    + ((static_cast<int64_t>(_y_prev) * _cutoff_coeff) >> 15);
        _x_prev = x;
        _y_prev = static_cast<int32_t>(y);
        return _y_prev;
    }

    /**
     * @brief Reset filter state variables.
     */
    void reset() {
        _x_prev = 0;
        _y_prev = 0;
    }

private:
    int32_t _cutoff_coeff;
    int32_t _x_prev = 0;
    int32_t _y_prev = 0;
};

} // namespace Espressif::Wrappers::Audio
