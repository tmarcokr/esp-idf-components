#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <functional>
#include <map>

namespace Espressif::Wrappers {

/**
 * @brief Interaction events for a GPIO button.
 */
enum class ButtonEvent {
    PressDown, 
    PressUp,   
    Click      
};

/**
 * @brief RAII wrapper for a mechanical push button.
 * 
 * Uses background polling to handle debouncing and multi-threshold timing.
 */
class GpioButton {
public:
    using Callback = std::function<void()>;

    /**
     * @brief Construct a new Gpio Button.
     * @param gpio_num Hardware GPIO pin.
     * @param active_low True for pull-up (GND short), False for pull-down (VCC short).
     */
    explicit GpioButton(gpio_num_t gpio_num, bool active_low = true);

    ~GpioButton();

    GpioButton(const GpioButton &) = delete;
    GpioButton &operator=(const GpioButton &) = delete;

    /**
     * @brief Initialize hardware and start polling task.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Bind an action to a standard event.
     */
    void onEvent(ButtonEvent event, Callback callback);

    /**
     * @brief Bind an action to a continuous hold duration.
     * @param duration_ms Required time in milliseconds.
     */
    void onLongPress(uint32_t duration_ms, Callback callback);

private:
    gpio_num_t _gpio_num;
    bool _active_low;
    bool _initialized;
    TaskHandle_t _task_handle;

    bool _is_pressed;
    uint32_t _press_start_time_ms;

    std::map<ButtonEvent, Callback> _event_callbacks;
    std::map<uint32_t, Callback> _long_press_callbacks;
    std::map<uint32_t, bool> _long_press_fired;

    static void pollTask(void *arg);
    void processState();
};

} // namespace Espressif::Wrappers
