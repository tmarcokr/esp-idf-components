# ESP32 Hardware Drivers Collection (ESP-IDF)

!ESP-IDF Version
!C++ Standard
!License

Welcome to a highly modular collection of hardware drivers for the ESP32 ecosystem (specifically tailored and tested for the ESP32-C6 and ESP32-S3). 

This repository separates complex hardware protocols (I2S, I2C, SPI) from application logic, providing clean, reusable, and thread-safe **C++20 APIs**. Whether you are building a custom lightsaber, an RC car, or an IoT drone, these components are designed to be "Copy & Paste" ready.

---

## 📦 Repository Architecture

The repository is structured following ESP-IDF standard practices:

- **`components/`**: Contains the isolated hardware drivers. Each driver has its own `CMakeLists.txt`, `include/` headers, `.cpp` implementations, and a dedicated `README.md` explaining hardware pinouts and usage.
- **`examples/`**: Contains atomic, single-purpose projects demonstrating how to use each component.

---

## 🛠️ Available Drivers

### 1. Audio Engine (`max98357a`)
A robust I2S driver for the MAX98357A Class-D amplifier.
- Features a strict **Anti-Pop sequence** during power-on.
- Abstracted DMA buffering for skip-free audio.

### 2. Motion Processing (`mpu6050`)
Advanced wrapper for the MPU-6050 IMU.
- Integrates the v6.12 DMP (Digital Motion Processor) blob.
- Real-time gravity-filtered quaternions and linear acceleration.

### 3. SD Card SPI (`sd_card`)
Fast and thread-safe wrapper for standard SD cards using the SPI bus.

---

## 🚀 Getting Started

To integrate any of these drivers into your own ESP-IDF project:
1. Copy the desired folder from `components/` (e.g., `components/max98357a`) into your project's `components/` directory.
2. ESP-IDF's CMake will automatically discover it.
3. Include the header in your code (e.g., `#include "max98357a.hpp"`) and initialize the class.

Check the `examples/` directory for ready-to-run code snippets!
