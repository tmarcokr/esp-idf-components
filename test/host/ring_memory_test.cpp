#include "RingMemory.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

using Espressif::Wrappers::Audio::resolveRingPlacement;
using Espressif::Wrappers::Audio::RingGeometry;
using Espressif::Wrappers::Audio::RingMemory;
using Espressif::Wrappers::Audio::RingPlacement;

namespace {

struct Case {
    RingMemory memory;
    uint32_t requested_samples;
    bool psram_present;
    RingPlacement expected;
};

constexpr RingPlacement kPsramDefault{true, true, 16384};
constexpr RingPlacement kInternalDefault{true, false, 4096};

constexpr std::array<Case, 24> kCases = {{
    {RingMemory::Auto, 0, true, kPsramDefault},
    {RingMemory::Auto, 0, false, kInternalDefault},
    {RingMemory::Psram, 0, true, kPsramDefault},
    {RingMemory::Psram, 0, false, kPsramDefault},
    {RingMemory::Internal, 0, true, kInternalDefault},
    {RingMemory::Internal, 0, false, kInternalDefault},

    {RingMemory::Auto, 2048, true, {true, true, 2048}},
    {RingMemory::Auto, 8192, false, {true, false, 8192}},
    {RingMemory::Auto, 65536, true, {true, true, 65536}},
    {RingMemory::Psram, 4096, false, {true, true, 4096}},
    {RingMemory::Internal, 16384, true, {true, false, 16384}},
    {RingMemory::Internal, 65536, false, {true, false, 65536}},

    {RingMemory::Auto, 1, true, {false, true, 1}},
    {RingMemory::Auto, 1024, false, {false, false, 1024}},
    {RingMemory::Auto, 2047, true, {false, true, 2047}},
    {RingMemory::Auto, 3000, false, {false, false, 3000}},
    {RingMemory::Psram, 6144, true, {false, true, 6144}},
    {RingMemory::Internal, 65537, false, {false, false, 65537}},
    {RingMemory::Internal, 131072, true, {false, false, 131072}},
    {RingMemory::Auto, UINT32_MAX, true, {false, true, UINT32_MAX}},
    {RingMemory::Psram, 0x80000000U, false, {false, true, 0x80000000U}},

    {static_cast<RingMemory>(3), 0, true, {false, false, 0}},
    {static_cast<RingMemory>(3), 4096, false, {false, false, 0}},
    {static_cast<RingMemory>(255), 0, false, {false, false, 0}},
}};

constexpr bool same(const RingPlacement& a, const RingPlacement& b) {
    return a.valid == b.valid && a.in_psram == b.in_psram && a.samples == b.samples;
}

static_assert(same(resolveRingPlacement(RingMemory::Auto, 0, true), kPsramDefault));
static_assert(same(resolveRingPlacement(RingMemory::Auto, 0, false), kInternalDefault));
static_assert(RingGeometry::isValidSize(kPsramDefault.samples));
static_assert(RingGeometry::isValidSize(kInternalDefault.samples));

}  // namespace

int main() {
    int failures = 0;
    for (size_t i = 0; i < kCases.size(); ++i) {
        const Case& c = kCases[i];
        const RingPlacement actual = resolveRingPlacement(c.memory, c.requested_samples, c.psram_present);
        if (!same(actual, c.expected)) {
            ++failures;
            std::printf("FAIL case %zu (memory %u, %u samples, PSRAM %d): got {%d, %d, %u}, expected {%d, %d, %u}\n",
                        i, static_cast<unsigned>(c.memory), static_cast<unsigned>(c.requested_samples),
                        c.psram_present, actual.valid, actual.in_psram, static_cast<unsigned>(actual.samples),
                        c.expected.valid, c.expected.in_psram, static_cast<unsigned>(c.expected.samples));
        }
    }

    int sweep_failures = 0;
    for (uint32_t samples = 1; samples <= 2 * RingGeometry::kMaxSamples; ++samples) {
        for (const RingMemory memory : {RingMemory::Auto, RingMemory::Psram, RingMemory::Internal}) {
            const RingPlacement placement = resolveRingPlacement(memory, samples, true);
            const bool accepted = (samples & (samples - 1)) == 0 && samples >= 2048 && samples <= 65536;
            if (placement.valid != accepted || placement.samples != samples) ++sweep_failures;
        }
    }
    if (sweep_failures != 0) {
        ++failures;
        std::printf("FAIL size sweep: %d mismatches\n", sweep_failures);
    }

    if (failures != 0) {
        std::printf("ring_memory_test: FAIL (%d checks)\n", failures);
        return 1;
    }
    std::printf("ring_memory_test: %zu cases and sizes 1..%zu checked\n", kCases.size(), 2 * RingGeometry::kMaxSamples);
    std::printf("ring_memory_test: PASS\n");
    return 0;
}
