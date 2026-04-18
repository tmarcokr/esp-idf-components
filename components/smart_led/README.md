# SmartLed Component

High-level, effect-driven controller for WS2812B (NeoPixel) LED strips using the ESP-IDF RMT peripheral.

## Architecture

| Layer | Class | Purpose |
|---|---|---|
| Types | `Color`, `hsvToRgb()`, `blend()` | Core color primitives with integer-only math |
| Driver | `LedStrip` | RAII RMT hardware wrapper (usable standalone) |
| Buffer | `Canvas` | In-memory pixel buffer for effect rendering |
| Interface | `IEffect` | Abstract base for composable animations |
| Scheduler | `Engine` | FreeRTOS render task with brightness control |
| Effects | `SolidColor`, `Breathe`, `RainbowCycle`, `Chase`, `Flash` | Ready-to-use built-in effects |

## File Structure

```
components/smart_led/
├── CMakeLists.txt
├── idf_component.yml
├── LedStrip.cpp
├── Canvas.cpp
├── Engine.cpp
├── effects/
│   ├── SolidColor.cpp
│   ├── Breathe.cpp
│   ├── RainbowCycle.cpp
│   ├── Chase.cpp
│   └── Flash.cpp
└── include/
    ├── SmartLed.hpp            ← umbrella header (includes everything)
    ├── SmartLedTypes.hpp       ← Color, hsvToRgb(), blend()
    ├── LedStrip.hpp
    ├── Canvas.hpp
    ├── IEffect.hpp
    ├── Engine.hpp
    ├── Effects.hpp             ← umbrella for all built-in effects
    └── effects/
        ├── SolidColor.hpp
        ├── Breathe.hpp
        ├── RainbowCycle.hpp
        ├── Chase.hpp
        └── Flash.hpp
```

## Quick Start

```cpp
#include "SmartLed.hpp"

using namespace Espressif::Wrappers::SmartLed;

Engine engine(GPIO_NUM_4, 5);  // 5 LEDs on GPIO 4
ESP_ERROR_CHECK(engine.init());

engine.setGlobalBrightness(180);
engine.setBaseEffect(std::make_unique<RainbowCycle>(3000));
engine.start();  // Runs in background FreeRTOS task
```

## Built-in Effects

| Effect | Description | Constructor |
|---|---|---|
| `SolidColor` | Static single color | `SolidColor(Color color)` |
| `Breathe` | Pulsating brightness on a fixed hue | `Breathe(uint16_t hue, uint32_t period_ms)` |
| `RainbowCycle` | Rotating rainbow across the strip | `RainbowCycle(uint32_t cycle_ms)` |
| `Chase` | A lit pixel with fading tail | `Chase(Color color, uint8_t tail_length, uint32_t step_ms)` |
| `Flash` | Blink N times then auto-remove (overlay) | `Flash(Color color, uint32_t on_ms, uint32_t off_ms, uint8_t count)` |

## Overlays

Temporary effects that composite on top of the base and auto-remove when finished:

```cpp
// Flash white 3 times on top of whatever base effect is running
engine.pushOverlay(std::make_unique<Flash>(Color::White(), 100, 100, 3));
```

Up to 5 overlays can be active simultaneously (`Engine::MAX_OVERLAYS`).

## Creating Custom Effects

Implement the `IEffect` interface. Each effect needs three methods:

### 1. Create the header: `include/effects/MyEffect.hpp`

```cpp
#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "SmartLedTypes.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

class MyEffect : public IEffect {
public:
    explicit MyEffect(uint32_t speed_ms);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    uint32_t _speed_ms;
    uint32_t _elapsed;
};

} // namespace Espressif::Wrappers::SmartLed
```

### 2. Create the implementation: `effects/MyEffect.cpp`

```cpp
#include "effects/MyEffect.hpp"

namespace Espressif::Wrappers::SmartLed {

MyEffect::MyEffect(uint32_t speed_ms) : _speed_ms(speed_ms), _elapsed(0) {}

void MyEffect::update(uint32_t delta_ms) {
    _elapsed += delta_ms;
    // Advance your effect's internal state here.
    // delta_ms is the time since the last frame (typically ~16ms at 60 FPS).
}

void MyEffect::render(Canvas& canvas) {
    uint16_t n = canvas.size();
    // Draw into the canvas using:
    //   canvas.setPixel(index, color)  — set one pixel
    //   canvas.fill(color)             — fill all pixels
    //   canvas.fillRange(start, count, color)
    //   canvas.gradient(start, count, colorA, colorB)
    //   canvas.blendPixel(index, color, alpha)  — alpha-blend with existing
}

bool MyEffect::isFinished() const {
    // Return false for continuous effects (base).
    // Return true when done for self-removing effects (overlays).
    return false;
}

} // namespace Espressif::Wrappers::SmartLed
```

### 3. Register the source file in `CMakeLists.txt`

Add `"effects/MyEffect.cpp"` to the `SRCS` list.

### 4. Add to the umbrella header `include/Effects.hpp`

```cpp
#include "effects/MyEffect.hpp"
```

### 5. Use it

```cpp
engine.setBaseEffect(std::make_unique<MyEffect>(100));
```

## Global Brightness

Brightness is a master dimmer applied at the hardware write stage. Effects always work in full 0–255 color space:

```cpp
engine.setGlobalBrightness(128);  // 50% brightness — all effects dimmed uniformly
engine.setGlobalBrightness(255);  // Full brightness (default)
```

## Thread Safety

`setBaseEffect()`, `pushOverlay()`, and `setGlobalBrightness()` are safe to call from any task while the engine is running.

## Supported Targets

ESP32, ESP32-S3, ESP32-C6 (and any target with RMT peripheral support).
