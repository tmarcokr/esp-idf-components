#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "hal/gpio_types.h" // IWYU pragma: keep
#include <cstdint>
#include <vector>

namespace Espressif::Wrappers {

/**
 * @brief RAII wrapper for I2S Standard Mode transmitter with DMA double-buffering.
 *
 * Designed for mono 16-bit PCM output to a MAX98357A Class-D amplifier.
 * Uses the ESP-IDF v5 I2S channel-based API with DMA for CPU-efficient transfers.
 */
class I2sTransmitter {
public:
    /**
     * @brief Configuration for the I2S transmitter hardware interface.
     */
    struct Config {
        gpio_num_t bclk_pin;                    ///< I2S bit clock (BCK/SCK)
        gpio_num_t ws_pin;                      ///< I2S word select (LRCLK)
        gpio_num_t dout_pin;                    ///< I2S data out (DIN on MAX98357A)
        uint32_t sample_rate        = 44100;    ///< Sample rate in Hz
        uint32_t dma_frame_count    = 256;      ///< Frames per DMA buffer (5.8ms @ 44.1kHz)
        uint32_t dma_desc_count     = 2;        ///< Number of DMA descriptors (double-buffering)
    };

    /**
     * @brief Construct a new I2S Transmitter.
     * @param config I2S hardware configuration.
     */
    explicit I2sTransmitter(const Config& config);

    /**
     * @brief Destroy the I2S Transmitter, freeing all DMA resources.
     */
    ~I2sTransmitter();

    I2sTransmitter(const I2sTransmitter&) = delete;
    I2sTransmitter& operator=(const I2sTransmitter&) = delete;

    /**
     * @brief Initialize and enable the I2S transmit channel.
     *
     * Configures I2S Standard Mode with:
     * - 16-bit sample depth
     * - Mono channel (left-aligned, data duplicated to both L/R)
     * - Philips format for MAX98357A compatibility
     *
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Write PCM frames to the I2S DMA buffer (blocking).
     *
     * This call blocks until the DMA is ready to accept new data, making it
     * naturally paced by the I2S clock. The mixer task should call this in a
     * loop to maintain continuous audio output.
     *
     * @param data Pointer to 16-bit signed PCM samples (mono).
     * @param frame_count Number of frames to write.
     * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on DMA timeout.
     */
    [[nodiscard]] esp_err_t write(const int16_t* data, size_t frame_count);

    /**
     * @brief Get the configured DMA frame count.
     * @return Number of frames per DMA buffer period.
     */
    uint32_t getFrameCount() const { return _config.dma_frame_count; }

    /**
     * @brief Get the configured sample rate.
     * @return Sample rate in Hz.
     */
    uint32_t getSampleRate() const { return _config.sample_rate; }

private:
    Config _config;
    i2s_chan_handle_t _tx_handle;
    bool _initialized;

    /// Temporary buffer for mono-to-stereo expansion (required for C6 stability)
    std::vector<int16_t> _stereo_buffer;

    /// @brief I2S write timeout in milliseconds (increased to 1000ms for safety margin).
    static constexpr int WRITE_TIMEOUT_MS = 1000;
};

} // namespace Espressif::Wrappers
