#include "../../components/audio/audio_channel/WavDataSize.hpp"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>

using Espressif::Wrappers::Audio::clampWavDataSize;

namespace {

struct Case {
    const char* name;
    uint32_t declared_bytes;
    uint32_t data_offset;
    uint64_t file_size;
    uint32_t expected;
};

constexpr uint32_t kHeader = 44;

constexpr std::array<Case, 10> kCases = {{
    {"declared size fits the file", 1000, kHeader, kHeader + 2000, 1000},
    {"declared size equals the file", 22050, kHeader, kHeader + 22050, 22050},
    {"truncated file (4x declared)", 4 * 22050, kHeader, kHeader + 22050, 22050},
    {"streaming placeholder 0xFFFFFFFF", 0xFFFFFFFFU, kHeader, kHeader + 22050, 22050},
    {"odd remainder in a truncated file", 100000, kHeader, kHeader + 1001, 1000},
    {"odd declared size inside the file", 1001, kHeader, kHeader + 5000, 1000},
    {"data offset at the end of the file", 1000, kHeader, kHeader, 0},
    {"data offset beyond the file", 1000, 5000, 100, 0},
    {"file larger than 4 GiB", 0xFFFFFFFFU, kHeader, uint64_t{1} << 33, 0xFFFFFFFEU},
    {"maximal data offset", 0xFFFFFFFFU, 0xFFFFFFFFU, 0x100000010ULL, 16},
}};

static_assert(clampWavDataSize(4 * 22050, kHeader, kHeader + 22050) == 22050);

}  // namespace

int main() {
    int failures = 0;
    for (const Case& c : kCases) {
        const uint32_t actual = clampWavDataSize(c.declared_bytes, c.data_offset, c.file_size);
        if (actual != c.expected) {
            ++failures;
            std::printf("FAIL %s: got %" PRIu32 ", expected %" PRIu32 "\n", c.name, actual, c.expected);
        }
    }

    if (failures != 0) {
        std::printf("wav_data_size_test: FAIL (%d cases)\n", failures);
        return 1;
    }
    std::printf("wav_data_size_test: %zu cases checked\n", kCases.size());
    std::printf("wav_data_size_test: PASS\n");
    return 0;
}
