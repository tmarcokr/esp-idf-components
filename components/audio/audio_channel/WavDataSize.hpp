#pragma once

#include <algorithm>
#include <cstdint>

namespace Espressif::Wrappers::Audio {

inline constexpr uint64_t wavDataAvailable(uint32_t data_offset, uint64_t file_size) {
    return (file_size > data_offset) ? file_size - data_offset : 0;
}

inline constexpr uint32_t clampWavDataSize(uint32_t declared_bytes, uint32_t data_offset, uint64_t file_size) {
    const uint64_t clamped = std::min<uint64_t>(declared_bytes, wavDataAvailable(data_offset, file_size));
    return static_cast<uint32_t>(clamped) & ~static_cast<uint32_t>(sizeof(int16_t) - 1);
}

} // namespace Espressif::Wrappers::Audio
