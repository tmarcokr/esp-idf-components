#include "RgbLed.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RGB_LED_EXAMPLE";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting RGB LED Example...");

    // Configure an RGB LED on GPIO 8 (Typical pin for the built-in WS2812 on chips like ESP32-C3/C6)
    Espressif::Wrappers::RgbLed led(GPIO_NUM_8, 1);

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
