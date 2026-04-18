#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "SmartLedTypes.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Pulsating brightness on a fixed hue. Triangular wave.
 */
class Breathe : public IEffect {
public:
    /**
     * @param hue Color hue (0–359).
     * @param period_ms Full cycle duration (bright → dim → bright).
     */
    Breathe(uint16_t hue, uint32_t period_ms);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    uint16_t _hue;
    uint32_t _period_ms;
    uint32_t _elapsed;
};

} // namespace Espressif::Wrappers::SmartLed
