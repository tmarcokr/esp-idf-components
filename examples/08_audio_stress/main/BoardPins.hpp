#pragma once

#include "hal/gpio_types.h" // IWYU pragma: keep
#include "sdkconfig.h"

/**
 * @brief Pin map of the stress test board per target (SDMMC 1-bit SD, MAX98357A amplifier).
 *
 * ESP32-S3: DevKitC-1. ESP32: SDMMC slot 1. Targets without an SDMMC host (ESP32-C6) exit
 * before using any pin, so every pin is GPIO_NUM_NC there. See the example README.
 */
namespace BoardPins {

#if CONFIG_IDF_TARGET_ESP32S3

inline constexpr gpio_num_t kSdClk = GPIO_NUM_9;
inline constexpr gpio_num_t kSdCmd = GPIO_NUM_8;
inline constexpr gpio_num_t kSdD0 = GPIO_NUM_7;

inline constexpr gpio_num_t kI2sBclk = GPIO_NUM_11;
inline constexpr gpio_num_t kI2sWs = GPIO_NUM_12;
inline constexpr gpio_num_t kI2sDout = GPIO_NUM_13;

inline constexpr gpio_num_t kAmpSdMode = GPIO_NUM_14;

#elif CONFIG_IDF_TARGET_ESP32

// Warning: fixed by the IOMUX (SDMMC slot 1); the driver ignores the slot pin settings on the ESP32.
inline constexpr gpio_num_t kSdClk = GPIO_NUM_14;
inline constexpr gpio_num_t kSdCmd = GPIO_NUM_15;
inline constexpr gpio_num_t kSdD0 = GPIO_NUM_2;

inline constexpr gpio_num_t kI2sBclk = GPIO_NUM_26;
inline constexpr gpio_num_t kI2sWs = GPIO_NUM_25;
inline constexpr gpio_num_t kI2sDout = GPIO_NUM_22;

inline constexpr gpio_num_t kAmpSdMode = GPIO_NUM_21;

#else

inline constexpr gpio_num_t kSdClk = GPIO_NUM_NC;
inline constexpr gpio_num_t kSdCmd = GPIO_NUM_NC;
inline constexpr gpio_num_t kSdD0 = GPIO_NUM_NC;

inline constexpr gpio_num_t kI2sBclk = GPIO_NUM_NC;
inline constexpr gpio_num_t kI2sWs = GPIO_NUM_NC;
inline constexpr gpio_num_t kI2sDout = GPIO_NUM_NC;

inline constexpr gpio_num_t kAmpSdMode = GPIO_NUM_NC;

#endif

} // namespace BoardPins
