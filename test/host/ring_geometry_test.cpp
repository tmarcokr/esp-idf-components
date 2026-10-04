#include "RingGeometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using Espressif::Wrappers::Audio::RingGeometry;

namespace {

constexpr size_t kMixFrames = 256;
constexpr int kSimulationStepsPerSize = 1000000;
constexpr std::array<size_t, 4> kExhaustiveSizes = {8, 16, 32, 2048};
constexpr std::array<size_t, 4> kSimulatedSizes = {2048, 4096, 16384, 65536};

static_assert(RingGeometry::forSamples(16384).mask == 16383);
static_assert(RingGeometry::forSamples(16384).refill_watermark == 8192);
static_assert(RingGeometry::forSamples(16384).max_chunk == 4096);
static_assert(RingGeometry::forSamples(16384).sd_prefill == 4096);
static_assert(RingGeometry::forSamples(16384).memory_prefill == 16383);
static_assert(RingGeometry::isValidSize(RingGeometry::kPsramDefaultSamples));
static_assert(RingGeometry::isValidSize(RingGeometry::kInternalDefaultSamples));

int failures = 0;

void fail(const char* what, size_t n, size_t a, size_t b, size_t actual, size_t expected) {
    ++failures;
    if (failures <= 20) {
        std::printf("FAIL %s: N %zu, args (%zu, %zu): got %zu, expected %zu\n", what, n, a, b, actual, expected);
    }
}

size_t branchyAvailable(size_t n, size_t write, size_t read) {
    return (write >= read) ? (write - read) : (n - read + write);
}

class XorShift32 {
public:
    explicit XorShift32(uint32_t seed) : _state(seed) {}

    uint32_t next() {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        return _state;
    }

    uint32_t below(uint32_t bound) { return next() % bound; }

private:
    uint32_t _state;
};

void checkDefaults() {
    const RingGeometry g = RingGeometry::forSamples(16384);
    const std::array<std::pair<size_t, size_t>, 6> fields = {{
        {g.samples, 16384}, {g.mask, 16383}, {g.refill_watermark, 8192},
        {g.max_chunk, 4096}, {g.sd_prefill, 4096}, {g.memory_prefill, 16383},
    }};
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].first != fields[i].second) fail("forSamples(16384) field", 16384, i, 0, fields[i].first, fields[i].second);
    }

    const std::array<std::pair<size_t, bool>, 8> sizes = {{
        {0, false}, {1024, false}, {2048, true}, {3000, false},
        {4096, true}, {6144, false}, {65536, true}, {131072, false},
    }};
    for (const auto& [n, valid] : sizes) {
        if (RingGeometry::isValidSize(n) != valid) fail("isValidSize", n, 0, 0, RingGeometry::isValidSize(n), valid);
    }
}

void checkAllIndexPairs(size_t n) {
    const RingGeometry g = RingGeometry::forSamples(n);
    for (size_t w = 0; w < n; ++w) {
        if (g.contiguousFrom(w) != n - w) fail("contiguousFrom", n, w, 0, g.contiguousFrom(w), n - w);
        if (g.advance(w, g.contiguousFrom(w)) != 0) fail("advance to the end", n, w, 0, g.advance(w, g.contiguousFrom(w)), 0);
        for (size_t r = 0; r < n; ++r) {
            const size_t available = g.available(w, r);
            const size_t expected = branchyAvailable(n, w, r);
            if (available != expected) fail("available", n, w, r, available, expected);
            if (available + g.freeSpace(w, r) != n - 1) fail("available + freeSpace", n, w, r, available + g.freeSpace(w, r), n - 1);
            if (g.advance(w, r) != (w + r) % n) fail("advance", n, w, r, g.advance(w, r), (w + r) % n);
        }
    }
}

struct SimulatedRing {
    explicit SimulatedRing(size_t n) : geometry(RingGeometry::forSamples(n)), samples(n, 0), unread(n, false) {}

    RingGeometry geometry;
    std::vector<uint32_t> samples;
    std::vector<uint8_t> unread;
    size_t write = 0;
    size_t read = 0;
    uint32_t next_value = 0;
    uint32_t expected_value = 0;
    uint64_t produced = 0;
    uint64_t consumed = 0;
    uint64_t underruns = 0;

    size_t writeSamples(size_t start, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const size_t slot = start + i;
            if (unread[slot]) fail("overwrite of unread data", geometry.samples, slot, read, 1, 0);
            samples[slot] = next_value++;
            unread[slot] = 1;
        }
        return count;
    }

    void prefill(size_t target) {
        write = writeSamples(0, target);
        produced += target;
    }

    // Mirrors AudioChannel::needsRefill() and refillBuffer(); a read may return fewer samples than asked.
    void refill(XorShift32& rng, bool short_reads) {
        if (geometry.available(write, read) >= geometry.refill_watermark) return;
        const size_t free_space = geometry.freeSpace(write, read);
        if (free_space == 0) return;

        const size_t to_read = std::min({free_space, geometry.refill_watermark, geometry.max_chunk});
        const size_t contiguous = std::min(to_read, geometry.contiguousFrom(write));
        const auto readUpTo = [&](size_t count) {
            return (short_reads && rng.below(4) == 0) ? rng.below(static_cast<uint32_t>(count) + 1) : count;
        };

        size_t total_read = writeSamples(write, readUpTo(contiguous));
        write = geometry.advance(write, total_read);
        if (total_read == contiguous && to_read > contiguous) {
            const size_t second = writeSamples(write, readUpTo(to_read - contiguous));
            write = geometry.advance(write, second);
            total_read += second;
        }
        produced += total_read;
        checkLevel();
    }

    // Mirrors one mixer cycle: beginMixCycle(), kMixFrames getNextSample(), endMixCycle().
    void mixCycle() {
        const size_t mix_write = write;
        size_t mix_read = read;
        for (size_t frame = 0; frame < kMixFrames; ++frame) {
            if (mix_read == mix_write) {
                ++underruns;
                continue;
            }
            if (!unread[mix_read]) fail("read of a free slot", geometry.samples, mix_read, mix_write, 0, 1);
            if (samples[mix_read] != expected_value) {
                fail("FIFO order", geometry.samples, mix_read, mix_write, samples[mix_read], expected_value);
            }
            unread[mix_read] = 0;
            expected_value = samples[mix_read] + 1;
            ++consumed;
            mix_read = (mix_read + 1) & geometry.mask;
        }
        read = mix_read;
        checkLevel();
    }

    void checkLevel() {
        const size_t available = geometry.available(write, read);
        if (available > geometry.mask) fail("available above N - 1", geometry.samples, write, read, available, geometry.mask);
        if (available != produced - consumed) {
            fail("available vs produced - consumed", geometry.samples, write, read, available,
                 static_cast<size_t>(produced - consumed));
        }
    }
};

void simulate(size_t n, bool memory_backed, int steps) {
    SimulatedRing ring(n);
    ring.prefill(memory_backed ? ring.geometry.memory_prefill : ring.geometry.sd_prefill);
    XorShift32 rng(0x9E3779B9U ^ static_cast<uint32_t>(n));

    int step = 0;
    while (step < steps / 2) {
        const uint32_t refills = 1 + rng.below(3);
        const bool refill_first = rng.below(2) == 0;
        if (!refill_first) ring.mixCycle();
        for (uint32_t i = 0; i < refills; ++i) ring.refill(rng, false);
        if (refill_first) ring.mixCycle();
        step += static_cast<int>(refills) + 1;
    }
    const uint64_t steady_underruns = ring.underruns;
    if (steady_underruns != 0) {
        fail("underruns with a refill every mixer cycle", n, memory_backed, 0, static_cast<size_t>(steady_underruns), 0);
    }

    const uint64_t steady_mixed = ring.consumed;
    ring.underruns = 0;
    for (; step < steps; ++step) {
        if (rng.below(2) == 0) {
            ring.refill(rng, true);
        } else {
            ring.mixCycle();
        }
    }

    std::printf("N %5zu %-6s: steady %llu samples, %llu underruns; random %llu samples, %llu underruns\n", n,
                memory_backed ? "memory" : "sd", static_cast<unsigned long long>(steady_mixed),
                static_cast<unsigned long long>(steady_underruns),
                static_cast<unsigned long long>(ring.consumed - steady_mixed),
                static_cast<unsigned long long>(ring.underruns));
}

}  // namespace

int main() {
    checkDefaults();

    for (const size_t n : kExhaustiveSizes) {
        checkAllIndexPairs(n);
    }

    for (const size_t n : kSimulatedSizes) {
        if (!RingGeometry::isValidSize(n)) fail("simulated size is valid", n, 0, 0, 0, 1);
        simulate(n, false, kSimulationStepsPerSize / 2);
        simulate(n, true, kSimulationStepsPerSize / 2);
    }

    if (failures != 0) {
        std::printf("ring_geometry_test: FAIL (%d checks)\n", failures);
        return 1;
    }
    std::printf("ring_geometry_test: PASS\n");
    return 0;
}
