# MPU-6050 Sensor Component

Professional I2C driver for the MPU-6050 accelerometer and gyroscope, including full support for the built-in DMP (Digital Motion Processor).

## Features
- **Advanced Hardware Management:** Properly initializes and loads the binary firmware (`v6.12`) into the sensor.
- **CPU Offloading:** Allows the MPU-6050 to calculate Quaternions internally, offloading intensive mathematical calculations from the ESP32 CPU.
- **Ready-to-use 3D Physics:** Provides quaternions, Euler angles (Pitch, Roll, Yaw), the gravity vector, and linear acceleration (without gravity) in a clean, strongly-typed manner.

## Basic Usage

```cpp
#include "Mpu6050.hpp"

// Configure MPU-6050 with SDA, SCL and INT pins
Espressif::Wrappers::Sensors::Mpu6050 mpu(GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_5);

if (mpu.initialize() == ESP_OK) {
    while (true) {
        // Read packet from the DMP FIFO
        auto data = mpu.readData();
        
        if (data.has_value()) {
            auto accel = data->getLinearAcceleration();
            auto euler = data->getEulerAngles();
            
            // Process the data (Pure linear acceleration, etc.)
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```
