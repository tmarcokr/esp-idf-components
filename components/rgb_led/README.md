# RGB LED Component

Component for controlling directional RGB LED strips (like WS2812, commonly known as NeoPixels) using the ESP32 RMT peripheral via the ESP-IDF `led_strip` component.

## Features
- **Friendly API:** Simple methods to set color (`setColor`), brightness, and turn on/off.
- **Standardized Colors:** Color structure with Red, Green, and Blue channels.
- **Automatic RMT Management:** Abstracts the complexity of bit transmission.

## Basic Usage

```cpp
#include "RgbLed.hpp"
#include "driver/gpio.h"

// Configure RGB LED on GPIO pin 8
Espressif::Wrappers::RgbLed led(GPIO_NUM_8);

if (led.init() == ESP_OK) {
    // Set color to Red at 100%
    led.setColor({255, 0, 0});
    led.show();
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Turn off
    led.clear();
}
```
