#pragma once

#include "SmartLedTypes.hpp"
#include "esp_err.h"
#include "led_strip.h" // IWYU pragma: keep
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief RAII wrapper for a WS2812 LED strip using the RMT peripheral.
 *
 * Standalone driver that can be used independently or as part of the Engine.
 */
class LedStrip {
public:
    /**
     * @brief Construct a new LED strip controller.
     * @param gpio Associated GPIO pin.
     * @param num_leds Number of LEDs in the chain.
     */
    explicit LedStrip(gpio_num_t gpio, uint16_t num_leds);

    ~LedStrip();

    LedStrip(const LedStrip&) = delete;
    LedStrip& operator=(const LedStrip&) = delete;

    /**
     * @brief Initialize the RMT peripheral and led_strip driver.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t init();

    [[nodiscard]] esp_err_t setPixel(uint16_t index, Color color);
    [[nodiscard]] esp_err_t fill(Color color);
    [[nodiscard]] esp_err_t clear();
    [[nodiscard]] esp_err_t refresh();

    [[nodiscard]] uint16_t numLeds() const;

private:
    gpio_num_t _gpio;
    uint16_t _num_leds;
    led_strip_handle_t _handle;
    bool _initialized;
};

} // namespace Espressif::Wrappers::SmartLed
