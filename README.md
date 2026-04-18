# ESP32 Hardware Drivers Collection (ESP-IDF)

[![ESP-IDF Version](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-orange)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Welcome to a highly modular collection of hardware drivers for the ESP32 ecosystem (specifically tailored and tested for the ESP32-C6 and ESP32-S3). 

This repository separates complex hardware protocols (I2S, I2C, SPI, RMT) from application logic, providing clean, reusable, and thread-safe **C++20 APIs**. Whether you are building a custom lightsaber, an RC car, or an IoT drone, these components are designed to be "Copy & Paste" ready.

---

## 📦 Repository Architecture

The repository is structured following ESP-IDF standard practices:

- **`components/`**: Contains the isolated hardware drivers. Each driver has its own `CMakeLists.txt`, `include/` headers, and implementation files.
- **`examples/`**: Contains atomic, single-purpose projects demonstrating how to use each component.

---

## 🛠️ Available Drivers

### 1. Audio Engine (`audio_engine`)
A sophisticated I2S-based polyphonic audio engine.
- Supports multiple `AudioChannel` instances for simultaneous playback.
- Integrated `I2sTransmitter` for MAX98357A and similar Class-D amplifiers.
- Features anti-pop sequences and DMA-backed skip-free audio.

### 2. Motion Processing (`mpu6050`)
Advanced wrapper for the MPU-6050 IMU.
- Integrates the Digital Motion Processor (DMP) for high-accuracy fused data.
- Real-time gravity-filtered quaternions and linear acceleration.

### 3. SD Card SPI (`sd_card`)
RAII-compliant wrapper for standard SD cards using the SPI bus.
- Simple mounting/unmounting logic.
- Standard POSIX file I/O integration.

### 4. Input & Visuals (`gpio_button` & `rgb_led`)
- **`gpio_button`**: Debounced interrupt-driven button handling.
- **`rgb_led`**: RMT-based driver for WS2812 (NeoPixel) and similar addressable LEDs.

### 5. Smart LED Strip Engine (`smart_led`)
Effect-driven WS2812B strip controller with a non-blocking FreeRTOS render engine.
- Composable effect system with base + overlay layering via the `IEffect` interface.
- Built-in effects: `SolidColor`, `Breathe`, `RainbowCycle`, `Chase`, `Flash`.
- Global brightness control applied at hardware write stage.
- Thread-safe runtime effect swapping.

---

## 🚀 Ready-to-Run Examples

| # | Example Name | Target Driver | Key Feature |
|---|--------------|---------------|-------------|
| 01 | `01_gpio_button` | `gpio_button` | Interrupt-based input handling. |
| 02 | `02_audio_tone` | `audio_engine` | Sine wave generation and I2S output. |
| 03 | `03_mpu6050_dmp` | `mpu6050` | Fused 6-axis motion data via DMP. |
| 04 | `04_sd_card_basic` | `sd_card` | Text file read/write on SPI SD cards. |
| 05 | `05_rgb_led` | `rgb_led` | WS2812 control via RMT peripheral. |
| 06 | `06_sd_audio_player` | `sd_card` + `audio` | Streaming raw audio from SD to I2S. |
| 07 | `07_smart_led` | `smart_led` | Effect engine demo with 5 WS2812B LEDs. |

---

## 🔧 Integration Guide

To integrate any of these drivers into your own ESP-IDF project:
1. Copy the desired folder from `components/` into your project's `components/` directory.
2. ESP-IDF's CMake will automatically discover it.
3. Include the header in your code and initialize the class.

```cpp
// Example: Using the RgbLed component
#include "RgbLed.hpp"

RgbLed led(GPIO_NUM_8, 1); // Pin 8, 1 LED
led.setColor(255, 0, 0);   // Solid Red
```
