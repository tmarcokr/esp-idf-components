#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "SmartLedTypes.hpp"

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Static single-color fill. Runs indefinitely.
 */
class SolidColor : public IEffect {
public:
    explicit SolidColor(Color color);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    Color _color;
};

} // namespace Espressif::Wrappers::SmartLed
