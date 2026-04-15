#include "RgbLed.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include <cinttypes>

namespace Espressif::Wrappers {

static constexpr const char* TAG = "RgbLedWrapper";

RgbLed::RgbLed(gpio_num_t gpio_num, uint32_t max_leds)
    : _gpio_num(gpio_num), 
      _max_leds(max_leds), 
      _handle(nullptr), 
      _current_color({0, 0, 0}), 
      _initialized(false) {}

RgbLed::~RgbLed() {
    if (_initialized && _handle) {
        (void)clear();
        led_strip_del(_handle);
        ESP_LOGI(TAG, "LED hardware resources freed.");
    }
}

esp_err_t RgbLed::init() {
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d...", _gpio_num);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = _gpio_num;
    strip_config.max_leds = _max_leds;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.flags.with_dma = false;

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &_handle);
    
    if (err == ESP_OK) {
        _initialized = true;
        (void)clear();
        ESP_LOGI(TAG, "RMT LED driver initialized successfully.");
    } else {
        ESP_LOGE(TAG, "Error initializing peripheral: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t RgbLed::setColor(const Color& color) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t err = ESP_OK;
    for (uint32_t i = 0; i < _max_leds; i++) {
        err = led_strip_set_pixel(_handle, i, color.r, color.g, color.b);
        if (err != ESP_OK) break;
    }
    
    if (err == ESP_OK) {
        err = led_strip_refresh(_handle);
        if (err == ESP_OK) {
            _current_color = color;
        }
    }
    return err;
}

esp_err_t RgbLed::clear() {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_clear(_handle);
    if (err == ESP_OK) {
        _current_color = {0, 0, 0};
    }
    return err;
}

void RgbLed::fadeTo(const Color& target, uint32_t duration_ms, uint32_t steps) {
    if (!_initialized || steps == 0) return;

    const uint32_t delay_per_step_ms = duration_ms / steps;
    const Color start = _current_color;

    for (uint32_t i = 1; i <= steps; ++i) {
        Color next_color = {
            interpolate(start.r, target.r, i, steps),
            interpolate(start.g, target.g, i, steps),
            interpolate(start.b, target.b, i, steps)
        };

       esp_err_t err = setColor(next_color);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Error in fade step %" PRIu32 ": %s", i, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(delay_per_step_ms));
    }
}

uint8_t RgbLed::interpolate(uint8_t start, uint8_t end, uint32_t step, uint32_t total_steps) {
    const int32_t diff = static_cast<int32_t>(end) - static_cast<int32_t>(start);
    return static_cast<uint8_t>(start + ((diff * static_cast<int32_t>(step)) / static_cast<int32_t>(total_steps)));
}

} // namespace Espressif::Wrappers
