#include "AudioEngine.hpp"
#include "AudioChannel.hpp"
#include "I2sTransmitter.hpp"
#include "PolyphonicMixer.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
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
    SemaphoreHandle_t chan_mutex  = nullptr;
    volatile bool running        = false;

    /// Channels pending release after fade-out completes
    static constexpr uint8_t MAX_PENDING = 16;
    ChannelId pending_release[MAX_PENDING] = {};
    uint8_t pending_count = 0;
};

static void mixer_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);
    const size_t frame_count = impl->i2s->getFrameCount();

    ESP_LOGI(TAG, "Mixer task started (%lu frames/cycle).",
             static_cast<unsigned long>(frame_count));

    while (impl->running) {
        // Mix all active channels into the output buffer
        impl->mixer->mixFrames(impl->mix_buffer, frame_count);

        // Write to I2S DMA (blocks until DMA buffer available)
        esp_err_t ret = impl->i2s->write(impl->mix_buffer, frame_count);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S write issue: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent CPU spinlock on I2S stall
        }

        // Check for channels pending release (fade-out completed)
        if (impl->pending_count > 0 && xSemaphoreTake(impl->chan_mutex, 0) == pdTRUE) {
            for (uint8_t i = 0; i < impl->pending_count; ++i) {
                ChannelId id = impl->pending_release[i];
                if (id >= 0 && id < impl->config.max_channels) {
                    AudioChannel* ch = impl->channels[id];
                    if (ch) {
                        // Check if volume has ramped down to near-zero
                        // We simply release — the ramp already happened
                        ch->release();
                    }
                }
            }
            impl->pending_count = 0;
            xSemaphoreGive(impl->chan_mutex);
        }
    }

    ESP_LOGI(TAG, "Mixer task stopped.");
    vTaskDelete(nullptr);
}

/**
 * @brief SD reader task: runs at priority 3, on-demand.
 *
 * Scans all active channels and refills their ring buffers when
 * the watermark threshold is crossed. Sleeps between scan cycles
 * to avoid busy-waiting.
 */
static void sd_reader_task_func(void* param) {
    auto* impl = static_cast<AudioEngineImpl*>(param);

    ESP_LOGI(TAG, "SD reader task started.");

    while (impl->running) {
        for (uint8_t i = 0; i < impl->config.max_channels; ++i) {
            AudioChannel* ch = impl->channels[i];
            if (ch && ch->isActive() && ch->needsRefill()) {
                esp_err_t ret = ch->refillBuffer();
                if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(TAG, "Refill error on channel %d: %s", i, esp_err_to_name(ret));
                }
            }
        }

        // Sleep for 1 tick between scan cycles to keep CPU usage low while remaining responsive.
        vTaskDelay(1);
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

    if (_impl->channels) {
        for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
            if (_impl->channels[i]) {
                _impl->channels[i]->release();
                delete _impl->channels[i];
            }
        }
        delete[] _impl->channels;
    }

    delete _impl->mixer;
    delete[] _impl->mix_buffer;
    delete _impl->i2s;

    // Delete mutex
    if (_impl->chan_mutex) {
        vSemaphoreDelete(_impl->chan_mutex);
    }

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

    I2sTransmitter::Config i2s_cfg = {
        .bclk_pin       = _impl->config.bclk_pin,
        .ws_pin         = _impl->config.ws_pin,
        .dout_pin       = _impl->config.dout_pin,
        .sample_rate    = _impl->config.sample_rate,
        .dma_frame_count = 256,
        .dma_desc_count  = 2
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

    _impl->mixer = new PolyphonicMixer(_impl->channels, _impl->config.max_channels);

    _impl->mix_buffer = new int16_t[_impl->i2s->getFrameCount()];
    std::memset(_impl->mix_buffer, 0, _impl->i2s->getFrameCount() * sizeof(int16_t));

    _impl->chan_mutex = xSemaphoreCreateMutex();
    if (!_impl->chan_mutex) {
        ESP_LOGE(TAG, "Failed to create channel mutex.");
        return ESP_ERR_NO_MEM;
    }

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

    BaseType_t result = xTaskCreate(
        mixer_task_func,
        "audio_mixer",
        4096,
        _impl,
        10,     // Priority 10 (highest audio)
        &_impl->mixer_task
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mixer task.");
        _impl->running = false;
        return ESP_ERR_NO_MEM;
    }

    result = xTaskCreate(
        sd_reader_task_func,
        "audio_sd_reader",
        8192,
        _impl,
        3,      // Priority 3 (background I/O)
        &_impl->reader_task
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SD reader task.");
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

    if (xSemaphoreTake(_impl->chan_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGW(TAG, "play(): mutex timeout");
        return INVALID_CHANNEL;
    }

    // Find first inactive channel
    ChannelId slot = INVALID_CHANNEL;
    for (uint8_t i = 0; i < _impl->config.max_channels; ++i) {
        if (_impl->channels[i] && !_impl->channels[i]->isActive()) {
            slot = static_cast<ChannelId>(i);
            break;
        }
    }

    if (slot == INVALID_CHANNEL) {
        ESP_LOGW(TAG, "No free channels available for: %.*s",
                 static_cast<int>(file_path.size()), file_path.data());
        xSemaphoreGive(_impl->chan_mutex);
        return INVALID_CHANNEL;
    }

    // Load the WAV file into the channel
    esp_err_t ret = _impl->channels[slot]->load(file_path, loop, initial_volume);
    xSemaphoreGive(_impl->chan_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load: %.*s (err=%s)",
                 static_cast<int>(file_path.size()), file_path.data(),
                 esp_err_to_name(ret));
        return INVALID_CHANNEL;
    }

    ESP_LOGI(TAG, "Playing [ch%d]: %.*s (loop=%d, vol=%u)",
             slot, static_cast<int>(file_path.size()), file_path.data(),
             loop, initial_volume);
    return slot;
}

void AudioEngine::stop(ChannelId id) {
    if (!_impl || id < 0 || id >= _impl->config.max_channels) return;

    // Set volume to 0 — the ramp will fade out over ~5ms
    _impl->channels[id]->setTargetVolume(0);

    // Schedule release after fade-out (handled in mixer task)
    if (xSemaphoreTake(_impl->chan_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_impl->pending_count < AudioEngineImpl::MAX_PENDING) {
            _impl->pending_release[_impl->pending_count++] = id;
        }
        xSemaphoreGive(_impl->chan_mutex);
    }

    ESP_LOGI(TAG, "Stopping channel %d (fade-out scheduled).", id);
}

void AudioEngine::setChannelVolume(ChannelId id, uint16_t target_volume) {
    if (!_impl || id < 0 || id >= _impl->config.max_channels) return;

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

} // namespace Espressif::Wrappers::Audio
