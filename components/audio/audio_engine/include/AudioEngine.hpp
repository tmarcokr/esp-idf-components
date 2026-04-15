#pragma once

#include "esp_err.h"
#include "hal/gpio_types.h" // IWYU pragma: keep
#include <cstdint>
#include <string_view>

namespace Espressif::Wrappers::Audio {

/// Channel identifier type. Valid IDs are >= 0.
using ChannelId = int8_t;

/// Returned when channel allocation fails.
constexpr ChannelId INVALID_CHANNEL = -1;

/**
 * @brief Polyphonic audio engine for ESP32 with real-time mixing and I2S output.
 *
 * Orchestrates multiple AudioChannels through a PolyphonicMixer, streaming WAV
 * files from SD card and outputting 16-bit mono PCM via I2S to a DAC/amplifier
 * (e.g., MAX98357A).
 *
 * Architecture:
 * - Spawns two FreeRTOS tasks: mixer (priority 10) and SD reader (priority 3)
 * - Mixer task is DMA-paced: fills 256-frame buffers at ~172Hz
 * - SD reader task refills per-channel ring buffers on watermark trigger
 * - Volume commands are lock-free (atomic) for real-time safety
 *
 * Thread safety:
 * - play() / stop(): protected by internal mutex
 * - setChannelVolume() / setGlobalVolume(): lock-free (atomic writes)
 * - getOutputLevel(): lock-free read
 *
 * Usage (future — not integrated into main.cpp yet):
 * @code
 *   AudioEngine::Config cfg = { .bclk_pin = GPIO_NUM_4, .ws_pin = GPIO_NUM_5, .dout_pin = GPIO_NUM_6 };
 *   AudioEngine engine(cfg);
 *   ESP_ERROR_CHECK(engine.init());
 *   ESP_ERROR_CHECK(engine.start());
 *   ChannelId hum = engine.play("/sdcard/hum.wav", true, 10000);
 *   engine.setChannelVolume(hum, 8000);
 * @endcode
 */
class AudioEngine {
public:
    /**
     * @brief Hardware and engine configuration.
     */
    struct Config {
        gpio_num_t bclk_pin;                    ///< I2S bit clock pin
        gpio_num_t ws_pin;                      ///< I2S word select pin
        gpio_num_t dout_pin;                    ///< I2S data out pin
        gpio_num_t sd_mode_pin  = GPIO_NUM_NC;  ///< MAX98357A SD_MODE pin for anti-pop
        uint32_t sample_rate    = 44100;        ///< Output sample rate (Hz)
        uint8_t max_channels    = 9;            ///< Maximum simultaneous channels
    };

    /**
     * @brief Construct a new Audio Engine.
     * @param config Hardware and engine configuration.
     */
    explicit AudioEngine(const Config& config);

    /**
     * @brief Destroy the Audio Engine, stopping all tasks and freeing resources.
     */
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * @brief Initialize I2S hardware and allocate internal resources.
     *
     * Must be called before start(). Initializes the I2S transmitter
     * and allocates the channel array and mixer.
     *
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Spawn the mixer and SD reader FreeRTOS tasks.
     *
     * After this call, the engine is actively outputting silence via I2S.
     * Use play() to begin audio playback on channels.
     *
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t start();

    /**
     * @brief Start audio playback on an available channel.
     *
     * Loads the WAV file, allocates a channel, and begins sample extraction.
     * Thread-safe (mutex-protected).
     *
     * @param file_path SD card path (e.g., "/sdcard/hum.wav").
     * @param loop Seamless looping flag (required for hum/swing channels).
     * @param initial_volume 14-bit volume (0–16384). Use 0 for SmoothSwing
     *        channels that need phase-aligned silent startup.
     * @return Channel ID (>= 0) on success, INVALID_CHANNEL on failure.
     */
    ChannelId play(std::string_view file_path,
                   bool loop = false,
                   uint16_t initial_volume = 16384);

    /**
     * @brief Stop a channel with a brief fade-out to prevent clicks.
     *
     * Sets volume to 0 (which triggers a ~5ms ramp-down), then releases
     * the channel resources on the next mixer cycle.
     * Thread-safe (mutex-protected).
     *
     * @param id Channel ID returned by play().
     */
    void stop(ChannelId id);

    /**
     * @brief Update a channel's target volume (thread-safe, lock-free).
     *
     * The volume change is applied gradually via exponential ramping
     * in the mixer task to prevent audible clicks.
     *
     * @param id Channel ID.
     * @param target_volume 14-bit volume (0–16384).
     */
    void setChannelVolume(ChannelId id, uint16_t target_volume);

    /**
     * @brief Set the global master volume (thread-safe, lock-free).
     *
     * Applies to the mixed output of all channels before I2S write.
     *
     * @param target_volume 14-bit volume (0–16384).
     */
    void setGlobalVolume(uint16_t target_volume);

    /**
     * @brief Get the current RMS output level for LED reactivity.
     *
     * Returns the RMS power level averaged over a 100ms window.
     * Thread-safe (lock-free read).
     *
     * @return 0–16384 (0%–100% of maximum output).
     */
    uint16_t getOutputLevel() const;

private:
    friend struct AudioEngineImpl;
    struct AudioEngineImpl* _impl; ///< Opaque implementation (PIMPL)
};

} // namespace Espressif::Wrappers::Audio
