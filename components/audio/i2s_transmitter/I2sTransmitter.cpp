#include "I2sTransmitter.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep

namespace Espressif::Wrappers {

static constexpr const char* TAG = "I2sTransmitter";

I2sTransmitter::I2sTransmitter(const Config& config)
    : _config(config), _tx_handle(nullptr), _initialized(false) {}

I2sTransmitter::~I2sTransmitter() {
    if (_initialized && _tx_handle) {
        i2s_channel_disable(_tx_handle);
        i2s_del_channel(_tx_handle);
        ESP_LOGI(TAG, "I2S channel resources freed.");
    }
}

esp_err_t I2sTransmitter::init() {
    if (_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing I2S transmitter (BCK:%d, WS:%d, DOUT:%d, %luHz)...",
             _config.bclk_pin, _config.ws_pin, _config.dout_pin,
             static_cast<unsigned long>(_config.sample_rate));

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4; // Increased from 2 for better stability on C6
    chan_cfg.dma_frame_num = 256; 
    // Removed auto_clear: true to prevent DMA stall corner cases on ESP32-C6.

    esp_err_t ret = i2s_new_channel(&chan_cfg, &_tx_handle, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(_config.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = _config.bclk_pin,
            .ws   = _config.ws_pin,
            .dout = _config.dout_pin,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    
    // Force PLL_160M on C6 to ensure stable clock generation
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;

    ret = i2s_channel_init_std_mode(_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(_tx_handle);
        _tx_handle = nullptr;
        return ret;
    }

    ret = i2s_channel_enable(_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(_tx_handle);
        _tx_handle = nullptr;
        return ret;
    }

    _stereo_buffer.resize(_config.dma_frame_count * 2);

    _initialized = true;
    ESP_LOGI(TAG, "I2S transmitter initialized (DMA: %lu frames x 4 descriptors).",
             static_cast<unsigned long>(_config.dma_frame_count));
    return ESP_OK;
}

esp_err_t I2sTransmitter::write(const int16_t* data, size_t frame_count) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    // Expand Mono samples to Stereo slots (L=R) for hardware compatibility
    // Since we are in I2S_SLOT_MODE_STEREO, the DMA expects 2 samples per frame.
    if (_stereo_buffer.size() < frame_count * 2) {
        _stereo_buffer.resize(frame_count * 2);
    }

    for (size_t i = 0; i < frame_count; ++i) {
        _stereo_buffer[i * 2]     = data[i]; // Left slot
        _stereo_buffer[i * 2 + 1] = data[i]; // Right slot
    }

    const size_t bytes_to_write = frame_count * 2 * sizeof(int16_t);
    size_t bytes_written = 0;

    esp_err_t ret = i2s_channel_write(_tx_handle, _stereo_buffer.data(), bytes_to_write,
                                       &bytes_written, pdMS_TO_TICKS(WRITE_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S write error: %s (wrote %zu/%zu bytes)",
                 esp_err_to_name(ret), bytes_written, bytes_to_write);
        return ret;
    }

    return ESP_OK;
}

} // namespace Espressif::Wrappers
