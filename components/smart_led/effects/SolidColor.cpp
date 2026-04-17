#include "effects/SolidColor.hpp"

namespace Espressif::Wrappers::SmartLed {

SolidColor::SolidColor(Color color) : _color(color) {}

void SolidColor::update(uint32_t /*delta_ms*/) {}

void SolidColor::render(Canvas& canvas) {
    canvas.fill(_color);
}

bool SolidColor::isFinished() const { return false; }

} // namespace Espressif::Wrappers::SmartLed
