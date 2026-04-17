#pragma once

#include "esp_err.h"
#include "led_strip.h" // IWYU pragma: keep
#include <cstdint>

namespace Espressif::Wrappers {

/**
 * @brief Simple RGB color representation (8-bit per channel).
 */
struct Color {
  uint8_t r; ///< Red component (0–255).
  uint8_t g; ///< Green component (0–255).
  uint8_t b; ///< Blue component (0–255).
};

/**
 * @brief RAII wrapper for a WS2812 RGB LED strip using the RMT peripheral.
 */
class RgbLed {
public:
  /**
   * @brief Construct a new RGB LED controller.
   * @param gpio_num Associated GPIO pin.
   * @param max_leds Number of LEDs in the chain.
   */
  explicit RgbLed(gpio_num_t gpio_num, uint32_t max_leds = 1);

  /**
   * @brief Destroy the controller and release RMT resources.
   */
  ~RgbLed();

  RgbLed(const RgbLed &) = delete;
  RgbLed &operator=(const RgbLed &) = delete;

  /**
   * @brief Initialize the RMT peripheral and the led_strip driver.
   * @return esp_err_t ESP_OK on success.
   */
  [[nodiscard]] esp_err_t init();

  /**
   * @brief Set all LEDs in the strip to the specified color.
   * @param color The target RGB color.
   * @return esp_err_t ESP_OK on success.
   */
  [[nodiscard]] esp_err_t setColor(const Color &color);

  /**
   * @brief Turn off all LEDs (set to black).
   * @return esp_err_t ESP_OK on success.
   */
  [[nodiscard]] esp_err_t clear();

  /**
   * @brief Perform a blocking smooth color transition over time.
   * @param target Final color.
   * @param duration_ms Total duration of the transition.
   * @param steps Number of interpolation steps.
   */
  void fadeTo(const Color &target, uint32_t duration_ms, uint32_t steps = 50);

  /**
   * @brief Linear interpolation between two 8-bit values.
   */
  static uint8_t interpolate(uint8_t start, uint8_t end, uint32_t step,
                             uint32_t total_steps);

private:
  gpio_num_t _gpio_num;
  uint32_t _max_leds;
  led_strip_handle_t _handle;
  Color _current_color;
  bool _initialized;
};

} // namespace Espressif::Wrappers
