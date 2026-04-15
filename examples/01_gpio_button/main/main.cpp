#include "GpioButton.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON_EXAMPLE";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GPIO Button Example...");

    // Configure the button on GPIO 9 (Typically the BOOT button on modern ESP32s)
    // With `true`, we activate the internal Pull-up resistor (Button is pulled to GND when pressed)
    Espressif::Wrappers::GpioButton btn(GPIO_NUM_9, true);

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
