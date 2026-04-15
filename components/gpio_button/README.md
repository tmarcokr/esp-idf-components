# GPIO Button Component

This component provides an object-oriented C++ wrapper for reading buttons connected to GPIO pins in ESP-IDF.

## Features
- **Software Debouncing & Background Task:** The driver automatically creates a FreeRTOS task (`pollTask`) that debounces electrical readings.
- **Event-Driven API:** Modern architecture based on callbacks (lambda functions). Bind your logic directly to events instead of using cumbersome `if/else` conditionals in the main thread.
- **Dynamic Long Presses:** Supports binding multiple callbacks to different millisecond durations without blocking the CPU.

## Basic Usage

```cpp
#include "GpioButton.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

// Configure button on GPIO 9 (Internal pull-up, active LOW)
Espressif::Wrappers::GpioButton btn(GPIO_NUM_9, true); 

btn.onEvent(Espressif::Wrappers::ButtonEvent::Click, []() {
    ESP_LOGI("APP", "The button was clicked!");
});

btn.onLongPress(2000, []() {
    ESP_LOGW("APP", "The button was held for 2 seconds!");
});

// Start the background task. The main thread no longer needs to update the state.
if (btn.init() == ESP_OK) {
    // The application can continue doing other things
}
```
