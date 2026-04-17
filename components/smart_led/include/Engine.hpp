#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "LedStrip.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Non-blocking render engine that drives LED effects in a dedicated FreeRTOS task.
 *
 * Owns a LedStrip and a Canvas internally. Composites one base effect
 * with up to MAX_OVERLAYS overlay effects per frame, applies global
 * brightness, and writes the result to hardware.
 */
class Engine {
public:
    static constexpr size_t MAX_OVERLAYS = 5;

    /**
     * @brief Construct a render engine.
     * @param gpio GPIO pin connected to the LED strip data line.
     * @param num_leds Number of LEDs in the strip.
     */
    Engine(gpio_num_t gpio, uint16_t num_leds);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /** @brief Initialize the RMT hardware and internal resources. */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Set the master brightness applied at hardware write time.
     * @param brightness 0 = off, 255 = full intensity.
     */
    void setGlobalBrightness(uint8_t brightness);
    [[nodiscard]] uint8_t globalBrightness() const;

    /** @brief Replace the continuously-running base effect (thread-safe). */
    void setBaseEffect(std::unique_ptr<IEffect> effect);

    /** @brief Add a temporary overlay effect (thread-safe). Returns false if all slots are full. */
    bool pushOverlay(std::unique_ptr<IEffect> effect);

    /** @brief Set the target render frame rate. Default: 60 FPS. */
    void setTargetFps(uint32_t fps);

    /** @brief Start the render task. */
    void start(UBaseType_t priority = 5, uint32_t stack_size = 4096);

    /** @brief Stop the render task and clear the strip. Blocks until the task exits. */
    void stop();

    [[nodiscard]] uint16_t numLeds() const;

private:
    static void taskEntry(void* arg);
    void renderLoop();

    LedStrip _strip;
    Canvas _canvas;

    std::unique_ptr<IEffect> _base_effect;
    std::array<std::unique_ptr<IEffect>, MAX_OVERLAYS> _overlays;

    std::atomic<uint8_t> _brightness;
    uint32_t _frame_delay_ms;

    TaskHandle_t _task_handle;
    SemaphoreHandle_t _mutex;
    std::atomic<bool> _running;
};

} // namespace Espressif::Wrappers::SmartLed
