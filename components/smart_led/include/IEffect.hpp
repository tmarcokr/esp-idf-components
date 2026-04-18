#pragma once

#include "Canvas.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Abstract interface for composable LED effects.
 *
 * Implement update() to advance internal state, render() to draw into the
 * Canvas, and isFinished() to signal automatic removal for overlay effects.
 */
class IEffect {
public:
    virtual ~IEffect() = default;

    /** @brief Advance the effect's internal state by delta_ms milliseconds. */
    virtual void update(uint32_t delta_ms) = 0;

    /** @brief Render the current frame into the canvas. */
    virtual void render(Canvas& canvas) = 0;

    /** @brief Return true when the effect has completed (overlays auto-remove). */
    [[nodiscard]] virtual bool isFinished() const = 0;
};

} // namespace Espressif::Wrappers::SmartLed
