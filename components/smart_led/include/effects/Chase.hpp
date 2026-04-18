#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "SmartLedTypes.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief A lit pixel with a fading tail running along the strip.
 */
class Chase : public IEffect {
public:
    /**
     * @param color Head color.
     * @param tail_length Number of trailing pixels (brightness-faded).
     * @param step_ms Time per pixel advance.
     */
    Chase(Color color, uint8_t tail_length, uint32_t step_ms);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    Color _color;
    uint8_t _tail;
    uint32_t _step_ms;
    uint32_t _position_x256;
};

} // namespace Espressif::Wrappers::SmartLed
