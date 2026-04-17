#include "LedStrip.hpp"
#include "esp_log.h"

namespace Espressif::Wrappers::SmartLed {

static constexpr const char* TAG = "SmartLed::LedStrip";

LedStrip::LedStrip(gpio_num_t gpio, uint16_t num_leds)
    : _gpio(gpio), _num_leds(num_leds), _handle(nullptr), _initialized(false) {}

LedStrip::~LedStrip() {
    if (_initialized && _handle) {
        led_strip_clear(_handle);
        led_strip_del(_handle);
    }
}

esp_err_t LedStrip::init() {
    led_strip_config_t config = {};
    config.strip_gpio_num = _gpio;
    config.max_leds = _num_leds;
    config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    config.led_model = LED_MODEL_WS2812;
    config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10 MHz
    rmt_config.flags.with_dma = false;

    esp_err_t err = led_strip_new_rmt_device(&config, &rmt_config, &_handle);
    if (err == ESP_OK) {
        _initialized = true;
        led_strip_clear(_handle);
        ESP_LOGI(TAG, "LedStrip initialized: GPIO %d, %u LEDs", _gpio, _num_leds);
    } else {
        ESP_LOGE(TAG, "LedStrip init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t LedStrip::setPixel(uint16_t index, Color color) {
    if (!_initialized || index >= _num_leds) return ESP_ERR_INVALID_STATE;
    return led_strip_set_pixel(_handle, index, color.r, color.g, color.b);
}

esp_err_t LedStrip::fill(Color color) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    for (uint16_t i = 0; i < _num_leds; ++i) {
        esp_err_t err = led_strip_set_pixel(_handle, i, color.r, color.g, color.b);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t LedStrip::clear() {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(_handle);
}

esp_err_t LedStrip::refresh() {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    return led_strip_refresh(_handle);
}

uint16_t LedStrip::numLeds() const { return _num_leds; }

} // namespace Espressif::Wrappers::SmartLed
