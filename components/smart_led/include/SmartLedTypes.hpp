#pragma once

#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief RGB color triplet with factory methods for common colors and HSV conversion.
 */
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    static constexpr Color Red()     { return {255, 0,   0  }; }
    static constexpr Color Green()   { return {0,   255, 0  }; }
    static constexpr Color Blue()    { return {0,   0,   255}; }
    static constexpr Color White()   { return {255, 255, 255}; }
    static constexpr Color Off()     { return {0,   0,   0  }; }
    static constexpr Color Yellow()  { return {255, 255, 0  }; }
    static constexpr Color Cyan()    { return {0,   255, 255}; }
    static constexpr Color Magenta() { return {255, 0,   255}; }
    static constexpr Color Orange()  { return {255, 165, 0  }; }

    constexpr bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b;
    }

    constexpr bool operator!=(const Color& o) const {
        return !(*this == o);
    }
};

/**
 * @brief Integer-only HSV to RGB conversion. Optimized for chips without FPU.
 * @param h Hue (0–359).
 * @param s Saturation (0–255).
 * @param v Value / brightness (0–255).
 */
constexpr Color hsvToRgb(uint16_t h, uint8_t s, uint8_t v) {
    if (s == 0) return {v, v, v};

    h = h % 360;
    uint8_t region = h / 60;
    uint16_t remainder = ((h - region * 60) * 255) / 60;

    uint8_t p = static_cast<uint8_t>((uint16_t(v) * (255 - s)) >> 8);
    uint8_t q = static_cast<uint8_t>((uint16_t(v) * (255 - ((uint16_t(s) * remainder) >> 8))) >> 8);
    uint8_t t = static_cast<uint8_t>((uint16_t(v) * (255 - ((uint16_t(s) * (255 - remainder)) >> 8))) >> 8);

    switch (region) {
        case 0:  return {v, t, p};
        case 1:  return {q, v, p};
        case 2:  return {p, v, t};
        case 3:  return {p, q, v};
        case 4:  return {t, p, v};
        default: return {v, p, q};
    }
}

/**
 * @brief Linear blend between two colors.
 * @param a Source color (ratio = 0).
 * @param b Target color (ratio = 255).
 * @param ratio Blend factor (0 = fully a, 255 = fully b).
 */
constexpr Color blend(Color a, Color b, uint8_t ratio) {
    uint16_t inv = 255 - ratio;
    return {
        static_cast<uint8_t>((uint16_t(a.r) * inv + uint16_t(b.r) * ratio) / 255),
        static_cast<uint8_t>((uint16_t(a.g) * inv + uint16_t(b.g) * ratio) / 255),
        static_cast<uint8_t>((uint16_t(a.b) * inv + uint16_t(b.b) * ratio) / 255)
    };
}

} // namespace Espressif::Wrappers::SmartLed
