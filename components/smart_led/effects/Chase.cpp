#include "effects/Chase.hpp"

namespace Espressif::Wrappers::SmartLed {

Chase::Chase(Color color, uint8_t tail_length, uint32_t step_ms)
    : _color(color), _tail(tail_length), _step_ms(step_ms), _position_x256(0) {}

void Chase::update(uint32_t delta_ms) {
    _position_x256 += (delta_ms * 256) / _step_ms;
}

void Chase::render(Canvas& canvas) {
    uint16_t n = canvas.size();
    if (n == 0) return;

    uint16_t head = (_position_x256 / 256) % n;

    for (uint8_t i = 0; i <= _tail; ++i) {
        int16_t idx = static_cast<int16_t>(head) - i;
        if (idx < 0) idx += n;

        // Brightness fades linearly along the tail
        uint16_t scale = static_cast<uint16_t>(255 - (i * 255 / (_tail + 1))) + 1;
        Color c = {
            static_cast<uint8_t>((uint16_t(_color.r) * scale) >> 8),
            static_cast<uint8_t>((uint16_t(_color.g) * scale) >> 8),
            static_cast<uint8_t>((uint16_t(_color.b) * scale) >> 8)
        };
        canvas.setPixel(static_cast<uint16_t>(idx), c);
    }
}

bool Chase::isFinished() const { return false; }

} // namespace Espressif::Wrappers::SmartLed
