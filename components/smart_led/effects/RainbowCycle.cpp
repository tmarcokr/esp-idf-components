#include "effects/RainbowCycle.hpp"
#include "SmartLedTypes.hpp"

namespace Espressif::Wrappers::SmartLed {

RainbowCycle::RainbowCycle(uint32_t cycle_ms)
    : _cycle_ms(cycle_ms), _elapsed(0) {}

void RainbowCycle::update(uint32_t delta_ms) {
    _elapsed += delta_ms;
    _elapsed %= _cycle_ms;
}

void RainbowCycle::render(Canvas& canvas) {
    uint16_t n = canvas.size();
    if (n == 0) return;

    uint16_t offset = static_cast<uint16_t>((_elapsed * 360) / _cycle_ms);
    for (uint16_t i = 0; i < n; ++i) {
        uint16_t hue = (offset + (i * 360) / n) % 360;
        canvas.setPixel(i, hsvToRgb(hue, 255, 255));
    }
}

bool RainbowCycle::isFinished() const { return false; }

} // namespace Espressif::Wrappers::SmartLed
