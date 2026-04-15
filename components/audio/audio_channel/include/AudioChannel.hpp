#pragma once

#include "esp_err.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace Espressif::Wrappers::Audio {

/**
 * @brief Per-channel audio state: WAV file streaming, ring buffer, volume ramping, and loop control.
 *
 * Each AudioChannel manages its own ring buffer filled from SD card by the reader task,
 * and provides sample-by-sample extraction for the mixer. Supports seamless looping
 * and anti-click volume ramping for SmoothSwing V2 compatibility.
 *
 * Thread safety model:
 * - `getNextSample()` / `isActive()` / `needsRefill()`: called from mixer task only
 * - `refillBuffer()`: called from SD reader task only
 * - `setTargetVolume()`: called from any task (atomic write)
 * - `load()` / `release()`: called from AudioEngine under mutex protection
 */
class AudioChannel {
public:
    /**
     * @brief Parsed WAV file header information.
     */
    struct WavHeader {
        uint32_t sample_rate;       ///< Sample rate from fmt chunk (Hz)
        uint16_t bits_per_sample;   ///< Bits per sample (expected: 16)
        uint16_t num_channels;      ///< Number of channels (expected: 1)
        uint32_t data_offset;       ///< Byte offset to the start of PCM data
        uint32_t data_size;         ///< Size of PCM data in bytes
    };

    /**
     * @brief Construct a new Audio Channel.
     */
    AudioChannel();

    /**
     * @brief Destroy the Audio Channel and release resources.
     */
    ~AudioChannel();

    AudioChannel(const AudioChannel&) = delete;
    AudioChannel& operator=(const AudioChannel&) = delete;

    /**
     * @brief Load a WAV file and prepare this channel for playback.
     *
     * Opens the file, parses and validates the WAV header, fills the initial
     * ring buffer, and sets the channel to active state.
     *
     * @param path Full filesystem path (e.g., "/sdcard/hum.wav").
     * @param loop Enable seamless looping (wraps to data start on EOF).
     * @param initial_volume 14-bit volume (0–16384). Channels at volume 0
     *        still advance playback position (SmoothSwing requirement).
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t load(std::string_view path, bool loop, uint16_t initial_volume);

    /**
     * @brief Release all resources and mark channel as inactive.
     *
     * Closes the file handle and resets internal state. Safe to call
     * even if the channel is not loaded.
     */
    void release();

    /**
     * @brief Extract the next PCM sample with volume scaling and ramping.
     *
     * Called by the mixer at the sample rate (44.1kHz). Applies per-sample
     * exponential volume ramping to prevent audible clicks on volume changes.
     *
     * For looping channels: wraps to data_offset on EOF (seamless).
     * For one-shot channels: marks inactive on EOF.
     *
     * @return Scaled 16-bit PCM sample, or 0 if channel is inactive.
     */
    int16_t getNextSample();

    /**
     * @brief Set the target volume for smooth ramping.
     *
     * Thread-safe (atomic write). The actual volume converges toward
     * the target over ~5ms via exponential decay in getNextSample().
     *
     * @param volume 14-bit target volume (0–16384 = 0%–100%).
     */
    void setTargetVolume(uint16_t volume);

    /**
     * @brief Check if this channel is actively producing samples.
     * @return true if the channel has a loaded file and hasn't finished playback.
     */
    bool isActive() const { return _active; }

    /**
     * @brief Check if the ring buffer needs refilling from SD card.
     *
     * Returns true when the available data in the buffer drops below
     * the watermark threshold (half of buffer capacity).
     *
     * @return true if the SD reader should call refillBuffer().
     */
    bool needsRefill() const;

    /**
     * @brief Refill the ring buffer from the SD card file.
     *
     * Called by the SD reader task. Reads up to half the buffer capacity
     * from the current file position. Handles EOF looping internally.
     *
     * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if channel inactive.
     */
    [[nodiscard]] esp_err_t refillBuffer();

private:
    /// Ring buffer capacity in samples (2048 samples = 4KB @ 16-bit).
    static constexpr size_t RING_BUFFER_SAMPLES = 2048;

    /// Watermark threshold: refill when available samples drop below this.
    static constexpr size_t REFILL_WATERMARK = 1024;

    /// Volume precision: 14-bit (0–16384).
    static constexpr uint16_t MAX_VOLUME = 16384;

    // --- State ---
    bool _active;
    bool _loop_enabled;
    std::string _file_path;

    // --- File I/O ---
    FILE* _file;
    WavHeader _wav_header;
    uint32_t _file_position;        ///< Current read position in data section (bytes)

    // --- Ring Buffer ---
    int16_t _ring_buffer[RING_BUFFER_SAMPLES];
    volatile size_t _write_index;   ///< Next write position (SD reader task)
    volatile size_t _read_index;    ///< Next read position (mixer task)

    // --- Volume ---
    std::atomic<uint16_t> _target_volume;
    uint16_t _current_volume;

    /**
     * @brief Parse and validate a WAV file header.
     * @param file Open file handle positioned at byte 0.
     * @param[out] header Parsed header data.
     * @return esp_err_t ESP_OK if valid 16-bit mono PCM WAV.
     */
    static esp_err_t parseWavHeader(FILE* file, WavHeader& header);

    /**
     * @brief Calculate the number of samples available in the ring buffer.
     * @return Number of unread samples.
     */
    size_t availableSamples() const;

    /**
     * @brief Apply exponential volume ramping toward the target.
     *
     * Updates _current_volume by (delta / 256) per sample call,
     * converging to _target_volume over ~5ms at 44.1kHz.
     */
    void updateVolumeRamp();

    /**
     * @brief Read samples from file into a destination buffer, handling EOF/loop.
     * @param dest Destination buffer.
     * @param samples_requested Number of samples to read.
     * @return Number of samples actually read.
     */
    size_t readFromFile(int16_t* dest, size_t samples_requested);
};

} // namespace Espressif::Wrappers::Audio
