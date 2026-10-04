#pragma once

#include "RingGeometry.hpp"
#include <cstdint>

namespace Espressif::Wrappers::Audio {

/**
 * @brief Where the per-channel ring buffers are allocated.
 */
enum class RingMemory : uint8_t {
    Auto,      ///< PSRAM when the heap has PSRAM at init(), internal RAM otherwise.
    Psram,     ///< PSRAM only; init() fails with ESP_ERR_NO_MEM if it cannot hold every ring.
    Internal,  ///< Internal RAM only.
};

/**
 * @brief Ring size and memory resolved from the engine configuration.
 */
struct RingPlacement {
    bool valid;        ///< false when the requested memory or size is not accepted.
    bool in_psram;     ///< true for PSRAM, false for internal RAM.
    uint32_t samples;  ///< Samples per channel ring.
};

/**
 * @brief Resolve the ring memory and size once, before any ring is allocated.
 *
 * Auto picks PSRAM when @p psram_present, internal RAM otherwise. A size of 0 selects
 * RingGeometry::kPsramDefaultSamples in PSRAM and RingGeometry::kInternalDefaultSamples
 * in internal RAM; any other size must pass RingGeometry::isValidSize().
 *
 * @param memory Requested memory.
 * @param requested_samples Samples per ring, or 0 for the default of the resolved memory.
 * @param psram_present Whether the heap has PSRAM.
 * @return The placement; valid is false for an unknown @p memory or a rejected size.
 */
constexpr RingPlacement resolveRingPlacement(RingMemory memory, uint32_t requested_samples, bool psram_present) {
    bool in_psram = false;
    switch (memory) {
        case RingMemory::Auto:     in_psram = psram_present; break;
        case RingMemory::Psram:    in_psram = true; break;
        case RingMemory::Internal: in_psram = false; break;
        default:                   return RingPlacement{false, false, 0};
    }

    if (requested_samples == 0) {
        const size_t samples = in_psram ? RingGeometry::kPsramDefaultSamples : RingGeometry::kInternalDefaultSamples;
        return RingPlacement{true, in_psram, static_cast<uint32_t>(samples)};
    }
    if (!RingGeometry::isValidSize(requested_samples)) return RingPlacement{false, in_psram, requested_samples};
    return RingPlacement{true, in_psram, requested_samples};
}

} // namespace Espressif::Wrappers::Audio
