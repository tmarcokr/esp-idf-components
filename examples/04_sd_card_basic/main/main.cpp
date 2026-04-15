#include "sd_card.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string>

static const char *TAG = "SD_EXAMPLE";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Basic SD Card Example...");

    Espressif::Wrappers::SdCard::Config sd_cfg = {
        .miso = GPIO_NUM_4, 
        .mosi = GPIO_NUM_11, 
        .sck = GPIO_NUM_7, 
        .cs = GPIO_NUM_10,
        .mount_point = "/sdcard", 
        .max_files = 5, 
        .format_if_mount_failed = false
    };

    Espressif::Wrappers::SdCard sd(sd_cfg);

    if (sd.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD Card!");
        return;
    }
    
    sd.printCardInfo();

    const std::string test_file = "/hello.txt";
    const std::string test_data = "Hello World! This is a test file from the ESP32.";

    ESP_LOGI(TAG, "Writing to %s...", test_file.c_str());
    if (sd.writeFile(test_file, test_data) == ESP_OK) {
        ESP_LOGI(TAG, "File written successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to write file.");
    }

    ESP_LOGI(TAG, "Reading back from %s...", test_file.c_str());
    std::string read_data;
    if (sd.readFile(test_file, read_data) == ESP_OK) {
        ESP_LOGI(TAG, "File read successfully. Content:");
        ESP_LOGI(TAG, "%s", read_data.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to read file.");
    }

    ESP_LOGI(TAG, "Unmounting SD Card...");
    if (sd.deinit() == ESP_OK) {
        ESP_LOGI(TAG, "SD Card unmounted successfully.");
    }

    ESP_LOGI(TAG, "Example completed.");
}
