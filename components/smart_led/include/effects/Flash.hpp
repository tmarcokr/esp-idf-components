#pragma once

#include "Canvas.hpp"
#include "IEffect.hpp"
#include "SmartLedTypes.hpp"
#include <cstdint>

namespace Espressif::Wrappers::SmartLed {

/**
 * @brief Blink N times then auto-remove. Designed as an overlay.
 */
class Flash : public IEffect {
public:
    /**
     * @param color Flash color.
     * @param on_ms On-phase duration.
     * @param off_ms Off-phase duration.
     * @param count Number of blinks before finishing.
     */
    Flash(Color color, uint32_t on_ms, uint32_t off_ms, uint8_t count);

    void update(uint32_t delta_ms) override;
    void render(Canvas& canvas) override;
    [[nodiscard]] bool isFinished() const override;

private:
    Color _color;
    uint32_t _on_ms;
    uint32_t _off_ms;
    uint8_t _total_blinks;
    uint8_t _current_blink;
    uint32_t _phase_elapsed;
    bool _is_on;
};

} // namespace Espressif::Wrappers::SmartLed
