#include "AudioEngine.hpp"
#include "AudioChannel.hpp"
#include "I2sTransmitter.hpp"
#include "PolyphonicMixer.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "driver/gpio.h"
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>

namespace Espressif::Wrappers::Audio {

static constexpr const char* TAG = "AudioEngine";


struct AudioEngineImpl {
    AudioEngine::Config config;

    I2sTransmitter* i2s          = nullptr;
    AudioChannel** channels      = nullptr;
    PolyphonicMixer* mixer       = nullptr;
    int16_t* mix_buffer          = nullptr;

    TaskHandle_t mixer_task      = nullptr;
    TaskHandle_t reader_task     = nullptr;
    TaskHandle_t mem_reader_task = nullptr;
    volatile bool running        = false;

    /// Ready channels the mixer starts at the beginning of its next cycle (bit = channel).
    std::atomic<uint32_t> pending_start{0};

    std::atomic<uint32_t> i2s_write_errors{0};
    std::atomic<uint32_t> load_failures{0};
    std::atomic<uint32_t> no_free_channels{0};
};

static void mixer_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);
    const size_t frame_count = impl->i2s->getFrameCount();

    ESP_LOGI(TAG, "Mixer task started (%lu frames/cycle).",
             static_cast<unsigned long>(frame_count));

    uint32_t cycle = 0;
    while (impl->running) {
        ++cycle;
        uint32_t starting = impl->pending_start.exchange(0, std::memory_order_acq_rel);
        for (; starting != 0; starting &= starting - 1) {
            impl->channels[std::countr_zero(starting)]->start(cycle);
        }

        // Mix all active channels into the output buffer
        impl->mixer->mixFrames(impl->mix_buffer, frame_count);

        // Write to I2S DMA (blocks until DMA buffer available)
        esp_err_t ret = impl->i2s->write(impl->mix_buffer, frame_count);
        if (ret != ESP_OK) {
            impl->i2s_write_errors.fetch_add(1, std::memory_order_relaxed);
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent CPU spinlock on I2S stall
        }

        if (impl->mem_reader_task) {
            xTaskNotifyGive(impl->mem_reader_task);
        }
        if (impl->reader_task) {
            xTaskNotifyGive(impl->reader_task);
        }
    }

    ESP_LOGI(TAG, "Mixer task stopped.");
    vTaskDelete(nullptr);
}

static void close_owned_channels(AudioEngineImpl* impl, bool memory_reader) {
    for (uint8_t i = 0; i < impl->config.max_channels; ++i) {
        AudioChannel* ch = impl->channels[i];
        if (ch) {
            ch->closeIfClosing(memory_reader);
        }
    }
}

static int8_t find_neediest_channel(AudioEngineImpl* impl, bool memory_reader, uint32_t excluded) {
    int8_t neediest = -1;
    size_t lowest_level = SIZE_MAX;
    for (uint8_t i = 0; i < impl->config.max_channels; ++i) {
        AudioChannel* ch = impl->channels[i];
        if (ch == nullptr || (excluded & (1U << i)) != 0) continue;
        if (ch->isOwnedBy(memory_reader) && ch->needsRefill()) {
            const size_t level = ch->bufferedSamples();
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
            if (impl->channels[idx]->refillBuffer() == 0) {
                exhausted |= bit;
            }
        }

        if ((served & ~exhausted) == 0) return;
        if (find_neediest_channel(impl, memory_reader, exhausted) < 0) return;
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

    while (impl->running) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        service_owned_channels(impl, true);
    }

    ESP_LOGI(TAG, "PSRAM reader task stopped.");
    vTaskDelete(nullptr);
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

    while (impl->running) {
        // Block until the mixer task notifies us, or a maximum of 10ms timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        service_owned_channels(impl, false);
    }

    ESP_LOGI(TAG, "SD reader task stopped.");
    vTaskDelete(nullptr);
}


AudioEngine::AudioEngine(const Config& config)
    : _impl(new AudioEngineImpl{.config = config}) {}

AudioEngine::~AudioEngine() {
    if (!_impl) return;

    // Signal tasks to stop
    _impl->running = false;

    // Wait for tasks to terminate
    if (_impl->mixer_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_impl->reader_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_impl->mem_reader_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (_impl->channels) {
        for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
            delete _impl->channels[i];
        }
        delete[] _impl->channels;
    }

    delete _impl->mixer;
    delete[] _impl->mix_buffer;
    delete _impl->i2s;

    // Put MAX98357A in shutdown if sd_mode_pin is configured
    if (_impl->config.sd_mode_pin != GPIO_NUM_NC) {
        gpio_set_level(_impl->config.sd_mode_pin, 0);
    }

    delete _impl;
    _impl = nullptr;

    ESP_LOGI(TAG, "AudioEngine destroyed.");
}


esp_err_t AudioEngine::init() {
    if (!_impl) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Initializing AudioEngine (max_channels=%u, sample_rate=%lu)...",
             _impl->config.max_channels,
             static_cast<unsigned long>(_impl->config.sample_rate));

    if (_impl->config.max_channels > PolyphonicMixer::MAX_CHANNELS) {
        ESP_LOGE(TAG, "max_channels=%u exceeds the supported %u.",
                 _impl->config.max_channels, PolyphonicMixer::MAX_CHANNELS);
        return ESP_ERR_INVALID_ARG;
    }

    I2sTransmitter::Config i2s_cfg = {
        .bclk_pin       = _impl->config.bclk_pin,
        .ws_pin         = _impl->config.ws_pin,
        .dout_pin       = _impl->config.dout_pin,
        .sample_rate    = _impl->config.sample_rate,
        .dma_frame_count = 256,
        .dma_desc_count  = 4
    };
    _impl->i2s = new I2sTransmitter(i2s_cfg);
    esp_err_t ret = _impl->i2s->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    _impl->channels = new AudioChannel*[_impl->config.max_channels];
    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        _impl->channels[i] = new AudioChannel();
    }

    _impl->mixer = new PolyphonicMixer(_impl->channels, _impl->config.max_channels, _impl->config.compressor_gain_threshold, _impl->config.dc_cutoff);

    _impl->mix_buffer = new int16_t[_impl->i2s->getFrameCount()];
    std::memset(_impl->mix_buffer, 0, _impl->i2s->getFrameCount() * sizeof(int16_t));

    if (_impl->config.sd_mode_pin != GPIO_NUM_NC) {
        gpio_config_t log_cfg = {
            .pin_bit_mask = (1ULL << _impl->config.sd_mode_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&log_cfg);
        gpio_set_level(_impl->config.sd_mode_pin, 0); // Keep in shutdown
        ESP_LOGI(TAG, "SD_MODE pin initialized to LOW (Shutdown).");
    }

    ESP_LOGI(TAG, "AudioEngine initialized successfully.");
    return ESP_OK;
}


esp_err_t AudioEngine::start() {
    if (!_impl || !_impl->i2s) return ESP_ERR_INVALID_STATE;

    _impl->running = true;

#if CONFIG_IDF_TARGET_ESP32S3
    constexpr int kAudioCore = 1;
#else
    constexpr int kAudioCore = tskNO_AFFINITY;
#endif

    BaseType_t result = xTaskCreatePinnedToCore(
        mixer_task_func,
        "audio_mixer",
        4096,
        _impl,
        10,     // Priority 10 (highest audio)
        &_impl->mixer_task,
        kAudioCore
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mixer task.");
        _impl->running = false;
        return ESP_ERR_NO_MEM;
    }

    result = xTaskCreatePinnedToCore(
        sd_reader_task_func,
        "audio_sd_reader",
        8192,
        _impl,
        6,      // Priority 6 (below mixer=10, above app tasks) — keep SD refills timely
        &_impl->reader_task,
        kAudioCore
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SD reader task.");
        _impl->running = false;
        return ESP_ERR_NO_MEM;
    }

    result = xTaskCreatePinnedToCore(
        mem_reader_task_func,
        "audio_mem_reader",
        4096,
        _impl,
        9,      // Priority 9 (just below mixer=10, above SD reader=6)
        &_impl->mem_reader_task,
        kAudioCore
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PSRAM reader task.");
        _impl->running = false;
        return ESP_ERR_NO_MEM;
    }

    if (_impl->config.sd_mode_pin != GPIO_NUM_NC) {
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(_impl->config.sd_mode_pin, 1);
        ESP_LOGI(TAG, "Audio output enabled via SD_MODE (anti-pop complete).");
    }

    ESP_LOGI(TAG, "AudioEngine started (mixer + SD reader tasks running).");
    return ESP_OK;
}


ChannelId AudioEngine::play(std::string_view file_path, bool loop, uint16_t initial_volume) {
    if (!_impl || !_impl->running) return INVALID_CHANNEL;

    ChannelId slot = INVALID_CHANNEL;
    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        if (_impl->channels[i] && _impl->channels[i]->claim()) {
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

    esp_err_t ret = _impl->channels[slot]->load(file_path, loop, initial_volume);
    if (ret != ESP_OK) {
        _impl->load_failures.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGE(TAG, "Failed to load: %.*s (err=%s)",
                 static_cast<int>(file_path.size()), file_path.data(),
                 esp_err_to_name(ret));
        return INVALID_CHANNEL;
    }

    _impl->pending_start.fetch_or(1U << slot, std::memory_order_release);

    ESP_LOGD(TAG, "Playing [ch%d]: %.*s (loop=%d, vol=%u)",
             slot, static_cast<int>(file_path.size()), file_path.data(),
             loop, initial_volume);
    return slot;
}

void AudioEngine::stop(ChannelId id) {
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return;

    _impl->channels[id]->requestStop();

    ESP_LOGD(TAG, "Stopping channel %d (fade-out scheduled).", id);
}

void AudioEngine::setChannelVolume(ChannelId id, uint16_t target_volume) {
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return;

    if (_impl->channels[id]) {
        _impl->channels[id]->setTargetVolume(target_volume);
    }
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
    stats.peak_in = mix.peak_in;
    stats.peak_out = mix.peak_out;
    stats.clipped_samples = mix.clipped_samples;
    stats.i2s_write_errors = _impl->i2s_write_errors.load(std::memory_order_relaxed);
    stats.load_failures = _impl->load_failures.load(std::memory_order_relaxed);
    stats.no_free_channels = _impl->no_free_channels.load(std::memory_order_relaxed);

    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        const AudioChannel* ch = _impl->channels[i];
        if (!ch) continue;
        stats.read_failures += ch->readFailures();
        if (ch->state() != AudioChannel::State::Idle) ++stats.busy_channels;
        if (ch->hasOpenFile()) ++stats.open_files;
    }
    return stats;
}

AudioEngine::ChannelInfo AudioEngine::channelInfo(ChannelId id) const {
    ChannelInfo info;
    if (!_impl || !_impl->channels || id < 0 || id >= _impl->config.max_channels) return info;

    const AudioChannel* ch = _impl->channels[id];
    if (!ch) return info;
    info.state = static_cast<uint8_t>(ch->state());
    info.start_cycle = ch->startCycle();
    info.underruns = ch->underruns();
    return info;
}

} // namespace Espressif::Wrappers::Audio
