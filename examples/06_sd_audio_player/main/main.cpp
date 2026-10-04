#include "sd_card.hpp"
#include "AudioEngine.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "SD_AUDIO_EXAMPLE";

// Pin map per target: see README.md (on the ESP32-S3 DevKitC-1, GPIO 19/20 are the native USB pins).
#if CONFIG_IDF_TARGET_ESP32
static constexpr gpio_num_t kSdMiso = GPIO_NUM_19;
static constexpr gpio_num_t kSdMosi = GPIO_NUM_23;
static constexpr gpio_num_t kSdSck = GPIO_NUM_18;
static constexpr gpio_num_t kSdCs = GPIO_NUM_5;
static constexpr gpio_num_t kI2sBclk = GPIO_NUM_26;
static constexpr gpio_num_t kI2sWs = GPIO_NUM_25;
static constexpr gpio_num_t kI2sDout = GPIO_NUM_22;
#else
static constexpr gpio_num_t kSdMiso = GPIO_NUM_4;
static constexpr gpio_num_t kSdMosi = GPIO_NUM_11;
static constexpr gpio_num_t kSdSck = GPIO_NUM_7;
static constexpr gpio_num_t kSdCs = GPIO_NUM_10;
static constexpr gpio_num_t kI2sBclk = GPIO_NUM_18;
static constexpr gpio_num_t kI2sWs = GPIO_NUM_19;
static constexpr gpio_num_t kI2sDout = GPIO_NUM_20;
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting SD Audio Player Example...");
    ESP_LOGW(TAG, "Make sure you have a file named 'test.wav' on your SD Card!");
    ESP_LOGW(TAG, "The WAV file must be: 44.1kHz, 16-bit, mono.");

    // 1. Configure and Mount the SD Card
    Espressif::Wrappers::SdCard::Config sd_cfg = {
        .miso = kSdMiso,
        .mosi = kSdMosi,
        .sck = kSdSck,
        .cs = kSdCs,
        .mount_point = "/sdcard", 
        .max_files = 5, 
        .format_if_mount_failed = false
    };

    Espressif::Wrappers::SdCard sd(sd_cfg);

    if (sd.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD Card!");
        return;
    }
    ESP_LOGI(TAG, "SD Card mounted successfully.");

    // 2. Configure and Start the Audio Engine (I2S DMA + Mixer)
    Espressif::Wrappers::Audio::AudioEngine::Config audio_cfg = {
        .bclk_pin = kI2sBclk,
        .ws_pin = kI2sWs,
        .dout_pin = kI2sDout,
        // .sd_mode_pin = GPIO_NUM_NC, // Optional, if your MAX98357A uses the SD_MODE pin to power on/off
        .sample_rate = 44100,
        .max_channels = 2 // We only need 1 or 2 channels for this example
    };

    Espressif::Wrappers::Audio::AudioEngine engine(audio_cfg);

    if (engine.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Audio Engine!");
        return;
    }

    if (engine.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Audio Engine tasks!");
        return;
    }
    ESP_LOGI(TAG, "Audio Engine started. I2S is now streaming.");

    // 3. Play the WAV file in a loop
    ESP_LOGI(TAG, "Attempting to play /sdcard/test.wav...");
    
    // play(file_path, loop_enable, volume_0_to_16384)
    auto channel_id = engine.play("/sdcard/test.wav", true, 16384);

    if (channel_id == Espressif::Wrappers::Audio::INVALID_CHANNEL) {
        ESP_LOGE(TAG, "Failed to play the file. Does /sdcard/test.wav exist?");
        // Cleanup on failure
        (void)sd.deinit();
        return;
    }

    ESP_LOGI(TAG, "Playing! You should hear audio now.");
    ESP_LOGI(TAG, "The file will loop indefinitely.");

    // The main thread just monitors the audio output level (RMS) to print a simple VU meter
    while (true) {
        uint16_t rms_level = engine.getOutputLevel();
        
        // Convert the 0-16384 RMS value to a simple 10-step progress bar
        int bars = (rms_level * 10) / 16384;
        std::string meter(bars, '#');
        std::string spaces(10 - bars, ' ');
        
        ESP_LOGI(TAG, "Audio Level: [%s%s] %d", meter.c_str(), spaces.c_str(), rms_level);
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Update VU meter 10 times per second
    }
}
