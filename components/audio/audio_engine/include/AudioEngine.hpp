#pragma once

#include "esp_err.h"
#include "hal/gpio_types.h" // IWYU pragma: keep
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include "DcBlocker.hpp"
#include "RingMemory.hpp"

namespace Espressif::Wrappers::Audio {

/// Channel identifier type. Valid IDs are >= 0.
using ChannelId = int8_t;

/// Returned when channel allocation fails.
constexpr ChannelId INVALID_CHANNEL = -1;

struct AudioEngineImpl;

/**
 * @brief Polyphonic audio engine for ESP32 with real-time mixing and I2S output.
 *
 * Streams 44.1 kHz 16-bit mono PCM WAV files from any mounted filesystem (an SD card, or
 * memory-backed files under "/mem/"), mixes up to 32 channels and outputs 16-bit PCM via
 * the ESP-IDF I2S standard-mode driver to a DAC/amplifier (e.g., MAX98357A).
 *
 * Supported targets: ESP32, ESP32-S3 and ESP32-C6, ESP-IDF v5.3 or newer. PSRAM is optional.
 *
 * Tasks (created by start(), joined by the destructor):
 * - Mixer (priority 10): DMA-paced, mixes 256 frames per cycle (~172 cycles/s at 44.1 kHz),
 *   runs the pending start requests at the beginning of each cycle and wakes both readers.
 * - PSRAM reader (priority 9): refills and closes the channels whose file is under "/mem/"
 *   (memcpy only, never waits for the SD card).
 * - SD reader (priority 6): refills and closes every other channel, most starved first, in
 *   bounded passes.
 * On dual-core targets (ESP32, ESP32-S3) the three tasks are pinned to core 1; on single-core
 * targets (ESP32-C6, or CONFIG_FREERTOS_UNICORE) they have no core affinity.
 *
 * Channel lifecycle (see AudioChannel::State):
 * Idle → Loading (prepare() or play() claims a free channel; the caller task opens, parses and
 * prefills the file) → Ready (the owning reader keeps the ring topped up) → Active (the mixer
 * starts it at the beginning of a cycle) → Stopping (stop(): fade-out over one cycle) → Closing
 * → Idle (the owning reader closes the file and frees the channel). A one-shot that plays to its
 * end goes from Active to Closing; stop() on a Ready channel goes straight to Closing. One task
 * owns the file of a channel at any time and only the owning reader closes it; the mixer never
 * touches a file.
 *
 * Starting sounds:
 * - play(): prepare() and startGroup() of one channel; audible from the next mixer cycle.
 * - prepare() then startGroup(): every listed channel starts in the same mixer cycle; two or
 *   more members are linked and stay sample-aligned (see startGroup() and Stats::group_holds).
 * - playLinked(): two files prepared and started as a linked pair, e.g. two loops crossfaded
 *   with setChannelVolume().
 *
 * Real-time rule: the mixer and reader tasks never log; problems are counted and read with
 * getStats() and channelInfo(). Only the public methods, in the caller task, and the task
 * start and exit messages log. Outside the engine, ESP-IDF's i2s_channel_write() logs an
 * error if the I2S channel is not enabled; CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT removes
 * such driver messages.
 *
 * Ring buffers and memory budget:
 * - Each channel streams through its own ring of Config::ring_buffer_samples 16-bit samples,
 *   allocated once by init() in the memory chosen by Config::ring_memory. RingMemory::Auto
 *   checks once, in init(), whether the heap has PSRAM. There is no per-channel fallback: a
 *   PSRAM that is present but cannot hold every ring makes init() fail with ESP_ERR_NO_MEM.
 * - Default ring: 16384 samples (32 KB, 371 ms) in PSRAM, 4096 samples (8 KB, 93 ms) in
 *   internal RAM. Valid sizes are powers of two from 2048 to 65536.
 * - Budget: max_channels x ring_buffer_samples x 2 bytes. With the default 9 channels that is
 *   288 KB of PSRAM, or 72 KB of internal RAM taken at init() on a board without PSRAM. Each
 *   internal ring needs one contiguous block.
 * - Without PSRAM, keep max_channels at 6 or fewer while a radio (Wi-Fi, Bluetooth, Thread) is
 *   active; the classic ESP32 splits its internal heap over several regions.
 * - The engine state, the channels and the mixer always live in internal RAM.
 *
 * Thread safety:
 * - play() / prepare() / startGroup() / playLinked() / stop(): lock-free; every channel
 *   has an atomic lifecycle and a single owner of its file at any time (the SD or
 *   PSRAM reader closes it, never the mixer)
 * - setChannelVolume() / setGlobalVolume(): lock-free (atomic writes)
 * - getOutputLevel() / getStats() / channelInfo() / ringBufferSamples() / ringBuffersInPsram():
 *   lock-free reads
 * - init(), start() and the destructor must not run concurrently with any other call.
 * - Warning: a caller task deleted inside prepare(), play() or startGroup() leaves its channel
 *   in Loading, or its start request reserved, for the lifetime of the engine.
 *
 * Known limitation: a ChannelId is reused once its channel is back in Idle, so an id kept after
 * its sound ended can address a newer sound. Callers that keep ids must track their own
 * generation.
 *
 * Usage:
 * @code
 *   AudioEngine::Config cfg = { .bclk_pin = GPIO_NUM_4, .ws_pin = GPIO_NUM_5, .dout_pin = GPIO_NUM_6 };
 *   AudioEngine engine(cfg);
 *   ESP_ERROR_CHECK(engine.init());
 *   ESP_ERROR_CHECK(engine.start());
 *   ChannelId bg_track = engine.play("/sdcard/bg_track.wav", true, 10000);
 *   engine.setChannelVolume(bg_track, 8000);
 *   auto pair = engine.playLinked("/sdcard/low.wav", "/sdcard/high.wav", true, 16384, 0);
 * @endcode
 */
class AudioEngine {
public:
    /**
     * @brief Hardware and engine configuration.
     */
    struct Config {
        gpio_num_t bclk_pin;                    ///< I2S bit clock pin
        gpio_num_t ws_pin;                      ///< I2S word select pin
        gpio_num_t dout_pin;                    ///< I2S data out pin
        gpio_num_t sd_mode_pin  = GPIO_NUM_NC;  ///< MAX98357A SD_MODE pin for anti-pop
        uint32_t sample_rate    = 44100;        ///< Output sample rate (Hz)
        /// Maximum simultaneous channels (up to 32). Their rings take
        /// max_channels x ring_buffer_samples x 2 bytes; without PSRAM keep it at 6 or fewer
        /// while a radio (Wi-Fi, Bluetooth, Thread) is active.
        uint8_t max_channels    = 9;
        uint16_t compressor_gain_threshold = 800; ///< Dynamic range compressor baseline threshold
        DcBlocker::CutoffPreset dc_cutoff = DcBlocker::CutoffPreset::Hz50; ///< High-pass filter cutoff
        RingMemory ring_memory  = RingMemory::Auto; ///< Ring buffer memory (see RingMemory).
        /// Samples per channel ring: 0 selects 16384 in PSRAM or 4096 in internal RAM; otherwise a
        /// power of two from 2048 to 65536. A 4096-sample ring buffers 93 ms (16384: 371 ms), so
        /// SD latency spikes from other tasks show up as underruns sooner; use 8192 when internal
        /// RAM allows.
        uint32_t ring_buffer_samples = 0;
    };

    /**
     * @brief Construct a new Audio Engine.
     * @param config Hardware and engine configuration.
     */
    explicit AudioEngine(const Config& config);

    /**
     * @brief Destroy the Audio Engine, stopping all tasks and freeing resources.
     *
     * Waits until every engine task has exited before anything is freed. If a task does
     * not exit within its timeout, the engine resources are deliberately leaked (and an
     * error is logged) rather than freed under a running task.
     *
     * Warning: blocks the calling task until the tasks exit; with stuck tasks this can take up
     * to about 15 s (5 s per task).
     */
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * @brief Initialize I2S hardware and allocate internal resources.
     *
     * Must be called before start(). Initializes the I2S transmitter
     * and allocates the channel array and mixer.
     *
     * Objects shared with the mixer are allocated in internal RAM. The per-channel ring
     * buffers go where Config::ring_memory says; RingMemory::Auto checks once, here, whether
     * the heap has PSRAM. There is no per-channel fallback: if the chosen memory cannot hold
     * every ring, init() fails. On failure nothing is kept and init() may be called again.
     *
     * Warning: ESP_ERR_NO_MEM covers the internal-RAM objects and the rings. The I2S
     * transmitter object and the mix buffer come from the default heap through new, which
     * aborts on exhaustion in a build without exceptions.
     *
     * @return esp_err_t ESP_OK on success; ESP_ERR_INVALID_ARG if max_channels is 0 or
     *         exceeds 32, or for an invalid ring_memory or ring_buffer_samples;
     *         ESP_ERR_INVALID_STATE if already initialized; ESP_ERR_NO_MEM if internal RAM or
     *         the ring memory is exhausted; an I2S or GPIO error otherwise.
     */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Spawn the mixer and SD reader FreeRTOS tasks.
     *
     * After this call, the engine is actively outputting silence via I2S.
     * Use play() to begin audio playback on channels.
     *
     * @return esp_err_t ESP_OK on success; ESP_ERR_INVALID_STATE if not initialized or
     *         already started; ESP_ERR_NO_MEM if a task cannot be created (the tasks
     *         already created are stopped); a GPIO error if the SD_MODE pin cannot be
     *         driven (the engine then runs with the amplifier still in shutdown).
     */
    [[nodiscard]] esp_err_t start();

    /**
     * @brief Start audio playback on an available channel.
     *
     * Equivalent to prepare() followed by startGroup() with a single channel (not linked):
     * the sound becomes audible at the next mixer cycle. Thread-safe (lock-free).
     *
     * @param file_path SD card path (e.g., "/sdcard/track.wav").
     * @param loop Seamless looping flag (required for background/drone channels).
     * @param initial_volume 14-bit volume (0–16384). Use 0 for crossfaded
     *        channels that need phase-aligned silent startup.
     * @return Channel ID (>= 0) on success, INVALID_CHANNEL on failure.
     */
    ChannelId play(std::string_view file_path,
                   bool loop = false,
                   uint16_t initial_volume = 16384);

    /**
     * @brief Open, parse and prefill @p file_path on a free channel without making it audible.
     *
     * The caller must then either start the channel with startGroup() or release it with
     * stop(); a prepared channel is never started otherwise. Thread-safe (lock-free).
     *
     * @param file_path Filesystem path (e.g., "/sdcard/track.wav").
     * @param loop Seamless looping flag.
     * @param initial_volume 14-bit volume (0–16384).
     * @return Channel ID (>= 0) on success, INVALID_CHANNEL on failure.
     */
    ChannelId prepare(std::string_view file_path,
                      bool loop = false,
                      uint16_t initial_volume = 16384);

    /**
     * @brief Make every prepared channel in @p ids audible from the same mixer cycle.
     *
     * All members start at frame 0 of the same mixer cycle. With two or more members they
     * are linked: when one of them runs short of buffered samples, the whole group is held
     * for that cycle so the members stay sample-aligned (see Stats::group_holds). A member
     * that is stopped leaves the group; the others continue, and a single remaining member
     * runs unlinked. A looping member whose file reads keep failing silences its whole
     * group until it is stopped. Thread-safe (lock-free).
     *
     * @param ids Channel IDs returned by prepare(), each listed once.
     * @return ESP_OK on success; ESP_ERR_INVALID_ARG for an empty list, an invalid or
     *         duplicated id; ESP_ERR_INVALID_STATE if any channel is not prepared (nothing
     *         is started in that case) or the engine is not running; ESP_ERR_NO_MEM if 32
     *         start requests are already pending.
     */
    [[nodiscard]] esp_err_t startGroup(std::span<const ChannelId> ids);

    /**
     * @brief Result of playLinked(); both ids are INVALID_CHANNEL on failure.
     */
    struct LinkedChannels {
        ChannelId first = INVALID_CHANNEL;   ///< Channel playing the first file.
        ChannelId second = INVALID_CHANNEL;  ///< Channel playing the second file.
    };

    /**
     * @brief prepare() both files, then startGroup() them as a linked pair.
     *
     * On any failure it stops every channel it prepared. Logs a warning when the two
     * files differ in length, since looping members of different lengths drift apart by
     * the difference on every loop. Thread-safe (lock-free).
     *
     * @param path_a First file.
     * @param path_b Second file.
     * @param loop Seamless looping flag for both channels.
     * @param volume_a 14-bit initial volume of the first channel.
     * @param volume_b 14-bit initial volume of the second channel.
     * @return The two channel IDs, or both INVALID_CHANNEL on failure.
     */
    LinkedChannels playLinked(std::string_view path_a, std::string_view path_b, bool loop,
                              uint16_t volume_a, uint16_t volume_b);

    /**
     * @brief Stop a channel with a brief fade-out to prevent clicks.
     *
     * The mixer fades the channel to 0 over ~5ms, then its reader task closes the
     * file and frees the channel. Never dropped. Thread-safe (lock-free).
     *
     * Also releases a prepared channel that was never started, and cancels a load in progress
     * (counted in Stats::load_failures).
     *
     * Warning: the id is not checked against the sound it was returned for. Once that sound has
     * ended, its channel can be reused, and a stale id stops the newer sound.
     *
     * @param id Channel ID returned by play(), prepare() or playLinked().
     */
    void stop(ChannelId id);

    /**
     * @brief Update a channel's target volume (thread-safe, lock-free).
     *
     * The volume change is applied gradually via exponential ramping
     * in the mixer task to prevent audible clicks. Ignored while the channel fades out.
     *
     * Warning: like stop(), a stale id addresses whatever sound now uses the channel.
     *
     * @param id Channel ID.
     * @param target_volume 14-bit volume (0–16384).
     */
    void setChannelVolume(ChannelId id, uint16_t target_volume);

    /**
     * @brief Set the global master volume (thread-safe, lock-free).
     *
     * Applies to the mixed output of all channels before I2S write.
     *
     * @param target_volume 14-bit volume (0–16384).
     */
    void setGlobalVolume(uint16_t target_volume);

    /**
     * @brief Get the current RMS output level for LED reactivity.
     *
     * Returns the RMS power level averaged over a 100ms window.
     * Thread-safe (lock-free read).
     *
     * @return 0–16384 (0%–100% of maximum output).
     */
    uint16_t getOutputLevel() const;

    /**
     * @brief Engine counters returned by getStats().
     */
    struct Stats {
        /// Silent samples output while a channel had no buffered data before its end, over all
        /// channels since init(). Includes the tail of a fading channel whose ring ran dry.
        uint32_t underruns = 0;
        uint32_t group_holds = 0;       ///< Mixer cycles in which a linked group was held, counted once per held group.
        uint32_t i2s_write_errors = 0;  ///< Failed I2S writes since start.
        uint32_t load_failures = 0;     ///< prepare() calls whose file could not be loaded or whose load a stop() cancelled, since start.
        uint32_t no_free_channels = 0;  ///< prepare() calls that found every channel busy, since start.
        uint32_t read_failures = 0;     ///< File reads that returned no data before the end, since start.
        uint8_t busy_channels = 0;      ///< Channels not Idle.
        uint8_t open_files = 0;         ///< Channels holding an open file.
        int32_t peak_in = 0;            ///< Max |sum| before DSP since the last call.
        int32_t peak_out = 0;           ///< Max |sample| after DSP since the last call.
        uint32_t clipped_samples = 0;   ///< Samples at full scale after DSP since the last call.
    };

    /**
     * @brief Lock-free snapshot of engine counters; peak fields reset on each call.
     *
     * Callable from any task. The uint32_t counters are cumulative (they wrap at 2^32); peak_in,
     * peak_out and clipped_samples cover the time since the previous call, so the function is
     * intended for a single periodic consumer.
     *
     * @return The counters; all zero before init().
     */
    Stats getStats();

    /**
     * @brief Per-channel snapshot for diagnostics and tests.
     */
    struct ChannelInfo {
        uint8_t state = 0;          ///< 0 Idle, 1 Loading, 2 Ready, 3 Active, 4 Stopping, 5 Closing.
        uint8_t group = 0;          ///< Linked group id; 0 when the channel is not linked.
        /// Mixer cycle in which the current sound started; 0 if not started. The cycle counter
        /// starts at 1 and wraps after 2^32 cycles (about 289 days at 44.1 kHz).
        uint32_t start_cycle = 0;
        uint32_t underruns = 0;     ///< Underrun samples of the current sound.
    };

    /**
     * @brief Lock-free snapshot of one channel; callable from any task.
     * @param id Channel ID.
     * @return The channel snapshot; all zero for an invalid id or before init().
     */
    ChannelInfo channelInfo(ChannelId id) const;

    /**
     * @brief Samples per channel ring chosen by init(); callable from any task (atomic read).
     * @return The ring size, or 0 before a successful init().
     */
    uint32_t ringBufferSamples() const;

    /**
     * @brief Whether init() placed the ring buffers in PSRAM; callable from any task (atomic read).
     * @return true for PSRAM; false for internal RAM or before a successful init().
     */
    bool ringBuffersInPsram() const;

private:
    struct ImplDeleter {
        void operator()(AudioEngineImpl* impl) const;
    };

    std::unique_ptr<AudioEngineImpl, ImplDeleter> _impl; ///< Opaque implementation (PIMPL), internal RAM
};

} // namespace Espressif::Wrappers::Audio
