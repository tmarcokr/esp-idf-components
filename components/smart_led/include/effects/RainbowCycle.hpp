#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Rotating rainbow distributed across the entire strip.
 */
class RainbowCycle : public IEffect {
public:
    /**
     * @param cycle_ms Time for one full 360° rotation.
     */
    explicit RainbowCycle(uint32_t cycle_ms);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    uint32_t _cycle_ms;
    uint32_t _elapsed;
};

} // namespace Espressif::Wrappers::SmartLed
