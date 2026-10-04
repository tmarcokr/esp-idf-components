#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace Espressif::Wrappers::Audio {

/**
 * @brief How the compressor takes the square root of its envelope.
 */
enum class EnvelopeRoot : uint8_t {
    Float,    ///< (int)sqrtf((float)x); bit-exact with the original compressor.
    Integer,  ///< squareRootFloor(); differs from Float by at most 1, only for envelopes above about 2^22.
};

#if defined(__XTENSA_SOFT_FLOAT__) || defined(__riscv_float_abi_soft)
/// Root used by DynamicRangeCompressor: Integer on targets without an FPU.
inline constexpr EnvelopeRoot kNativeEnvelopeRoot = EnvelopeRoot::Integer;
#else
/// Root used by DynamicRangeCompressor: Float on targets with an FPU.
inline constexpr EnvelopeRoot kNativeEnvelopeRoot = EnvelopeRoot::Float;
#endif

/**
 * @brief Exact integer square root, floor(sqrt(x)), in constant time without division.
 * @param x Value.
 * @return The largest r with r * r <= x.
 */
inline constexpr uint32_t squareRootFloor(uint32_t x) {
    // Trick: digit-by-digit (bit-pair) method; each of the 16 steps fixes one bit of the root,
    // a fixed count instead of skipping leading zero pairs, so the time does not depend on x.
    uint32_t root = 0;
    uint32_t bit = 1U << 30;
    for (int step = 0; step < 16; ++step) {
        const uint32_t trial = root + bit;
        root >>= 1;
        if (x >= trial) {
            x -= trial;
            root += bit;
        }
        bit >>= 2;
    }
    return root;
}

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
 *
 * The formula is the same on every target. With the float root (targets with an FPU: ESP32,
 * ESP32-S3) the output is bit-exact with the original compressor. With the integer root
 * (targets without an FPU, such as the ESP32-C6) the divisor differs by at most 1, only for
 * large envelopes, which moves a sample by less than 17 LSB.
 *
 * @tparam Root Square root of the envelope; use the DynamicRangeCompressor alias, which picks
 *         the float root on targets with an FPU and the integer root otherwise.
 */
template <EnvelopeRoot Root>
class BasicDynamicRangeCompressor {
public:
    /**
     * @brief Construct a new Dynamic Range Compressor.
     * @param volume Target threshold volume for gain compression.
     */
    explicit BasicDynamicRangeCompressor(int32_t volume = kDefaultVolume)
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

        const int32_t volume = _volume.load(std::memory_order_relaxed);

        // Square-root-law gain reduction. sqrtf would run in software without an FPU, so those
        // targets use the exact integer root instead.
        int32_t divisor;
        if constexpr (Root == EnvelopeRoot::Float) {
            divisor = static_cast<int32_t>(std::sqrt(static_cast<float>(_vol_avg))) + 100;
        } else {
            divisor = static_cast<int32_t>(squareRootFloor(_vol_avg)) + 100;
        }

        // Cap gain at unity. The mixer must only attenuate loud passages to
        // prevent clipping, never amplify them. Since gain = volume / divisor,
        // capping gain <= 1 means divisor >= volume.
        if (divisor < volume) divisor = volume;

        // Trick: v * volume nearly always fits in 32 bits; the 32-bit division is a single hardware
        // instruction, the 64-bit one a __divdi3 libcall. Both truncate toward zero, so the result is identical.
        int32_t product;
        const int32_t out = __builtin_mul_overflow(v, volume, &product)
            ? static_cast<int32_t>((static_cast<int64_t>(v) * volume) / divisor)
            : product / divisor;

        return clampToInt16(out);
    }

    /**
     * @brief Set the compression volume parameter.
     *
     * Safe to call from any task while another task runs process() (lock-free atomic store).
     *
     * @param volume Target volume threshold.
     */
    void setVolume(int32_t volume) { _volume.store(volume, std::memory_order_relaxed); }

    /**
     * @brief Get the current compression volume parameter; callable from any task.
     * @return Volume threshold.
     */
    int32_t volume() const { return _volume.load(std::memory_order_relaxed); }

    /**
     * @brief Get the running average rectified volume envelope.
     *
     * Not synchronized: call it only from the task that calls process().
     *
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

    // Relaxed is enough: no other data is published or read through the volume.
    std::atomic<int32_t> _volume;
    uint32_t _vol_avg;
};

/**
 * @brief The compressor with the native envelope root of the target (see kNativeEnvelopeRoot).
 *
 * An alias of a class template, so it cannot be forward-declared with
 * `class DynamicRangeCompressor;`; include this header instead.
 */
using DynamicRangeCompressor = BasicDynamicRangeCompressor<kNativeEnvelopeRoot>;

} // namespace Espressif::Wrappers::Audio
