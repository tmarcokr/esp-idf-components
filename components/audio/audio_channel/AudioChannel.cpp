#include "AudioChannel.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Espressif::Wrappers::Audio {

static constexpr const char* TAG = "AudioChannel";


AudioChannel::AudioChannel()
    : _active(false),
      _loop_enabled(false),
      _file(nullptr),
      _wav_header{},
      _file_position(0),
      _ring_buffer{},
      _write_index(0),
      _read_index(0),
      _target_volume(0),
      _current_volume(0) {}

AudioChannel::~AudioChannel() {
    release();
}


esp_err_t AudioChannel::load(std::string_view path, bool loop, uint16_t initial_volume) {
    release();

    _file_path = std::string(path);

    _file = fopen(_file_path.c_str(), "rb");
    if (!_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", _file_path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = parseWavHeader(_file, _wav_header);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid WAV header in: %s", _file_path.c_str());
        fclose(_file);
        _file = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "Loaded: %s (%luHz, %u-bit, %u-ch, data: %lu bytes)",
             _file_path.c_str(),
             static_cast<unsigned long>(_wav_header.sample_rate),
             _wav_header.bits_per_sample,
             _wav_header.num_channels,
             static_cast<unsigned long>(_wav_header.data_size));

    _loop_enabled = loop;
    _file_position = 0;
    _write_index = 0;
    _read_index = 0;
    _target_volume.store(std::min(initial_volume, MAX_VOLUME));
    _current_volume = _target_volume.load();

    fseek(_file, static_cast<long>(_wav_header.data_offset), SEEK_SET);

    // Pre-fill the entire ring buffer
    int16_t temp[RING_BUFFER_SAMPLES];
    size_t filled = readFromFile(temp, RING_BUFFER_SAMPLES);
    for (size_t i = 0; i < filled; ++i) {
        _ring_buffer[i] = temp[i];
    }
    _write_index = filled % RING_BUFFER_SAMPLES;
    _read_index = 0;

    _active = true;
    return ESP_OK;
}

void AudioChannel::release() {
    _active = false;

    if (_file) {
        fclose(_file);
        _file = nullptr;
    }

    _write_index = 0;
    _read_index = 0;
    _file_position = 0;
    _current_volume = 0;
    _target_volume.store(0);
    _loop_enabled = false;
    _file_path.clear();
    std::memset(_ring_buffer, 0, sizeof(_ring_buffer));
}


int16_t AudioChannel::getNextSample() {
    if (!_active) return 0;

    if (_read_index == _write_index) {
        // Buffer underrun — output silence but don't deactivate
        ESP_LOGD(TAG, "Buffer underrun on: %s", _file_path.c_str());
        return 0;
    }

    int16_t sample = _ring_buffer[_read_index];
    _read_index = (_read_index + 1) % RING_BUFFER_SAMPLES;

    updateVolumeRamp();

    int32_t scaled = (static_cast<int32_t>(sample) * static_cast<int32_t>(_current_volume)) >> 14;
    return static_cast<int16_t>(scaled);
}

void AudioChannel::setTargetVolume(uint16_t volume) {
    _target_volume.store(std::min(volume, MAX_VOLUME));
}


void AudioChannel::updateVolumeRamp() {
    uint16_t target = _target_volume.load();

    if (_current_volume == target) return;

    int32_t delta = static_cast<int32_t>(target) - static_cast<int32_t>(_current_volume);
    int32_t step = delta / 256; // Exponential decay constant (~5ms at 44.1kHz)

    // Ensure minimum step size to avoid stalling
    if (step == 0) {
        step = (delta > 0) ? 1 : -1;
    }

    int32_t new_volume = static_cast<int32_t>(_current_volume) + step;
    _current_volume = static_cast<uint16_t>(std::clamp(new_volume, int32_t{0}, static_cast<int32_t>(MAX_VOLUME)));
}


size_t AudioChannel::availableSamples() const {
    size_t w = _write_index;
    size_t r = _read_index;

    if (w >= r) {
        return w - r;
    }
    return RING_BUFFER_SAMPLES - r + w;
}

bool AudioChannel::needsRefill() const {
    if (!_active || !_file) return false;
    return availableSamples() < REFILL_WATERMARK;
}

esp_err_t AudioChannel::refillBuffer() {
    if (!_active || !_file) return ESP_ERR_NOT_FOUND;

    size_t available = availableSamples();
    size_t free_space = RING_BUFFER_SAMPLES - 1 - available; // -1 to distinguish full from empty

    if (free_space == 0) return ESP_OK;

    size_t to_read = std::min(free_space, REFILL_WATERMARK);
    int16_t temp[RING_BUFFER_SAMPLES];
    size_t samples_read = readFromFile(temp, to_read);

    if (samples_read == 0 && !_loop_enabled) {
        // One-shot channel reached EOF — will be deactivated when buffer drains
        return ESP_OK;
    }

    for (size_t i = 0; i < samples_read; ++i) {
        _ring_buffer[_write_index] = temp[i];
        _write_index = (_write_index + 1) % RING_BUFFER_SAMPLES;
    }

    return ESP_OK;
}


size_t AudioChannel::readFromFile(int16_t* dest, size_t samples_requested) {
    if (!_file || samples_requested == 0) return 0;

    size_t total_read = 0;

    while (total_read < samples_requested) {
        // How many bytes remain in the data section?
        uint32_t bytes_remaining = _wav_header.data_size - _file_position;
        if (bytes_remaining == 0) {
            if (_loop_enabled) {
                // Seamless loop: seek back to data start
                fseek(_file, static_cast<long>(_wav_header.data_offset), SEEK_SET);
                _file_position = 0;
                bytes_remaining = _wav_header.data_size;
            } else {
                // One-shot: mark as inactive when buffer drains
                _active = false;
                break;
            }
        }

        size_t samples_remaining_in_file = bytes_remaining / sizeof(int16_t);
        size_t to_read = std::min(samples_requested - total_read, samples_remaining_in_file);

        size_t actually_read = fread(dest + total_read, sizeof(int16_t), to_read, _file);
        if (actually_read == 0) {
            ESP_LOGW(TAG, "Unexpected read failure on: %s", _file_path.c_str());
            break;
        }

        total_read += actually_read;
        _file_position += static_cast<uint32_t>(actually_read * sizeof(int16_t));
    }

    return total_read;
}


esp_err_t AudioChannel::parseWavHeader(FILE* file, WavHeader& header) {
    if (!file) return ESP_ERR_INVALID_ARG;

    fseek(file, 0, SEEK_SET);

    // --- RIFF header ---
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];

    if (fread(riff_id, 1, 4, file) != 4) return ESP_ERR_INVALID_SIZE;
    if (fread(&riff_size, 4, 1, file) != 1) return ESP_ERR_INVALID_SIZE;
    if (fread(wave_id, 1, 4, file) != 4) return ESP_ERR_INVALID_SIZE;

    if (std::memcmp(riff_id, "RIFF", 4) != 0 || std::memcmp(wave_id, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a valid RIFF/WAVE file.");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // --- Search for fmt and data chunks ---
    bool found_fmt = false;
    bool found_data = false;

    while (!found_fmt || !found_data) {
        char chunk_id[4];
        uint32_t chunk_size;

        if (fread(chunk_id, 1, 4, file) != 4) break;
        if (fread(&chunk_size, 4, 1, file) != 1) break;

        if (std::memcmp(chunk_id, "fmt ", 4) == 0) {
            // Format chunk
            uint16_t audio_format;
            if (fread(&audio_format, 2, 1, file) != 1) return ESP_ERR_INVALID_SIZE;

            if (audio_format != 1) {
                ESP_LOGE(TAG, "Unsupported audio format: %u (expected PCM=1)", audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }

            if (fread(&header.num_channels, 2, 1, file) != 1) return ESP_ERR_INVALID_SIZE;
            if (fread(&header.sample_rate, 4, 1, file) != 1) return ESP_ERR_INVALID_SIZE;

            uint32_t byte_rate;
            uint16_t block_align;
            if (fread(&byte_rate, 4, 1, file) != 1) return ESP_ERR_INVALID_SIZE;
            if (fread(&block_align, 2, 1, file) != 1) return ESP_ERR_INVALID_SIZE;
            if (fread(&header.bits_per_sample, 2, 1, file) != 1) return ESP_ERR_INVALID_SIZE;

            // Skip any extra fmt bytes
            long extra = static_cast<long>(chunk_size) - 16;
            if (extra > 0) fseek(file, extra, SEEK_CUR);

            // Validate constraints
            if (header.num_channels != 1) {
                ESP_LOGE(TAG, "Unsupported channel count: %u (expected mono)", header.num_channels);
                return ESP_ERR_NOT_SUPPORTED;
            }
            if (header.bits_per_sample != 16) {
                ESP_LOGE(TAG, "Unsupported bit depth: %u (expected 16)", header.bits_per_sample);
                return ESP_ERR_NOT_SUPPORTED;
            }

            found_fmt = true;

        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            header.data_offset = static_cast<uint32_t>(ftell(file));
            header.data_size = chunk_size;
            found_data = true;
            // Don't skip — caller will seek to data_offset

        } else {
            // Unknown chunk — skip it
            fseek(file, static_cast<long>(chunk_size), SEEK_CUR);
        }
    }

    if (!found_fmt || !found_data) {
        ESP_LOGE(TAG, "Missing required WAV chunks (fmt=%d, data=%d)", found_fmt, found_data);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

} // namespace Espressif::Wrappers::Audio
