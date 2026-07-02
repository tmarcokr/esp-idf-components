#pragma once

#include <cmath>
#include <cstdint>

namespace Espressif::Wrappers::Audio {

/**
 * @brief Square-root-law auto-gain compressor for dynamic range control.
 *
 * Maintains a leaky running average of the rectified input signal envelope
 * with a time constant of approximately 256 samples (~5.8 ms @ 44.1 kHz).
 *
 * The output sample is computed as:
 *   out = in * volume / (sqrt(average_envelope) + 100)
 *
 * Transients are dynamically compressed before the final output clamping
 * to prevent digital clipping and mechanical speaker distortion.
 */
class DynamicRangeCompressor {
public:
    /**
     * @brief Construct a new Dynamic Range Compressor.
     * @param volume Target threshold volume for gain compression.
     */
    explicit DynamicRangeCompressor(int32_t volume = kDefaultVolume)
        : _volume(volume), _vol_avg(0) {}

    /**
     * @brief Process a single 32-bit sample, applying compression and clamping.
     * @param v Input sample to compress.
     * @return Compressed and clamped 16-bit output sample.
     */
    int16_t process(int32_t v) {
        // Leaky integrator of |v| — one-pole IIR, tau ~= 256 samples.
        _vol_avg += static_cast<uint32_t>(v < 0 ? -v : v);
        _vol_avg -= (_vol_avg + 255) >> 8;

        // Square-root-law gain reduction. float sqrt is fine on ESP32 (FPU).
        int32_t divisor = static_cast<int32_t>(std::sqrt(static_cast<float>(_vol_avg))) + 100;

        // Cap gain at unity. The mixer must only attenuate loud passages to
        // prevent clipping, never amplify them. Since gain = volume / divisor,
        // capping gain <= 1 means divisor >= volume.
        if (divisor < _volume) divisor = _volume;

        int32_t out = static_cast<int32_t>((static_cast<int64_t>(v) * _volume) / divisor);

        return clampToInt16(out);
    }

    /**
     * @brief Set the compression volume parameter.
     * @param volume Target volume threshold.
     */
    void setVolume(int32_t volume) { _volume = volume; }

    /**
     * @brief Get the current compression volume parameter.
     * @return Volume threshold.
     */
    int32_t volume() const { return _volume; }

    /**
     * @brief Get the running average rectified volume envelope.
     * @return Running average envelope level.
     */
    uint32_t averageVolume() const { return _vol_avg; }

    /// Default target volume threshold for gain compression logic.
    static constexpr int32_t kDefaultVolume = 2000;

private:
    static int16_t clampToInt16(int32_t x) {
        if (x > 32767) return 32767;
        if (x < -32768) return -32768;
        return static_cast<int16_t>(x);
    }

    int32_t _volume;
    uint32_t _vol_avg;
};

} // namespace Espressif::Wrappers::Audio
