#include "RgbLed.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RGB_LED_EXAMPLE";

extern "C" void app_main(void) {
    // Select GPIO based on target chip:
#if CONFIG_IDF_TARGET_ESP32S3
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_48; // ESP32-S3-DevKitC-1
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_8;  // ESP32-C3/C6/H2 DevKits
#elif CONFIG_IDF_TARGET_ESP32S2
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_18; // ESP32-S2-Saola
#else
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_2;  // Default fallback for classic ESP32
#endif

    ESP_LOGI(TAG, "Starting RGB LED Example on GPIO %d...", LED_GPIO);

    // Configure an RGB LED
    Espressif::Wrappers::RgbLed led(LED_GPIO, 1);

    if (led.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RGB LED!");
        return;
    }

    ESP_LOGI(TAG, "RGB LED initialized. Cycling colors...");

    while (true) {
        ESP_LOGI(TAG, "Color: RED");
        (void)led.setColor({255, 0, 0});
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Color: GREEN");
        (void)led.setColor({0, 255, 0});
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Color: BLUE");
        (void)led.setColor({0, 0, 255});
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Fading smoothly to PURPLE over 2 seconds...");
        led.fadeTo({128, 0, 128}, 2000, 50); // 50 steps
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Turning OFF");
        (void)led.clear();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
