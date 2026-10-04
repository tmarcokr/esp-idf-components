#include "AudioEngine.hpp"
#include "AudioChannel.hpp"
#include "I2sTransmitter.hpp"
#include "InternalRam.hpp"
#include "PolyphonicMixer.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Espressif::Wrappers::Audio {

static constexpr const char* TAG = "AudioEngine";

struct TaskSpec {
    const char* name;
    uint32_t stack_bytes;
    UBaseType_t priority;
};

static constexpr TaskSpec kMixerTask{"audio_mixer", 4096, 10};
static constexpr TaskSpec kMemReaderTask{"audio_mem_reader", 4096, 9};
static constexpr TaskSpec kSdReaderTask{"audio_sd_reader", 8192, 6};

#if CONFIG_IDF_TARGET_ESP32S3
static constexpr BaseType_t kAudioCore = 1;
#else
static constexpr BaseType_t kAudioCore = tskNO_AFFINITY;
#endif

static constexpr uint32_t kDmaFrameCount = 256;
static constexpr uint32_t kDmaDescCount = 4;

static constexpr uint32_t kRefillRequest = 1U << 0;
static constexpr uint32_t kStopRequest = 1U << 1;
static constexpr TickType_t kReaderWakePeriod = pdMS_TO_TICKS(10);
static constexpr TickType_t kI2sErrorBackoff = pdMS_TO_TICKS(10);
static constexpr TickType_t kAmpEnableDelay = pdMS_TO_TICKS(50);

// Well above the longest mixer cycle (1 s I2S write timeout plus back-off) and one bounded
// reader pass, so an expired wait means a stuck task, not a slow one.
static constexpr TickType_t kTaskExitTimeout = pdMS_TO_TICKS(5000);


class EngineTask {
public:
    EngineTask() : _exited(xSemaphoreCreateBinaryStatic(&_exited_storage)) {}
    ~EngineTask() { vSemaphoreDelete(_exited); }

    EngineTask(const EngineTask&) = delete;
    EngineTask& operator=(const EngineTask&) = delete;

    bool spawn(TaskFunction_t entry, const TaskSpec& spec, AudioEngineImpl* impl) {
        TaskHandle_t handle = nullptr;
        if (xTaskCreatePinnedToCore(entry, spec.name, spec.stack_bytes, impl, spec.priority, &handle,
                                    kAudioCore) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create task %s.", spec.name);
            return false;
        }
        _handle = handle;
        return true;
    }

    bool isRunning() const { return _handle != nullptr; }

    // Warning: only valid while the task cannot exit on its own (a reader before its stop
    // request, see join()); notifying a deleted task is a use-after-free.
    void notify(uint32_t requests) const { xTaskNotify(_handle, requests, eSetBits); }

    bool join(bool send_stop_request, TickType_t timeout) {
        if (_handle == nullptr) return true;
        if (send_stop_request && !_stop_requested) {
            notify(kStopRequest);
            _stop_requested = true;
        }
        if (xSemaphoreTake(_exited, timeout) != pdTRUE) return false;
        _handle = nullptr;
        _stop_requested = false;
        return true;
    }

    void exitFromTask() {
        const SemaphoreHandle_t exited = _exited;
        // Warning: the engine may free this object as soon as the semaphore is given.
        xSemaphoreGive(exited);
        vTaskDelete(nullptr);
    }

private:
    TaskHandle_t _handle = nullptr;
    bool _stop_requested = false;
    StaticSemaphore_t _exited_storage{};
    SemaphoreHandle_t _exited;
};


struct AudioEngineImpl {
    explicit AudioEngineImpl(const AudioEngine::Config& engine_config) : config(engine_config) {}

    AudioEngine::Config config;

    std::unique_ptr<I2sTransmitter> i2s;
    InternalRam::Array<AudioChannel> channels;
    InternalRam::Ptr<PolyphonicMixer> mixer;
    std::vector<int16_t> mix_buffer;

    EngineTask mixer_task;
    EngineTask mem_reader_task;
    EngineTask sd_reader_task;
    std::atomic<bool> running{false};

    /// Start requests in use (bit = request index), from startGroup() until the mixer has run them.
    std::atomic<uint32_t> requests_in_use{0};
    /// Committed start requests the mixer runs at the beginning of its next cycle.
    std::atomic<uint32_t> committed_requests{0};
    /// Members of each start request; written before the commit, read by the mixer after it.
    uint32_t request_members[PolyphonicMixer::MAX_CHANNELS] = {};
    /// Last linked group id handed out (mixer task only).
    uint8_t last_group_id = 0;

    std::atomic<uint32_t> i2s_write_errors{0};
    std::atomic<uint32_t> load_failures{0};
    std::atomic<uint32_t> no_free_channels{0};
};

static uint8_t next_group_id(AudioEngineImpl* impl) {
    for (;;) {
        if (++impl->last_group_id == 0) impl->last_group_id = 1;
        bool in_use = false;
        for (uint8_t i = 0; i < impl->config.max_channels && !in_use; ++i) {
            in_use = impl->channels[i].group() == impl->last_group_id;
        }
        if (!in_use) return impl->last_group_id;
    }
}

static void run_start_request(AudioEngineImpl* impl, uint8_t request, uint32_t cycle) {
    uint32_t started = 0;
    for (uint32_t pending = impl->request_members[request]; pending != 0; pending &= pending - 1) {
        const int ch = std::countr_zero(pending);
        if (impl->channels[ch].start(request, cycle)) {
            started |= (1U << ch);
        }
    }

    if (std::popcount(started) > 1) {
        const uint8_t group = next_group_id(impl);
        for (uint32_t pending = started; pending != 0; pending &= pending - 1) {
            impl->channels[std::countr_zero(pending)].setGroup(group);
        }
    }

    impl->requests_in_use.fetch_and(~(1U << request), std::memory_order_release);
}

static void mixer_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);
    int16_t* const output = impl->mix_buffer.data();
    const size_t frame_count = impl->mix_buffer.size();

    ESP_LOGI(TAG, "Mixer task started (%lu frames/cycle).",
             static_cast<unsigned long>(frame_count));

    uint32_t cycle = 0;
    while (impl->running.load(std::memory_order_acquire)) {
        ++cycle;
        uint32_t committed = impl->committed_requests.exchange(0, std::memory_order_acq_rel);
        for (; committed != 0; committed &= committed - 1) {
            run_start_request(impl, static_cast<uint8_t>(std::countr_zero(committed)), cycle);
        }

        impl->mixer->mixFrames(output, frame_count);

        if (impl->i2s->write(output, frame_count) != ESP_OK) {
            impl->i2s_write_errors.fetch_add(1, std::memory_order_relaxed);
            vTaskDelay(kI2sErrorBackoff); // Prevent CPU spinlock on I2S stall
        }

        impl->mem_reader_task.notify(kRefillRequest);
        impl->sd_reader_task.notify(kRefillRequest);
    }

    ESP_LOGI(TAG, "Mixer task stopped.");
    impl->mixer_task.exitFromTask();
}

static void close_owned_channels(AudioEngineImpl* impl, bool memory_reader) {
    for (uint8_t i = 0; i < impl->config.max_channels; ++i) {
        impl->channels[i].closeIfClosing(memory_reader);
    }
}

static int8_t find_neediest_channel(AudioEngineImpl* impl, bool memory_reader, uint32_t excluded) {
    int8_t neediest = -1;
    size_t lowest_level = SIZE_MAX;
    for (uint8_t i = 0; i < impl->config.max_channels; ++i) {
        if ((excluded & (1U << i)) != 0) continue;
        const AudioChannel& ch = impl->channels[i];
        if (ch.isOwnedBy(memory_reader) && ch.needsRefill()) {
            const size_t level = ch.bufferedSamples();
            if (level < lowest_level) {
                lowest_level = level;
                neediest = static_cast<int8_t>(i);
            }
        }
    }
    return neediest;
}

// Each pass refills every needy owned channel at most once, most starved first. A refill
// that adds no sample excludes the channel until the next wake, so nothing can spin.
static void service_owned_channels(AudioEngineImpl* impl, bool memory_reader) {
    uint32_t exhausted = 0;
    for (;;) {
        close_owned_channels(impl, memory_reader);

        uint32_t served = 0;
        for (int8_t idx = find_neediest_channel(impl, memory_reader, exhausted);
             idx >= 0;
             idx = find_neediest_channel(impl, memory_reader, exhausted | served)) {
            const uint32_t bit = 1U << idx;
            served |= bit;
            if (impl->channels[idx].refillBuffer() == 0) {
                exhausted |= bit;
            }
        }

        if ((served & ~exhausted) == 0) return;
        if (find_neediest_channel(impl, memory_reader, exhausted) < 0) return;
    }
}

// Warning: a reader exits only on a stop request, never on its own, so the mixer can keep
// notifying it until the mixer itself has been joined (see stop_tasks()).
static void run_reader(AudioEngineImpl* impl, bool memory_reader) {
    for (;;) {
        uint32_t requests = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &requests, kReaderWakePeriod) == pdTRUE &&
            (requests & kStopRequest) != 0) {
            return;
        }
        service_owned_channels(impl, memory_reader);
    }
}

/**
 * @brief PSRAM reader task: runs at priority 9 (just below the mixer).
 *
 * Services only memory-backed channels under /mem. Their refills are memcpy from
 * PSRAM — they never touch the SD/FATFS lock — so this task never blocks and
 * always finishes a pass in microseconds. Running it above the SD reader
 * guarantees PSRAM channels (e.g., looping background tracks) are topped up even while the SD
 * reader is stalled inside a slow blocking read on a different channel.
 * It is the only task that closes and resets the memory-backed channels.
 */
static void mem_reader_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);

    ESP_LOGI(TAG, "PSRAM reader task started.");
    run_reader(impl, true);
    ESP_LOGI(TAG, "PSRAM reader task stopped.");
    impl->mem_reader_task.exitFromTask();
}

/**
 * @brief SD reader task: runs at priority 6, on-demand.
 *
 * Refills the most-starved SD channel first so a slow read on one channel cannot
 * push another into underrun, in bounded passes (see service_owned_channels()).
 * Sleeps (notify/timeout) between wakes to avoid busy-waiting.
 * It is the only task that closes and resets the SD-backed channels.
 */
static void sd_reader_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);

    ESP_LOGI(TAG, "SD reader task started.");
    run_reader(impl, false);
    ESP_LOGI(TAG, "SD reader task stopped.");
    impl->sd_reader_task.exitFromTask();
}

// Warning: the mixer notifies both readers every cycle, so the readers are stopped only
// after the mixer has been joined. Returns false if a task is still running.
static bool stop_tasks(AudioEngineImpl& impl) {
    impl.running.store(false, std::memory_order_release);
    if (!impl.mixer_task.join(false, kTaskExitTimeout)) return false;
    const bool mem_reader_joined = impl.mem_reader_task.join(true, kTaskExitTimeout);
    const bool sd_reader_joined = impl.sd_reader_task.join(true, kTaskExitTimeout);
    return mem_reader_joined && sd_reader_joined;
}

static esp_err_t set_amp_enabled(const AudioEngine::Config& config, bool enabled) {
    if (config.sd_mode_pin == GPIO_NUM_NC) return ESP_OK;
    return gpio_set_level(config.sd_mode_pin, enabled ? 1 : 0);
}

static esp_err_t configure_amp_pin(const AudioEngine::Config& config) {
    if (config.sd_mode_pin == GPIO_NUM_NC) return ESP_OK;

    const gpio_config_t pin_config = {
        .pin_bit_mask = (1ULL << config.sd_mode_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (const esp_err_t ret = gpio_config(&pin_config); ret != ESP_OK) return ret;
    if (const esp_err_t ret = set_amp_enabled(config, false); ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "SD_MODE pin initialized to LOW (Shutdown).");
    return ESP_OK;
}


void AudioEngine::ImplDeleter::operator()(AudioEngineImpl* impl) const {
    InternalRam::Deleter{}(impl);
}

AudioEngine::AudioEngine(const Config& config)
    : _impl(InternalRam::make<AudioEngineImpl, ImplDeleter>(config)) {
    if (!_impl) {
        ESP_LOGE(TAG, "Cannot allocate the engine state in internal RAM.");
    }
}

AudioEngine::~AudioEngine() {
    if (!_impl) return;

    const bool joined = stop_tasks(*_impl);

    if (_impl->i2s) {
        if (const esp_err_t ret = set_amp_enabled(_impl->config, false); ret != ESP_OK) {
            ESP_LOGE(TAG, "Cannot put the amplifier in shutdown: %s", esp_err_to_name(ret));
        }
    }

    if (!joined) {
        ESP_LOGE(TAG, "Audio tasks did not exit; leaking the engine instead of freeing it under them.");
        // Warning: a task may still use the engine state, so it must never be freed.
        static_cast<void>(_impl.release());
        return;
    }

    _impl.reset();
    ESP_LOGI(TAG, "AudioEngine destroyed.");
}


esp_err_t AudioEngine::init() {
    if (!_impl) return ESP_ERR_NO_MEM;
    if (_impl->i2s) return ESP_ERR_INVALID_STATE;

    const Config& config = _impl->config;
    ESP_LOGI(TAG, "Initializing AudioEngine (max_channels=%u, sample_rate=%lu)...",
             config.max_channels, static_cast<unsigned long>(config.sample_rate));

    if (config.max_channels == 0 || config.max_channels > PolyphonicMixer::MAX_CHANNELS) {
        ESP_LOGE(TAG, "max_channels=%u is outside the supported 1..%u.",
                 config.max_channels, PolyphonicMixer::MAX_CHANNELS);
        return ESP_ERR_INVALID_ARG;
    }

    auto i2s = std::make_unique<I2sTransmitter>(I2sTransmitter::Config{
        .bclk_pin        = config.bclk_pin,
        .ws_pin          = config.ws_pin,
        .dout_pin        = config.dout_pin,
        .sample_rate     = config.sample_rate,
        .dma_frame_count = kDmaFrameCount,
        .dma_desc_count  = kDmaDescCount,
    });
    if (const esp_err_t ret = i2s->init(); ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    auto channels = InternalRam::makeArray<AudioChannel>(config.max_channels);
    if (!channels) {
        ESP_LOGE(TAG, "Cannot allocate %u channels in internal RAM.", config.max_channels);
        return ESP_ERR_NO_MEM;
    }

    auto mixer = InternalRam::make<PolyphonicMixer>(std::span<AudioChannel>(channels.get(), config.max_channels),
                                                    config.compressor_gain_threshold, config.dc_cutoff);
    if (!mixer) {
        ESP_LOGE(TAG, "Cannot allocate the mixer in internal RAM.");
        return ESP_ERR_NO_MEM;
    }

    if (const esp_err_t ret = configure_amp_pin(config); ret != ESP_OK) {
        ESP_LOGE(TAG, "SD_MODE pin configuration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    _impl->mix_buffer.assign(i2s->getFrameCount(), 0);
    _impl->i2s = std::move(i2s);
    _impl->channels = std::move(channels);
    _impl->mixer = std::move(mixer);

    ESP_LOGI(TAG, "AudioEngine initialized successfully.");
    return ESP_OK;
}


esp_err_t AudioEngine::start() {
    if (!_impl || !_impl->i2s) return ESP_ERR_INVALID_STATE;

    AudioEngineImpl& impl = *_impl;
    if (impl.mixer_task.isRunning() || impl.mem_reader_task.isRunning() || impl.sd_reader_task.isRunning()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Warning: the readers must exist before the mixer, which notifies them every cycle.
    const bool readers_created = impl.mem_reader_task.spawn(mem_reader_task_func, kMemReaderTask, &impl) &&
                                 impl.sd_reader_task.spawn(sd_reader_task_func, kSdReaderTask, &impl);
    if (readers_created) {
        impl.running.store(true, std::memory_order_release);
    }
    if (!readers_created || !impl.mixer_task.spawn(mixer_task_func, kMixerTask, &impl)) {
        if (!stop_tasks(impl)) {
            ESP_LOGE(TAG, "Audio tasks did not exit after a failed start.");
        }
        return ESP_ERR_NO_MEM;
    }

    if (impl.config.sd_mode_pin != GPIO_NUM_NC) {
        vTaskDelay(kAmpEnableDelay);
        if (const esp_err_t ret = set_amp_enabled(impl.config, true); ret != ESP_OK) {
            ESP_LOGE(TAG, "Cannot enable the amplifier via SD_MODE: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Audio output enabled via SD_MODE (anti-pop complete).");
    }

    ESP_LOGI(TAG, "AudioEngine started (mixer and reader tasks running).");
    return ESP_OK;
}



ChannelId AudioEngine::prepare(std::string_view file_path, bool loop, uint16_t initial_volume) {
    if (!_impl || !_impl->running.load(std::memory_order_acquire)) return INVALID_CHANNEL;

    ChannelId slot = INVALID_CHANNEL;
    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        if (_impl->channels[i].claim()) {
            slot = static_cast<ChannelId>(i);
            break;
        }
    }

    if (slot == INVALID_CHANNEL) {
        _impl->no_free_channels.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "No free channels available for: %.*s",
                 static_cast<int>(file_path.size()), file_path.data());
        return INVALID_CHANNEL;
    }

    esp_err_t ret = _impl->channels[slot].load(file_path, loop, initial_volume);
    if (ret != ESP_OK) {
        _impl->load_failures.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGE(TAG, "Failed to load: %.*s (err=%s)",
                 static_cast<int>(file_path.size()), file_path.data(),
                 esp_err_to_name(ret));
        return INVALID_CHANNEL;
    }

    ESP_LOGD(TAG, "Prepared [ch%d]: %.*s (loop=%d, vol=%u)",
             slot, static_cast<int>(file_path.size()), file_path.data(),
             loop, initial_volume);
    return slot;
}

esp_err_t AudioEngine::startGroup(std::span<const ChannelId> ids) {
    if (!_impl || !_impl->running.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    if (ids.empty() || ids.size() > _impl->config.max_channels) return ESP_ERR_INVALID_ARG;

    uint32_t members = 0;
    for (const ChannelId id : ids) {
        if (id < 0 || id >= _impl->config.max_channels) return ESP_ERR_INVALID_ARG;
        const uint32_t bit = 1U << id;
        if ((members & bit) != 0) return ESP_ERR_INVALID_ARG;
        members |= bit;
    }

    uint32_t in_use = _impl->requests_in_use.load(std::memory_order_acquire);
    uint8_t request = 0;
    for (;;) {
        if (in_use == UINT32_MAX) return ESP_ERR_NO_MEM;
        request = static_cast<uint8_t>(std::countr_zero(~in_use));
        if (_impl->requests_in_use.compare_exchange_weak(in_use, in_use | (1U << request),
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
            break;
        }
    }

    uint32_t armed = 0;
    for (uint32_t pending = members; pending != 0; pending &= pending - 1) {
        const int ch = std::countr_zero(pending);
        if (!_impl->channels[ch].arm(request)) {
            for (uint32_t undo = armed; undo != 0; undo &= undo - 1) {
                _impl->channels[std::countr_zero(undo)].disarm(request);
            }
            _impl->requests_in_use.fetch_and(~(1U << request), std::memory_order_release);
            return ESP_ERR_INVALID_STATE;
        }
        armed |= (1U << ch);
    }

    _impl->request_members[request] = members;
    _impl->committed_requests.fetch_or(1U << request, std::memory_order_release);
    return ESP_OK;
}

AudioEngine::LinkedChannels AudioEngine::playLinked(std::string_view path_a, std::string_view path_b,
                                                    bool loop, uint16_t volume_a, uint16_t volume_b) {
    const ChannelId first = prepare(path_a, loop, volume_a);
    if (first == INVALID_CHANNEL) return {};

    const ChannelId second = prepare(path_b, loop, volume_b);
    if (second == INVALID_CHANNEL) {
        stop(first);
        return {};
    }

    const uint32_t size_a = _impl->channels[first].dataSize();
    const uint32_t size_b = _impl->channels[second].dataSize();
    if (size_a != size_b) {
        ESP_LOGW(TAG, "Linked files differ in length (%lu vs %lu bytes); loops will drift: %.*s, %.*s",
                 static_cast<unsigned long>(size_a), static_cast<unsigned long>(size_b),
                 static_cast<int>(path_a.size()), path_a.data(),
                 static_cast<int>(path_b.size()), path_b.data());
    }

    const ChannelId ids[] = {first, second};
    const esp_err_t ret = startGroup(ids);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start linked pair: %s", esp_err_to_name(ret));
        stop(first);
        stop(second);
        return {};
    }
    return {.first = first, .second = second};
}

ChannelId AudioEngine::play(std::string_view file_path, bool loop, uint16_t initial_volume) {
    const ChannelId id = prepare(file_path, loop, initial_volume);
    if (id == INVALID_CHANNEL) return INVALID_CHANNEL;

    const ChannelId ids[] = {id};
    const esp_err_t ret = startGroup(ids);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start [ch%d]: %s", id, esp_err_to_name(ret));
        stop(id);
        return INVALID_CHANNEL;
    }
    return id;
}

void AudioEngine::stop(ChannelId id) {
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return;

    _impl->channels[id].requestStop();

    ESP_LOGD(TAG, "Stopping channel %d (fade-out scheduled).", id);
}

void AudioEngine::setChannelVolume(ChannelId id, uint16_t target_volume) {
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return;

    _impl->channels[id].setTargetVolume(target_volume);
}

void AudioEngine::setGlobalVolume(uint16_t target_volume) {
    if (!_impl || !_impl->mixer) return;
    _impl->mixer->setGlobalVolume(target_volume);
}

uint16_t AudioEngine::getOutputLevel() const {
    if (!_impl || !_impl->mixer) return 0;
    return _impl->mixer->getOutputLevel();
}

AudioEngine::Stats AudioEngine::getStats() {
    Stats stats;
    if (!_impl || !_impl->channels || !_impl->mixer) return stats;

    const PolyphonicMixer::Stats mix = _impl->mixer->takeStats();
    stats.underruns = mix.underruns;
    stats.group_holds = mix.group_holds;
    stats.peak_in = mix.peak_in;
    stats.peak_out = mix.peak_out;
    stats.clipped_samples = mix.clipped_samples;
    stats.i2s_write_errors = _impl->i2s_write_errors.load(std::memory_order_relaxed);
    stats.load_failures = _impl->load_failures.load(std::memory_order_relaxed);
    stats.no_free_channels = _impl->no_free_channels.load(std::memory_order_relaxed);

    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        const AudioChannel& ch = _impl->channels[i];
        stats.read_failures += ch.readFailures();
        if (ch.state() != AudioChannel::State::Idle) ++stats.busy_channels;
        if (ch.hasOpenFile()) ++stats.open_files;
    }
    return stats;
}

AudioEngine::ChannelInfo AudioEngine::channelInfo(ChannelId id) const {
    ChannelInfo info;
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return info;

    const AudioChannel& ch = _impl->channels[id];
    info.state = static_cast<uint8_t>(ch.state());
    info.group = ch.group();
    info.start_cycle = ch.startCycle();
    info.underruns = ch.underruns();
    return info;
}

} // namespace Espressif::Wrappers::Audio
