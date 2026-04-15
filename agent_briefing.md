# ESP32 Hardware Drivers - Agent Briefing

## 🎯 Objective
Transform this repository into a highly professional, reusable, and modular collection of hardware drivers for the ESP32 (ESP-IDF v5.x ecosystem) using modern C++20.

The goal is to provide isolated, "Copy & Paste" ready components for standard hardware modules (Sensors, SD Cards, Audio Amplifiers) that can be easily integrated into any IoT, robotics, or embedded project.

## 📐 Architecture Rules
The repository must strictly follow this structure:
1. **`components/<driver_name>/`**: Contains the driver logic. Must include its own `CMakeLists.txt`, `include/*.hpp`, and `*.cpp` files. No application logic here, only hardware abstraction.
2. **`examples/<example_name>/`**: Atomic, independent ESP-IDF projects. Each example must have its own `CMakeLists.txt` and `main/main.cpp`. An example must demonstrate exactly **one** feature of a component simply and clearly.

## ✅ Current State
- The root `CMakeLists.txt` is already configured to discover the `components/` folder.
- The root `README.md` is already structured professionally.
- The **`max98357a`** (I2S Audio Amplifier) component is successfully extracted and completed.
- The **`02_audio_tone`** example is created and demonstrates the audio component.
- The **`mpu6050`** component is partially created (basic I2C setup and raw reads are implemented).

## 🚀 Pending Tasks for the Agent
Please act autonomously to complete the following tasks:

### Task 1: Complete MPU-6050 Component
- Update the `components/mpu6050` module to include the loading of the DMP (Digital Motion Processor) firmware.
- The binary blob is available in `components/mpu6050/mpu6050_dmp_firmware.h`.
- Expose a clean API method to initialize the DMP and read gravity-filtered data.

### Task 2: Create MPU-6050 Example
- Create an independent ESP-IDF project at `examples/03_mpu6050_dmp`.
- The `main.cpp` should initialize the `Mpu6050` class and periodically print the filtered acceleration and gyroscope data to the console using `ESP_LOGI`.

### Task 3: Extract SD Card Component
- Extract the existing SD Card SPI logic (currently entangled in the legacy `main/` app) into `components/sd_card`.
- Create `components/sd_card/CMakeLists.txt`, `include/sd_card.hpp`, and `sd_card.cpp`.
- Provide a simple, RAII-compliant C++ class to mount the SD card and handle basic file I/O over the SPI bus.

### Task 4: Create SD Card Example
- Create an independent ESP-IDF project at `examples/04_sd_card_basic`.
- The `main.cpp` should mount the SD card, write a "Hello World" text file, read it back, print the content to the console, and safely unmount.

### Task 5: Clean Up Legacy Architecture
- Delete the old `main.cpp` and legacy `main/` directory from the root of the repository. The root should no longer be a runnable ESP-IDF project, but rather a library container. 

---
**Note to Agent:** Write clean, documented C++20 code. Use strong typing, avoid raw pointers where possible, and handle ESP-IDF error codes (`esp_err_t`) gracefully. Proceed with generating the code for these pending tasks.