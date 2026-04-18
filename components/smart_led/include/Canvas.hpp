#pragma once

#include "SmartLedTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief In-memory pixel buffer that effects render into. Decoupled from hardware.
 */
class Canvas {
public:
    explicit Canvas(uint16_t num_leds);

    void setPixel(uint16_t index, Color color);
    void fill(Color color);
    void fillRange(uint16_t start, uint16_t count, Color color);
    void gradient(uint16_t start, uint16_t count, Color from, Color to);
    void blendPixel(uint16_t index, Color color, uint8_t alpha);
    void clear();

    [[nodiscard]] uint16_t size() const;
    [[nodiscard]] const Color& operator[](uint16_t index) const;

private:
    std::vector<Color> _buffer;
};

} // namespace Espressif::Wrappers::SmartLed
