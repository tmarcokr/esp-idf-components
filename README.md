# ESP32 Hardware Drivers Collection (ESP-IDF)

[![ESP-IDF Version](https://img.shields.io/badge/ESP--IDF-v5.3%2B%20%7C%20v6.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
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
- Sample-aligned grouped starts, lock-free control and real-time counters (`getStats()`).
- Runs on the ESP32, ESP32-S3 and ESP32-C6; PSRAM is optional (without it the ring buffers use internal RAM). See [`components/audio/README.md`](components/audio/README.md).

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
| 08 | `08_audio_stress` | `sd_card` + `audio` | Stress test of the audio lifecycle (linked loops, stop storms, null test); needs SDMMC, so it runs on the ESP32-S3 and ESP32 and exits cleanly on the ESP32-C6. |

Each example is a standalone ESP-IDF project, built and verified in CI with ESP-IDF v6.1 for the ESP32, ESP32-S3 and ESP32-C6 (the components require ESP-IDF v5.3 or newer). On the classic ESP32, only `02`, `06` and `08` have a reviewed pin map; `01`, `03`, `04`, `05` and `07` are only build-checked there (`03` and `04` use GPIO 6–11, the flash pins of most ESP32 modules). The ESP32 defaults of `06` and `08` enable PSRAM with `CONFIG_SPIRAM_IGNORE_NOTFOUND`, so one binary runs with or without PSRAM; while the minimum chip revision stays below 3 (the default), this also enables the PSRAM cache workaround for early ESP32 revisions, which makes the code slightly slower. Build one from its folder with an activated ESP-IDF environment, or open that folder with the ESP-IDF VS Code extension:

```bash
cd examples/07_smart_led
idf.py set-target esp32s3
idf.py build
```

---

## 🧪 Host Tests

`test/host/` holds standalone checks for the ESP-IDF-free headers (DC blocker, compressor, ring geometry, ring memory placement, WAV data size). `test/host/run_host_tests.sh` builds and runs every test with GCC 13+ and the address and undefined-behaviour sanitizers; CI runs it in the "Host tests" job. Without a host compiler, use a container from the repository root:

```bash
podman run --rm -v "$PWD":/src:Z -w /src docker.io/library/gcc:13 bash test/host/run_host_tests.sh
```

See [`test/host/README.md`](test/host/README.md).

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
