#include "GpioButton.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON_EXAMPLE";

extern "C" void app_main(void) {
    // Select BOOT button GPIO based on target chip:
    // ESP32-C6 DevKit uses GPIO 9 for the BOOT button.
    // ESP32 and ESP32-S3 DevKits typically use GPIO 0 for the BOOT button.
#if CONFIG_IDF_TARGET_ESP32C6
    constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_9;
#else
    constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_0;
#endif

    ESP_LOGI(TAG, "Starting GPIO Button Example on GPIO %d...", BUTTON_GPIO);

    // Configure the button
    // With `true`, we activate the internal Pull-up resistor (Button is pulled to GND when pressed)
    Espressif::Wrappers::GpioButton btn(BUTTON_GPIO, true);

    // Bind actions to button events using lambda expressions
    btn.onEvent(Espressif::Wrappers::ButtonEvent::PressDown, []() {
        ESP_LOGI(TAG, "=> Event: Press Down!");
    });

    btn.onEvent(Espressif::Wrappers::ButtonEvent::PressUp, []() {
        ESP_LOGI(TAG, "=> Event: Press Up!");
    });

    btn.onEvent(Espressif::Wrappers::ButtonEvent::Click, []() {
        ESP_LOGI(TAG, "=> Event: Click (Short press)!");
    });

    // Bind a 1-second long press
    btn.onLongPress(1000, []() {
        ESP_LOGW(TAG, "=> Event: Long Press (1 second detected)!");
    });

    // Bind another extra-long press of 3 seconds
    btn.onLongPress(3000, []() {
        ESP_LOGE(TAG, "=> Event: Extra Long Press (3 seconds detected)!");
    });

    // Initialize hardware and start the background button polling task
    if (btn.init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize button!");
        return;
    }

    ESP_LOGI(TAG, "Button initialized. Waiting for background events...");

    // The main thread doesn't have to do anything, the GpioButton class handles polling in a separate task
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
