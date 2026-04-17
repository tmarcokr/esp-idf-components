#include "Engine.hpp"
#include "esp_timer.h"

namespace Espressif::Wrappers::SmartLed {

Engine::Engine(gpio_num_t gpio, uint16_t num_leds)
    : _strip(gpio, num_leds), _canvas(num_leds),
      _brightness(255), _frame_delay_ms(1000 / 60),
      _task_handle(nullptr), _mutex(nullptr), _running(false) {}

Engine::~Engine() {
    stop();
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

esp_err_t Engine::init() {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) return ESP_ERR_NO_MEM;
    return _strip.init();
}

void Engine::setGlobalBrightness(uint8_t brightness) {
    _brightness.store(brightness);
}

uint8_t Engine::globalBrightness() const {
    return _brightness.load();
}

void Engine::setBaseEffect(std::unique_ptr<IEffect> effect) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _base_effect = std::move(effect);
    xSemaphoreGive(_mutex);
}

bool Engine::pushOverlay(std::unique_ptr<IEffect> effect) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool placed = false;
    for (auto& slot : _overlays) {
        if (!slot) {
            slot = std::move(effect);
            placed = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    return placed;
}

void Engine::setTargetFps(uint32_t fps) {
    _frame_delay_ms = (fps > 0) ? (1000 / fps) : 16;
}

void Engine::start(UBaseType_t priority, uint32_t stack_size) {
    if (_task_handle) return;
    _running.store(true);
    xTaskCreate(taskEntry, "SmartLedTask", stack_size, this, priority, &_task_handle);
}

void Engine::stop() {
    if (!_task_handle) return;
    _running.store(false);
    while (_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

uint16_t Engine::numLeds() const {
    return _canvas.size();
}

void Engine::taskEntry(void* arg) {
    static_cast<Engine*>(arg)->renderLoop();
}

void Engine::renderLoop() {
    TickType_t last_wake = xTaskGetTickCount();
    uint64_t last_us = esp_timer_get_time();

    while (_running.load()) {
        uint64_t now_us = esp_timer_get_time();
        uint32_t delta_ms = static_cast<uint32_t>((now_us - last_us) / 1000);
        last_us = now_us;

        // Prevent zero-delta on very fast iterations
        if (delta_ms == 0) delta_ms = 1;

        xSemaphoreTake(_mutex, portMAX_DELAY);

        _canvas.clear();

        if (_base_effect) {
            _base_effect->update(delta_ms);
            _base_effect->render(_canvas);
        }

        for (auto& overlay : _overlays) {
            if (overlay) {
                overlay->update(delta_ms);
                overlay->render(_canvas);
                if (overlay->isFinished()) {
                    overlay.reset();
                }
            }
        }

        xSemaphoreGive(_mutex);

        // Write to hardware with global brightness scaling
        uint8_t br = _brightness.load();
        uint16_t n = _canvas.size();

        if (br == 255) {
            for (uint16_t i = 0; i < n; ++i) {
                (void)_strip.setPixel(i, _canvas[i]);
            }
        } else {
            // (value * (brightness + 1)) >> 8 gives exact 255→255 mapping
            uint16_t scale = static_cast<uint16_t>(br) + 1;
            for (uint16_t i = 0; i < n; ++i) {
                const auto& c = _canvas[i];
                Color scaled = {
                    static_cast<uint8_t>((uint16_t(c.r) * scale) >> 8),
                    static_cast<uint8_t>((uint16_t(c.g) * scale) >> 8),
                    static_cast<uint8_t>((uint16_t(c.b) * scale) >> 8)
                };
                (void)_strip.setPixel(i, scaled);
            }
        }
        (void)_strip.refresh();

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(_frame_delay_ms));
    }

    (void)_strip.clear();
    (void)_strip.refresh();
    _task_handle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace Espressif::Wrappers::SmartLed
