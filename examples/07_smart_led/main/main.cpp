#include "SmartLed.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char* TAG = "SmartLedExample";

using namespace Espressif::Wrappers::SmartLed;

constexpr uint16_t NUM_LEDS = 5;

extern "C" void app_main(void) {
#if CONFIG_IDF_TARGET_ESP32S3
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_48;
#elif CONFIG_IDF_TARGET_ESP32C6
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_4;
#else
    constexpr gpio_num_t LED_GPIO = GPIO_NUM_18;
#endif

    ESP_LOGI(TAG, "SmartLed Example — GPIO %d, %u LEDs", LED_GPIO, NUM_LEDS);

    Engine engine(LED_GPIO, NUM_LEDS);
    ESP_ERROR_CHECK(engine.init());

    engine.setGlobalBrightness(200);
    engine.start();

    while (true) {
        // ── Phase 1: Solid Red ──────────────────────────────────────────
        ESP_LOGI(TAG, "Phase 1: Solid Red");
        engine.setBaseEffect(std::make_unique<SolidColor>(Color::Red()));
        vTaskDelay(pdMS_TO_TICKS(3000));

        // ── Phase 2: Breathe Blue ───────────────────────────────────────
        ESP_LOGI(TAG, "Phase 2: Breathe (Blue, 2s period)");
        engine.setBaseEffect(std::make_unique<Breathe>(240, 2000));
        vTaskDelay(pdMS_TO_TICKS(5000));

        // ── Phase 3: Rainbow Cycle ──────────────────────────────────────
        ESP_LOGI(TAG, "Phase 3: Rainbow Cycle (3s rotation)");
        engine.setBaseEffect(std::make_unique<RainbowCycle>(3000));
        vTaskDelay(pdMS_TO_TICKS(6000));

        // ── Phase 4: Chase Green ────────────────────────────────────────
        ESP_LOGI(TAG, "Phase 4: Chase (Green, tail=3, 150ms/step)");
        engine.setBaseEffect(std::make_unique<Chase>(Color::Green(), 3, 150));
        vTaskDelay(pdMS_TO_TICKS(4000));

        // ── Phase 5: Flash Overlay on top of Chase ──────────────────────
        ESP_LOGI(TAG, "Phase 5: White Flash overlay (3 blinks)");
        engine.pushOverlay(std::make_unique<Flash>(Color::White(), 100, 100, 3));
        vTaskDelay(pdMS_TO_TICKS(2000));

        // ── Phase 6: Brightness ramp down then up ───────────────────────
        ESP_LOGI(TAG, "Phase 6: Brightness ramp — cycling brightness");
        engine.setBaseEffect(std::make_unique<RainbowCycle>(2000));

        for (int b = 200; b >= 10; b -= 5) {
            engine.setGlobalBrightness(static_cast<uint8_t>(b));
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        for (int b = 10; b <= 200; b += 5) {
            engine.setGlobalBrightness(static_cast<uint8_t>(b));
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        engine.setGlobalBrightness(200);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // ── Phase 7: Orange Chase + Cyan Flash ──────────────────────────
        ESP_LOGI(TAG, "Phase 7: Orange Chase + Cyan Flash overlay");
        engine.setBaseEffect(std::make_unique<Chase>(Color::Orange(), 2, 100));
        vTaskDelay(pdMS_TO_TICKS(2000));
        engine.pushOverlay(std::make_unique<Flash>(Color::Cyan(), 80, 120, 5));
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "── Cycle complete, restarting ──");
    }
}
