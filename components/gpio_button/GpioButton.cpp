#include "GpioButton.hpp"
#include "esp_timer.h"

namespace Espressif::Wrappers {

GpioButton::GpioButton(gpio_num_t gpio_num, bool active_low)
    : _gpio_num(gpio_num), _active_low(active_low), _initialized(false),
      _task_handle(nullptr), _is_pressed(false), _press_start_time_ms(0) {}

GpioButton::~GpioButton() {
    if (_task_handle != nullptr) {
        vTaskDelete(_task_handle);
    }
    gpio_reset_pin(_gpio_num);
}

esp_err_t GpioButton::init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << _gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = _active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = _active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    _initialized = true;

    BaseType_t ret = xTaskCreate(pollTask, "gpio_btn_tsk", 2048, this, 5, &_task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

void GpioButton::onEvent(ButtonEvent event, Callback callback) {
    _event_callbacks[event] = callback;
}

void GpioButton::onLongPress(uint32_t duration_ms, Callback callback) {
    _long_press_callbacks[duration_ms] = callback;
    _long_press_fired[duration_ms] = false;
}

void GpioButton::processState() {
    const bool current_state = _active_low ? (gpio_get_level(_gpio_num) == 0) : (gpio_get_level(_gpio_num) == 1);
    const uint32_t now = esp_timer_get_time() / 1000;

    if (current_state && !_is_pressed) {
        _is_pressed = true;
        _press_start_time_ms = now;
        
        for (auto& [duration, fired] : _long_press_fired) fired = false;

        if (_event_callbacks.contains(ButtonEvent::PressDown)) {
            _event_callbacks[ButtonEvent::PressDown]();
        }
    } else if (!current_state && _is_pressed) {
        _is_pressed = false;

        if (_event_callbacks.contains(ButtonEvent::PressUp)) {
            _event_callbacks[ButtonEvent::PressUp]();
        }

        bool triggered_long_press = false;
        for (const auto& [duration, fired] : _long_press_fired) {
            if (fired) {
                triggered_long_press = true;
                break;
            }
        }

        if (!triggered_long_press && _event_callbacks.contains(ButtonEvent::Click)) {
            _event_callbacks[ButtonEvent::Click]();
        }
    } else if (current_state && _is_pressed) {
        const uint32_t elapsed = now - _press_start_time_ms;
        
        for (auto& [duration, callback] : _long_press_callbacks) {
            if (elapsed >= duration && !_long_press_fired[duration]) {
                _long_press_fired[duration] = true;
                callback();
            }
        }
    }
}

void GpioButton::pollTask(void *arg) {
    auto *button = static_cast<GpioButton*>(arg);
    while (true) {
        button->processState();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

} // namespace Espressif::Wrappers
