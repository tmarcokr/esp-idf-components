#pragma once

#include "hal/gpio_types.h" // IWYU pragma: keep

/**
 * @brief Pin map of the stress test board (ESP32-S3 DevKitC-1, SDMMC 1-bit SD, MAX98357A amplifier).
 */
namespace BoardPins {

inline constexpr gpio_num_t kSdClk = GPIO_NUM_9;
inline constexpr gpio_num_t kSdCmd = GPIO_NUM_8;
inline constexpr gpio_num_t kSdD0 = GPIO_NUM_7;

inline constexpr gpio_num_t kI2sBclk = GPIO_NUM_11;
inline constexpr gpio_num_t kI2sWs = GPIO_NUM_12;
inline constexpr gpio_num_t kI2sDout = GPIO_NUM_13;

inline constexpr gpio_num_t kAmpSdMode = GPIO_NUM_14;

} // namespace BoardPins
