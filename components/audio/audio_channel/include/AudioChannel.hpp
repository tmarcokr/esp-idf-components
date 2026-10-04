#pragma once

#include "esp_err.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace Espressif::Wrappers::Audio {

/**
 * @brief Per-channel audio state: WAV file streaming, ring buffer, volume ramping, and loop control.
 *
 * Each AudioChannel manages its own ring buffer filled by a reader task, and provides
 * sample-by-sample extraction for the mixer. Supports seamless looping and anti-click
 * volume ramping for dynamic crossfading compatibility.
 *
 * Ownership model: an atomic lifecycle state decides which task owns the file handle and
 * the write side of the ring buffer at any time.
 *
 * | State      | Owner of the file and ring write side        | Mixed            | Refilled |
 * |:-----------|:---------------------------------------------|:-----------------|:---------|
 * | `Idle`     | nobody                                       | no               | no       |
 * | `Loading`  | the task that won claim() and calls load()   | no               | no       |
 * | `Ready`    | the reader task matching isMemoryBacked()    | no               | yes      |
 * | `Active`   | owning reader                                | yes              | yes      |
 * | `Stopping` | owning reader                                | yes, fading to 0 | yes      |
 * | `Closing`  | owning reader, which closes and resets it    | no               | no       |
 *
 * Thread safety model:
 * - claim() / load(): caller task (only the task that won claim() may call load()).
 * - requestStop() / setTargetVolume(): any task, lock-free.
 * - arm() / disarm(): the task that runs AudioEngine::startGroup().
 * - start() / setGroup() / beginMixCycle() / mix*() / getNextSample() / endMixCycle(): mixer task only.
 * - isOwnedBy() / needsRefill() / refillBuffer() / closeIfClosing(): reader tasks only.
 * - state() / isActive() / underruns() / readFailures() / startCycle() / group() / dataSize() /
 *   hasOpenFile(): any task.
 */
class AudioChannel {
public:
    /**
     * @brief Lifecycle state of a channel.
     */
    enum class State : uint8_t {
        Idle,       ///< Free; can be claimed.
        Loading,    ///< Claimed; the claimant opens, parses and prefills.
        Ready,      ///< Loaded and refilled, waiting for the mixer to start it.
        Active,     ///< Mixed at its target volume.
        Stopping,   ///< Mixed while fading to 0.
        Closing,    ///< Waiting for the owning reader to close and reset it.
    };

    /**
     * @brief Parsed WAV file header information.
     */
    struct WavHeader {
        uint32_t sample_rate;       ///< Sample rate from fmt chunk (Hz)
        uint16_t bits_per_sample;   ///< Bits per sample (expected: 16)
        uint16_t num_channels;      ///< Number of channels (expected: 1)
        uint32_t data_offset;       ///< Byte offset to the start of PCM data
        uint32_t data_size;         ///< Size of PCM data in bytes
    };

    /**
     * @brief Construct a new Audio Channel.
     */
    AudioChannel();

    /**
     * @brief Destroy the Audio Channel and release resources.
     *
     * Warning: no task may access the channel during or after destruction.
     */
    ~AudioChannel();

    AudioChannel(const AudioChannel&) = delete;
    AudioChannel& operator=(const AudioChannel&) = delete;

    /**
     * @brief Claim an Idle channel for loading (Idle → Loading).
     * @return true if the caller now owns the channel and must call load().
     */
    [[nodiscard]] bool claim();

    /**
     * @brief Load a WAV file into a claimed channel (Loading → Ready).
     *
     * Opens the file, parses and validates the WAV header and fills the initial ring
     * buffer. The channel is not audible until the mixer starts it (see start()).
     * A one-shot fully buffered by the prefill has its file closed before publication.
     * On any failure the channel is back in Idle and the caller must not touch it.
     *
     * @param path Full filesystem path (e.g., "/sdcard/track.wav").
     * @param loop Enable seamless looping (wraps to data start on EOF).
     * @param initial_volume 14-bit volume (0–16384). Channels at volume 0
     *        still advance playback position (required for dynamic crossfading).
     *
     * @return ESP_OK on success; ESP_ERR_INVALID_STATE if the channel was not claimed or a
     *         stop was requested while loading; ESP_ERR_NO_MEM without a ring buffer;
     *         ESP_ERR_INVALID_SIZE for a loop without samples; a parse or open error otherwise.
     */
    [[nodiscard]] esp_err_t load(std::string_view path, bool loop, uint16_t initial_volume);

    /**
     * @brief Request a stop from any task, lock-free; never dropped.
     *
     * Ready → Closing, Active → Stopping (fade-out, then the mixer moves it to Closing).
     * While Loading, cancels the load: load() fails and returns the channel to Idle, or,
     * if the publication wins the race, the channel is closed right after it becomes Ready
     * (same outcome). No-op in Idle, Stopping and Closing.
     */
    void requestStop();

    /**
     * @brief Mark a Ready channel as a member of start request @p request.
     *
     * Only one request can arm a channel; the mark is dropped if the channel is stopped.
     *
     * @param request Start request index (0–31) owned by the caller.
     * @return true if the channel was Ready and not armed, and is now armed for @p request.
     */
    [[nodiscard]] bool arm(uint8_t request);

    /**
     * @brief Undo arm() if the channel is still Ready and armed for @p request.
     * @param request Start request index passed to arm().
     */
    void disarm(uint8_t request);

    /**
     * @brief Mixer only: start a channel armed for @p request (Ready → Active).
     * @param request Start request index the channel must be armed for.
     * @param cycle Mixer cycle counter value, reported by startCycle().
     * @return true if the channel became Active; false if it left Ready or is not armed for @p request.
     */
    bool start(uint8_t request, uint32_t cycle);

    /**
     * @brief Mixer only: set the linked group id of an Active channel (0 = not linked).
     * @param group Group id.
     */
    void setGroup(uint8_t group);

    /**
     * @brief Linked group id; 0 when the channel is not linked or has left its group.
     * @return The group id.
     */
    uint8_t group() const;

    /**
     * @brief Size of the PCM data of the current sound.
     * @return Data size in bytes; 0 when no sound is loaded.
     */
    uint32_t dataSize() const;

    /**
     * @brief Mixer only: snapshot the channel for one mixer cycle.
     * @return true if the channel is Active or Stopping and must be mixed in this cycle.
     */
    bool beginMixCycle();

    /**
     * @brief Mixer only: group id used by the linked-group policy in this cycle.
     * @return The group id if the channel is Active in this cycle, otherwise 0.
     */
    uint8_t mixGroup() const;

    /**
     * @brief Mixer only: samples buffered at the start of this cycle.
     * @return Unread samples in the cycle snapshot.
     */
    size_t mixBufferedSamples() const;

    /**
     * @brief Mixer only: whether the whole file was buffered at the start of this cycle.
     * @return true if the end of a one-shot is in the ring buffer.
     */
    bool mixAtEof() const;

    /**
     * @brief Mixer only: extract the next PCM sample with volume scaling and ramping.
     *
     * Valid only between a beginMixCycle() that returned true and endMixCycle().
     * Applies per-sample volume ramping to prevent audible clicks: exponential toward
     * the target volume, or a linear fade to 0 (~5.8ms) while Stopping.
     *
     * @return Scaled 16-bit PCM sample, or 0 on underrun or after the end of a one-shot.
     */
    int16_t getNextSample();

    /**
     * @brief Mixer only: publish the consumed samples and finish the cycle.
     *
     * Moves a Stopping channel whose fade reached 0 (or whose buffer ran dry), and a
     * one-shot that played to its end, to Closing.
     *
     * @return Underrun samples (silence output while not at the end of the file) in this cycle.
     */
    uint32_t endMixCycle();

    /**
     * @brief Set the target volume for smooth ramping.
     *
     * Thread-safe (atomic write). The actual volume converges toward
     * the target over ~5ms via exponential decay in getNextSample().
     * Ignored while the channel is Stopping (it fades to 0).
     *
     * @param volume 14-bit target volume (0–16384 = 0%–100%).
     */
    void setTargetVolume(uint16_t volume);

    /**
     * @brief Current lifecycle state (acquire load).
     * @return The state at the time of the call.
     */
    State state() const;

    /**
     * @brief Check if this channel holds a sound (Ready, Active or Stopping).
     * @return true if the channel has a loaded file that has not been closed.
     */
    bool isActive() const;

    /**
     * @brief True when the loaded file is served from the in-memory VFS (PSRAM).
     *
     * PSRAM-backed channels refill via memcpy and never block on the SD/FATFS
     * lock, so the AudioEngine services them from a separate high-priority
     * reader task to avoid head-of-line blocking behind slow SD reads.
     * Meaningful only while the channel is not Idle.
     */
    bool isMemoryBacked() const;

    /**
     * @brief Underrun samples of the current sound (reset when a sound is loaded or closed).
     * @return Samples output as silence because the ring buffer was empty before the end of the file.
     */
    uint32_t underruns() const;

    /**
     * @brief Failed or empty file reads since construction (never reset).
     * @return Number of reads that returned no data before the end of the data section.
     */
    uint32_t readFailures() const;

    /**
     * @brief Mixer cycle in which the current sound was started.
     * @return The cycle passed to start(), or 0 if the current sound has not started.
     */
    uint32_t startCycle() const;

    /**
     * @brief True while the channel holds an open file handle.
     *
     * Maintained by the task that owns the file, so it can be read from any task.
     * A one-shot closes its file as soon as its last chunk is buffered.
     */
    bool hasOpenFile() const;

    /**
     * @brief Reader only: check whether the given reader kind owns this channel.
     *
     * Once this returns true, the channel cannot become Idle (and so cannot be reused)
     * until the same reader calls closeIfClosing().
     *
     * @param memory_reader true for the PSRAM reader, false for the SD reader.
     * @return true if the channel is Ready, Active, Stopping or Closing with that backing.
     */
    bool isOwnedBy(bool memory_reader) const;

    /**
     * @brief Reader only: check if the ring buffer needs refilling.
     * @return true if the channel is Ready, Active or Stopping, has not reached the end of a
     *         one-shot, and is below the watermark.
     */
    bool needsRefill() const;

    /**
     * @brief Number of unread samples currently buffered.
     *
     * Lets the reader service the most-starved channel first so a slow
     * read on one channel can't push another into underrun.
     *
     * @return Unread samples in the ring buffer (0..RING_BUFFER_SAMPLES-1).
     */
    size_t bufferedSamples() const { return availableSamples(); }

    /**
     * @brief Reader only: refill the ring buffer from the file.
     *
     * Must be called only by the reader that owns the channel (see isOwnedBy()).
     * Read failures are counted (see readFailures()), not logged.
     * Reads up to MAX_SD_CHUNK_SAMPLES from the current file position and handles
     * EOF looping internally. Stops early if the channel moves to Closing. When a
     * one-shot reaches the end of its data (or a read fails), the end is published
     * and the file is closed at once.
     *
     * @return Samples added to the ring; 0 if the channel is not refillable, the ring
     *         is full, or nothing could be read.
     */
    [[nodiscard]] size_t refillBuffer();

    /**
     * @brief Reader only: close and reset the channel if it is Closing (Closing → Idle).
     * @param memory_reader true for the PSRAM reader, false for the SD reader.
     * @return true if the channel was closed by this call.
     */
    bool closeIfClosing(bool memory_reader);

private:
    /// Ring buffer capacity in samples (16384 samples = 32KB @ 16-bit, ~371ms).
    /// Large slack absorbs SD-reader stalls on big files (magnetic profile).
    /// Allocated in PSRAM (not internal RAM) — see allocateRingBuffer().
    static constexpr size_t RING_BUFFER_SAMPLES = 16384;

    /// Watermark threshold: refill when available samples drop below this.
    static constexpr size_t REFILL_WATERMARK = 8192;

    /// Max samples per single SD read (4096 = ~93ms). Caps how long one
    /// refillBuffer() holds the FATFS lock so the SD reader yields frequently
    /// and can interleave other starved SD channels between chunks. Sized so
    /// the reader can keep a long SD sound (e.g. a 2s retraction) topped up
    /// even while sharing time with a concurrent heavy read.
    static constexpr size_t MAX_SD_CHUNK_SAMPLES = 4096;

    /// Initial samples pre-loaded for SD-backed files at load() time (~93ms).
    /// Kept small so triggering a sound (e.g., a sudden loud effect) holds the SD
    /// lock only briefly; the reader task tops the ring up afterwards.
    /// Memory-backed files ignore this and pre-fill the whole ring (memcpy).
    static constexpr size_t INITIAL_PREFILL_SAMPLES = 4096;

    /// Volume precision: 14-bit (0–16384).
    static constexpr uint16_t MAX_VOLUME = 16384;

    /// Linear stop fade per sample: full scale to 0 in 256 samples (~5.8ms, one mixer cycle).
    static constexpr uint16_t STOP_FADE_STEP = MAX_VOLUME / 256;

    /// The status word packs the State with the backing flag (so one acquire load gives a
    /// reader both the state and its owner), the stop request (valid only in Loading) and
    /// the start request a Ready channel is armed for.
    static constexpr uint32_t STATE_MASK = 0x07;
    static constexpr uint32_t ARMED_BIT = 0x08;
    static constexpr uint32_t STOP_REQUESTED_BIT = 0x40;
    static constexpr uint32_t MEMORY_BACKED_BIT = 0x80;
    static constexpr uint32_t REQUEST_SHIFT = 8;
    static constexpr uint32_t REQUEST_MASK = 0x1F << REQUEST_SHIFT;

    struct FileCloser {
        void operator()(FILE* file) const { static_cast<void>(std::fclose(file)); }
    };

    struct RingBufferFree {
        void operator()(int16_t* samples) const;
    };

    using RingBuffer = std::unique_ptr<int16_t[], RingBufferFree>;

    // --- Lifecycle ---
    std::atomic<uint32_t> _status;

    // --- Owned by the current owner (see the class table) ---
    bool _loop_enabled;
    std::string _file_path;
    std::unique_ptr<FILE, FileCloser> _file;
    WavHeader _wav_header;
    uint32_t _file_position;        ///< Current read position in data section (bytes)

    // --- Ring Buffer (allocated in PSRAM) ---
    RingBuffer _ring_buffer;            ///< PSRAM-backed, RING_BUFFER_SAMPLES capacity
    std::atomic<size_t> _write_index;   ///< Next write position (owner; released after the samples)
    std::atomic<size_t> _read_index;    ///< Next read position (mixer; released after consuming)
    std::atomic<bool> _eof;             ///< One-shot fully buffered (released after _write_index)
    std::atomic<bool> _file_open;       ///< Mirrors _file != nullptr (owner writes)

    // --- Volume ---
    std::atomic<uint16_t> _target_volume;
    uint16_t _current_volume;

    // --- Statistics and diagnostics ---
    // Relaxed is enough: no other data is published or read through these values; the mixer
    // only reads its own writes of _group, and resets are ordered by the _status hand-offs.
    std::atomic<uint32_t> _underrun_count;
    std::atomic<uint32_t> _read_failures;
    std::atomic<uint32_t> _start_cycle;
    std::atomic<uint8_t> _group;
    std::atomic<uint32_t> _data_size;

    // --- Mixer cycle snapshot (mixer task only) ---
    State _mix_state;
    bool _mix_eof;
    size_t _mix_read;
    size_t _mix_write;
    uint16_t _mix_target;
    uint32_t _mix_underruns;

    static State stateOf(uint32_t status) { return static_cast<State>(status & STATE_MASK); }
    static bool isArmedFor(uint32_t status, uint8_t request);

    static RingBuffer allocateRingBuffer();

    bool transition(State from, State to);

    // Warning: the caller must own the channel (claimant in Loading, owning reader otherwise).
    void closeFile();

    // Warning: the caller must own the channel (claimant in Loading, owning reader in Closing).
    void release();

    void abortLoad();

    // Warning: the caller must own the channel. A failed seek is counted as a read failure.
    [[nodiscard]] bool seekToData(FILE* file);

    /**
     * @brief Parse and validate a WAV file header.
     * @param file Open file handle positioned at byte 0.
     * @param[out] header Parsed header data.
     * @return esp_err_t ESP_OK if valid 16-bit mono PCM WAV.
     */
    static esp_err_t parseWavHeader(FILE* file, WavHeader& header);

    /**
     * @brief Calculate the number of samples available in the ring buffer.
     * @return Number of unread samples.
     */
    size_t availableSamples() const;

    /**
     * @brief Apply volume ramping for one sample.
     *
     * Updates _current_volume by (delta / 256) per sample call, converging to the
     * cycle target over ~5ms at 44.1kHz; while Stopping, fades linearly to 0.
     */
    void updateVolumeRamp();

    /**
     * @brief Read samples from file into a destination buffer, handling EOF/loop.
     * @param dest Destination buffer.
     * @param samples_requested Number of samples to read.
     * @param[out] eof Set to true when a one-shot reaches the end of its data or a read fails.
     * @return Number of samples actually read.
     */
    size_t readFromFile(int16_t* dest, size_t samples_requested, bool& eof);
};

} // namespace Espressif::Wrappers::Audio
