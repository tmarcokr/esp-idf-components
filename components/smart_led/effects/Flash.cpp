#include "effects/Flash.hpp"

namespace Espressif::Wrappers::SmartLed {

Flash::Flash(Color color, uint32_t on_ms, uint32_t off_ms, uint8_t count)
    : _color(color), _on_ms(on_ms), _off_ms(off_ms), _total_blinks(count),
      _current_blink(0), _phase_elapsed(0), _is_on(true) {}

void Flash::update(uint32_t delta_ms) {
    if (isFinished()) return;

    _phase_elapsed += delta_ms;

    uint32_t phase_duration = _is_on ? _on_ms : _off_ms;
    while (_phase_elapsed >= phase_duration && !isFinished()) {
        _phase_elapsed -= phase_duration;
        if (_is_on) {
            _is_on = false;
        } else {
            _is_on = true;
            _current_blink++;
        }
        phase_duration = _is_on ? _on_ms : _off_ms;
    }
}

void Flash::render(Canvas& canvas) {
    if (_is_on && !isFinished()) {
        canvas.fill(_color);
    }
}

bool Flash::isFinished() const {
    return _current_blink >= _total_blinks;
}

} // namespace Espressif::Wrappers::SmartLed
