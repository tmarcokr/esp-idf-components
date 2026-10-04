#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "DynamicRangeCompressor.hpp"
#include "DcBlocker.hpp"

namespace Espressif::Wrappers::Audio {

// Forward declaration
class AudioChannel;

/**
 * @brief Polyphonic audio mixer with 32-bit accumulation and soft-clipping.
 *
 * Sums samples from all active AudioChannels into a single mono output buffer,
 * applies global volume scaling, soft-clipping to prevent DAC overflow, and
 * tracks RMS output level for LED reactivity.
 *
 * This class is internal to the AudioEngine and should NOT be used directly.
 */
class PolyphonicMixer {
public:
    /// Maximum channels one mixer can handle (one bit per channel in a uint32_t mask).
    static constexpr uint8_t MAX_CHANNELS = 32;

    /**
     * @brief Mixer statistics returned by takeStats().
     */
    struct Stats {
        uint32_t underruns;         ///< Underrun samples over all channels since construction.
        uint32_t group_holds;       ///< Group-cycles in which a linked group was held, since construction.
        int32_t peak_in;            ///< Max |sum| before DSP since the previous takeStats().
        int32_t peak_out;           ///< Max |sample| after DSP since the previous takeStats().
        uint32_t clipped_samples;   ///< Samples at full scale after DSP since the previous takeStats().
    };

    /**
     * @brief Construct a new Polyphonic Mixer.
     *
     * Warning: @p channels must outlive the mixer; only the first MAX_CHANNELS are mixed.
     *
     * @param channels Channel storage owned by the caller.
     * @param compressor_gain_threshold Baseline threshold for the dynamic range compressor.
     * @param dc_cutoff High-pass filter cutoff preset for the DC Blocker.
     */
    PolyphonicMixer(std::span<AudioChannel> channels, uint16_t compressor_gain_threshold, DcBlocker::CutoffPreset dc_cutoff);
    ~PolyphonicMixer() = default;

    PolyphonicMixer(const PolyphonicMixer&) = delete;
    PolyphonicMixer& operator=(const PolyphonicMixer&) = delete;

    /**
     * @brief Mix all Active and Stopping channels into the output buffer.
     *
     * Snapshots every channel once per call (beginMixCycle()), mixes the frames,
     * then publishes the consumed samples (endMixCycle()). Active channels that share a
     * non-zero group id are held together for the whole call (no sample consumed or output)
     * when one of them has fewer than @p frame_count samples buffered and none has its
     * end of file buffered, so they stay sample-aligned. For each frame:
     * 1. Sum all active channel samples into a 32-bit accumulator
     * 2. Apply global volume scaling (14-bit fixed-point)
     * 3. Apply soft-clipping to prevent DAC overflow
     * 4. Update running RMS tracker
     *
     * @param output Destination buffer for 16-bit mono PCM samples.
     * @param frame_count Number of frames to produce.
     */
    void mixFrames(int16_t* output, size_t frame_count);

    /**
     * @brief Set the global master volume.
     * @param volume 14-bit volume (0–16384 = 0%–100%).
     */
    void setGlobalVolume(uint16_t volume);

    /**
     * @brief Get the current RMS output level.
     *
     * Tracks the running RMS over a ~100ms window (4410 samples @ 44.1kHz).
     * Useful for LED blade reactivity tied to audio intensity.
     *
     * @return 0–16384 (0%–100% of maximum output).
     */
    uint16_t getOutputLevel() const;

    /**
     * @brief Lock-free snapshot of the mixer statistics; resets the peak and clip fields.
     *
     * Safe to call from any task while mixFrames() runs.
     *
     * @return The statistics accumulated since the previous call (underruns: since construction).
     */
    Stats takeStats();

private:
    std::span<AudioChannel> _channels;
    uint16_t _global_volume;
    uint16_t _compressor_gain_threshold;

    DynamicRangeCompressor _compressor;
    DcBlocker _dc_blocker;

    // Relaxed is enough: no other data is published or read through these counters.
    std::atomic<uint32_t> _underruns{0};
    std::atomic<uint32_t> _group_holds{0};
    std::atomic<int32_t> _peak_in{0};
    std::atomic<int32_t> _peak_out{0};
    std::atomic<uint32_t> _clipped_samples{0};

    /// Window size for RMS calculation (~100ms @ 44.1kHz)
    static constexpr size_t RMS_WINDOW_SAMPLES = 4410;
    uint64_t _rms_accumulator;      ///< Running sum of squared samples
    size_t _rms_sample_count;       ///< Samples processed in current window
    uint16_t _rms_level;            ///< Last computed RMS level (0–16384)

    static constexpr uint16_t MAX_VOLUME = 16384;

    /**
     * @brief Compute the final compressor gain from the Q14 volume and the base threshold.
     */
    int32_t volumeToCompressorGain(uint16_t q14_volume) const;

    /**
     * @brief Update the running RMS tracker with a new output sample.
     * @param sample The final 16-bit output sample.
     */
    void updateRms(int16_t sample);

    uint32_t heldChannels(uint32_t mixed_mask, size_t frame_count, uint32_t& holds);
};

} // namespace Espressif::Wrappers::Audio
