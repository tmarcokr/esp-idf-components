#include "Canvas.hpp"

namespace Espressif::Wrappers::SmartLed {

Canvas::Canvas(uint16_t num_leds) : _buffer(num_leds, Color::Off()) {}

void Canvas::setPixel(uint16_t index, Color color) {
    if (index < _buffer.size()) _buffer[index] = color;
}

void Canvas::fill(Color color) {
    std::fill(_buffer.begin(), _buffer.end(), color);
}

void Canvas::fillRange(uint16_t start, uint16_t count, Color color) {
    uint16_t end = std::min<uint16_t>(start + count, static_cast<uint16_t>(_buffer.size()));
    for (uint16_t i = start; i < end; ++i) {
        _buffer[i] = color;
    }
}

void Canvas::gradient(uint16_t start, uint16_t count, Color from, Color to) {
    if (count == 0) return;
    for (uint16_t i = 0; i < count && (start + i) < _buffer.size(); ++i) {
        uint8_t ratio = (count > 1) ? static_cast<uint8_t>((i * 255) / (count - 1)) : 0;
        _buffer[start + i] = blend(from, to, ratio);
    }
}

void Canvas::blendPixel(uint16_t index, Color color, uint8_t alpha) {
    if (index < _buffer.size()) {
        _buffer[index] = blend(_buffer[index], color, alpha);
    }
}

void Canvas::clear() {
    fill(Color::Off());
}

uint16_t Canvas::size() const {
    return static_cast<uint16_t>(_buffer.size());
}

const Color& Canvas::operator[](uint16_t index) const {
    return _buffer[index];
}

} // namespace Espressif::Wrappers::SmartLed
