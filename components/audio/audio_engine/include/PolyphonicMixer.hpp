#pragma once

#include <cstddef>
#include <cstdint>

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
    /**
     * @brief Construct a new Polyphonic Mixer.
     * @param channels Pointer to the array of AudioChannel pointers.
     * @param max_channels Maximum number of channels in the array.
     */
    PolyphonicMixer(AudioChannel** channels, uint8_t max_channels);
    ~PolyphonicMixer() = default;

    PolyphonicMixer(const PolyphonicMixer&) = delete;
    PolyphonicMixer& operator=(const PolyphonicMixer&) = delete;

    /**
     * @brief Mix all active channels into the output buffer.
     *
     * For each frame:
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

private:
    AudioChannel** _channels;
    uint8_t _max_channels;
    uint16_t _global_volume;

    /// Window size for RMS calculation (~100ms @ 44.1kHz)
    static constexpr size_t RMS_WINDOW_SAMPLES = 4410;
    uint64_t _rms_accumulator;      ///< Running sum of squared samples
    size_t _rms_sample_count;       ///< Samples processed in current window
    uint16_t _rms_level;            ///< Last computed RMS level (0–16384)

    static constexpr uint16_t MAX_VOLUME = 16384;

    /**
     * @brief Apply soft-clipping to prevent harsh digital distortion.
     *
     * Uses a hyperbolic tangent approximation for smooth gain reduction
     * near the clipping threshold, preserving audio dynamics.
     *
     * @param mixed 32-bit accumulated sample (may exceed int16 range).
     * @return Clipped 16-bit sample within [-32768, 32767].
     */
    static int16_t applySoftClipping(int32_t mixed);

    /**
     * @brief Update the running RMS tracker with a new output sample.
     * @param sample The final 16-bit output sample.
     */
    void updateRms(int16_t sample);
};

} // namespace Espressif::Wrappers::Audio
