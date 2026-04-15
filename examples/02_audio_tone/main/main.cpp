#include "I2sTransmitter.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <cmath>
#include <vector>

static const char *TAG = "AUDIO_TONE_EXAMPLE";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Audio Tone Example (440Hz Sine Wave)...");

    // Configure I2S pins for MAX98357A amplifier
    Espressif::Wrappers::I2sTransmitter::Config i2s_cfg = {
        .bclk_pin = GPIO_NUM_18,   // BCLK (Bit Clock)
        .ws_pin = GPIO_NUM_19,     // LRC (Word Select / Left-Right Clock)
        .dout_pin = GPIO_NUM_20,   // DIN (Data In)
        .sample_rate = 44100,
        .dma_frame_count = 256,
        .dma_desc_count = 2
    };

    Espressif::Wrappers::I2sTransmitter transmitter(i2s_cfg);

    if (transmitter.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S Transmitter!");
        return;
    }

    ESP_LOGI(TAG, "I2S initialized. Generating 440Hz tone...");

    const float frequency = 440.0f; // A4 Note
    const float amplitude = 10000.0f; // Volume (max for 16-bit is ~32767)
    const float phase_increment = 2.0f * M_PI * frequency / i2s_cfg.sample_rate;
    float phase = 0.0f;

    std::vector<int16_t> audio_buffer(i2s_cfg.dma_frame_count);

    while (true) {
        // Generate a chunk of sine wave
        for (size_t i = 0; i < i2s_cfg.dma_frame_count; ++i) {
            audio_buffer[i] = static_cast<int16_t>(amplitude * std::sin(phase));
            phase += phase_increment;
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }

        // Send to DMA buffer (this will block until DMA is ready for more data, ensuring perfect pacing)
        esp_err_t err = transmitter.write(audio_buffer.data(), audio_buffer.size());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S Write timeout or error!");
        }
    }
}
