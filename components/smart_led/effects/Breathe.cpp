#include "effects/Breathe.hpp"

namespace Espressif::Wrappers::SmartLed {

Breathe::Breathe(uint16_t hue, uint32_t period_ms)
    : _hue(hue), _period_ms(period_ms), _elapsed(0) {}

void Breathe::update(uint32_t delta_ms) {
    _elapsed += delta_ms;
    _elapsed %= _period_ms;
}

void Breathe::render(Canvas& canvas) {
    // Triangular wave: 0 → 255 → 0 over one period
    uint32_t phase = (_elapsed * 512) / _period_ms;
    uint8_t brightness = (phase < 256)
        ? static_cast<uint8_t>(phase)
        : static_cast<uint8_t>(511 - phase);

    canvas.fill(hsvToRgb(_hue, 255, brightness));
}

bool Breathe::isFinished() const { return false; }

} // namespace Espressif::Wrappers::SmartLed
