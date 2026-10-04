#pragma once

#include <cstddef>

namespace Espressif::Wrappers::Audio {

/**
 * @brief Size, refill thresholds and index math of a single-producer single-consumer sample ring.
 *
 * The ring holds a power-of-two number of samples N and keeps one slot empty to tell full
 * from empty, so it buffers at most N - 1 samples. Indices stay in [0, N).
 */
struct RingGeometry {
    static constexpr size_t kMinSamples = 2048;
    static constexpr size_t kMaxSamples = 65536;
    static constexpr size_t kPsramDefaultSamples = 16384;
    static constexpr size_t kInternalDefaultSamples = 4096;

    size_t samples;           ///< Capacity N in samples (power of two).
    size_t mask;              ///< N - 1.
    size_t refill_watermark;  ///< N / 2: the reader refills below this level and adds at most this many samples.
    size_t max_chunk;         ///< N / 4: samples per file read; bounds how long one refill holds the file system lock.
    size_t sd_prefill;        ///< N / 4: samples buffered by load() for an SD file, so a trigger holds the SD lock briefly.
    size_t memory_prefill;    ///< N - 1: samples buffered by load() for an in-memory file (the whole ring).

    /**
     * @brief Check a ring size.
     * @param n Samples.
     * @return true if @p n is a power of two within [kMinSamples, kMaxSamples].
     */
    static constexpr bool isValidSize(size_t n) {
        return n >= kMinSamples && n <= kMaxSamples && (n & (n - 1)) == 0;
    }

    /**
     * @brief Derive the geometry of an N-sample ring, without validation (see isValidSize()).
     * @param n Samples; must be a power of two of at least 4.
     * @return The geometry.
     */
    static constexpr RingGeometry forSamples(size_t n) {
        return RingGeometry{n, n - 1, n / 2, n / 4, n / 4, n - 1};
    }

    /**
     * @brief Unread samples between a read and a write index.
     * @param write Producer index.
     * @param read Consumer index.
     * @return Samples in [0, N - 1].
     */
    constexpr size_t available(size_t write, size_t read) const {
        // Trick: unsigned wrap-around is modulo 2^bits, which N divides, so the masked difference
        // equals the branchy (write >= read ? write - read : N - read + write).
        return (write - read) & mask;
    }

    /**
     * @brief Samples the producer may write without overwriting unread data.
     * @param write Producer index.
     * @param read Consumer index.
     * @return N - 1 - available(write, read).
     */
    constexpr size_t freeSpace(size_t write, size_t read) const {
        return mask - available(write, read);
    }

    /**
     * @brief Move an index forward with wrap-around.
     * @param index Index in [0, N).
     * @param count Samples to advance.
     * @return (index + count) mod N.
     */
    constexpr size_t advance(size_t index, size_t count) const {
        return (index + count) & mask;
    }

    /**
     * @brief Slots from an index to the physical end of the buffer.
     * @param index Index in [0, N).
     * @return N - index.
     */
    constexpr size_t contiguousFrom(size_t index) const {
        return samples - index;
    }
};

} // namespace Espressif::Wrappers::Audio
