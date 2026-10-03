#include "AudioChannel.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cstring>

namespace Espressif::Wrappers::Audio {

static constexpr const char* TAG = "AudioChannel";

static constexpr uint8_t toStatus(AudioChannel::State state) {
    return static_cast<uint8_t>(state);
}


AudioChannel::AudioChannel()
    : _status(toStatus(State::Idle)),
      _loop_enabled(false),
      _file(nullptr),
      _wav_header{},
      _file_position(0),
      _min_available_samples(RING_BUFFER_SAMPLES),
      _ring_buffer(static_cast<int16_t*>(
          heap_caps_malloc(RING_BUFFER_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM))),
      _write_index(0),
      _read_index(0),
      _eof(false),
      _target_volume(0),
      _current_volume(0),
      _underrun_count(0),
      _mix_state(State::Idle),
      _mix_eof(false),
      _mix_read(0),
      _mix_write(0),
      _mix_target(0) {
    if (!_ring_buffer) {
        ESP_LOGE(TAG, "Failed to allocate %zu-sample ring buffer in PSRAM.",
                 RING_BUFFER_SAMPLES);
    }
}

AudioChannel::~AudioChannel() {
    release();
    if (_ring_buffer) {
        heap_caps_free(_ring_buffer);
        _ring_buffer = nullptr;
    }
}


AudioChannel::State AudioChannel::state() const {
    return stateOf(_status.load(std::memory_order_acquire));
}

bool AudioChannel::transition(State from, State to) {
    uint8_t status = _status.load(std::memory_order_acquire);
    while (stateOf(status) == from) {
        const auto next = static_cast<uint8_t>((status & MEMORY_BACKED_BIT) | toStatus(to));
        if (_status.compare_exchange_weak(status, next,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

bool AudioChannel::claim() {
    uint8_t expected = toStatus(State::Idle);
    return _status.compare_exchange_strong(expected, toStatus(State::Loading),
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire);
}


esp_err_t AudioChannel::load(std::string_view path, bool loop, uint16_t initial_volume) {
    if (state() != State::Loading) return ESP_ERR_INVALID_STATE;

    if (!_ring_buffer) {
        abortLoad();
        return ESP_ERR_NO_MEM;
    }

    _file_path.assign(path);
    const bool memory_backed = (_file_path.rfind("/mem/", 0) == 0);

    _file = fopen(_file_path.c_str(), "rb");
    if (!_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", _file_path.c_str());
        abortLoad();
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = parseWavHeader(_file, _wav_header);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid WAV header in: %s", _file_path.c_str());
        abortLoad();
        return ret;
    }

    if (loop && _wav_header.data_size < sizeof(int16_t)) {
        ESP_LOGE(TAG, "Cannot loop a WAV without samples: %s", _file_path.c_str());
        abortLoad();
        return ESP_ERR_INVALID_SIZE;
    }

    if ((_status.load(std::memory_order_acquire) & STOP_REQUESTED_BIT) != 0) {
        abortLoad();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Loaded: %s (%luHz, %u-bit, %u-ch, data: %lu bytes)",
             _file_path.c_str(),
             static_cast<unsigned long>(_wav_header.sample_rate),
             _wav_header.bits_per_sample,
             _wav_header.num_channels,
             static_cast<unsigned long>(_wav_header.data_size));

    _loop_enabled = loop;
    _file_position = 0;
    _underrun_count = 0;
    _min_available_samples = RING_BUFFER_SAMPLES;
    const uint16_t volume = std::min(initial_volume, MAX_VOLUME);
    _target_volume.store(volume);
    _current_volume = volume;

    fseek(_file, static_cast<long>(_wav_header.data_offset), SEEK_SET);

    // Pre-fill the ring buffer (leave 1 slot empty to distinguish full from empty).
    // Reads start at index 0 and never exceed capacity, so this is contiguous —
    // read straight into the ring, no temp buffer / no wrap handling needed here.
    // Memory-backed files fill the whole ring instantly (memcpy from PSRAM);
    // SD-backed files pre-fill only a small slice so triggering a sound holds
    // the SD lock briefly, then the reader task tops the ring up.
    const size_t prefill_target = memory_backed ? (RING_BUFFER_SAMPLES - 1)
                                                : INITIAL_PREFILL_SAMPLES;
    bool eof = false;
    const size_t filled = readFromFile(_ring_buffer, prefill_target, eof);
    _read_index.store(0, std::memory_order_release);
    _write_index.store(filled, std::memory_order_release);
    _eof.store(eof, std::memory_order_release);
    if (eof) {
        closeFile();
    }

    uint8_t expected = toStatus(State::Loading);
    const uint8_t backing = memory_backed ? MEMORY_BACKED_BIT : 0;
    if (!_status.compare_exchange_strong(expected, static_cast<uint8_t>(backing | toStatus(State::Ready)),
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
        abortLoad();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void AudioChannel::abortLoad() {
    release();
    _status.store(toStatus(State::Idle), std::memory_order_release);
}

void AudioChannel::closeFile() {
    if (_file) {
        fclose(_file);
        _file = nullptr;
    }
}

void AudioChannel::release() {
    closeFile();

    _file_path.clear();
    _loop_enabled = false;
    _file_position = 0;
    _wav_header = {};
    _min_available_samples = RING_BUFFER_SAMPLES;
    _underrun_count = 0;
    _current_volume = 0;
    _target_volume.store(0);
    _write_index.store(0, std::memory_order_release);
    _read_index.store(0, std::memory_order_release);
    _eof.store(false, std::memory_order_release);
}


void AudioChannel::requestStop() {
    uint8_t status = _status.load(std::memory_order_acquire);
    const State first_seen = stateOf(status);
    for (;;) {
        // Warning: one sound only moves forward through State; a lower state after a failed
        // CAS means the slot was closed and re-claimed, and the new sound must not be stopped.
        if (stateOf(status) < first_seen) return;

        uint8_t desired;
        switch (stateOf(status)) {
            case State::Loading:
                if ((status & STOP_REQUESTED_BIT) != 0) return;
                desired = static_cast<uint8_t>(status | STOP_REQUESTED_BIT);
                break;
            case State::Ready:
                desired = static_cast<uint8_t>((status & MEMORY_BACKED_BIT) | toStatus(State::Closing));
                break;
            case State::Active:
                desired = static_cast<uint8_t>((status & MEMORY_BACKED_BIT) | toStatus(State::Stopping));
                break;
            default:
                return;
        }
        if (_status.compare_exchange_weak(status, desired,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return;
        }
    }
}


bool AudioChannel::start() {
    return transition(State::Ready, State::Active);
}

bool AudioChannel::beginMixCycle() {
    const State current = state();
    if (current != State::Active && current != State::Stopping) return false;

    _mix_state = current;
    _mix_target = _target_volume.load();
    // Warning: _eof must be loaded before _write_index (the owner stores them in the
    // opposite order), or the last chunk of a one-shot could be skipped.
    _mix_eof = _eof.load(std::memory_order_acquire);
    _mix_write = _write_index.load(std::memory_order_acquire);
    _mix_read = _read_index.load(std::memory_order_acquire);
    return true;
}

int16_t AudioChannel::getNextSample() {
    if (_mix_read == _mix_write) {
        if (!_mix_eof) {
            _underrun_count++;
            // Rate-limit warning to once per second of continuous underruns to prevent UART starvation
            if (_underrun_count % 44100 == 1) {
                ESP_LOGW(TAG, "Buffer underrun on: %s (count: %lu)", _file_path.c_str(), static_cast<unsigned long>(_underrun_count));
            }
        }
        return 0;
    }

    int16_t sample = _ring_buffer[_mix_read];
    _mix_read = (_mix_read + 1) % RING_BUFFER_SAMPLES;

    updateVolumeRamp();

    int32_t scaled = (static_cast<int32_t>(sample) * static_cast<int32_t>(_current_volume)) >> 14;
    return static_cast<int16_t>(scaled);
}

void AudioChannel::endMixCycle() {
    _read_index.store(_mix_read, std::memory_order_release);

    const bool drained = (_mix_read == _mix_write);
    if (_mix_state == State::Stopping) {
        if (_current_volume == 0 || drained) {
            transition(State::Stopping, State::Closing);
        }
    } else if (_mix_eof && drained) {
        ESP_LOGI(TAG, "Playback finished: %s. Underruns: %lu",
                 _file_path.c_str(), static_cast<unsigned long>(_underrun_count));
        transition(State::Active, State::Closing);
    }
}

void AudioChannel::setTargetVolume(uint16_t volume) {
    _target_volume.store(std::min(volume, MAX_VOLUME));
}


void AudioChannel::updateVolumeRamp() {
    if (_mix_state == State::Stopping) {
        _current_volume = (_current_volume > STOP_FADE_STEP) ? _current_volume - STOP_FADE_STEP : 0;
        return;
    }

    const uint16_t target = _mix_target;

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


bool AudioChannel::isActive() const {
    const State current = state();
    return current == State::Ready || current == State::Active || current == State::Stopping;
}

bool AudioChannel::isMemoryBacked() const {
    return (_status.load(std::memory_order_acquire) & MEMORY_BACKED_BIT) != 0;
}

bool AudioChannel::isOwnedBy(bool memory_reader) const {
    const uint8_t status = _status.load(std::memory_order_acquire);
    const State current = stateOf(status);
    if (current == State::Idle || current == State::Loading) return false;
    return ((status & MEMORY_BACKED_BIT) != 0) == memory_reader;
}

size_t AudioChannel::availableSamples() const {
    const size_t w = _write_index.load(std::memory_order_acquire);
    const size_t r = _read_index.load(std::memory_order_acquire);

    if (w >= r) {
        return w - r;
    }
    return RING_BUFFER_SAMPLES - r + w;
}

bool AudioChannel::needsRefill() const {
    const State current = state();
    if (current != State::Ready && current != State::Active && current != State::Stopping) return false;
    if (_eof.load(std::memory_order_acquire)) return false;
    return availableSamples() < REFILL_WATERMARK;
}

size_t AudioChannel::refillBuffer() {
    const State current = state();
    if (current != State::Ready && current != State::Active && current != State::Stopping) {
        return 0;
    }
    if (!_file) return 0;

    size_t write = _write_index.load(std::memory_order_acquire);
    const size_t read = _read_index.load(std::memory_order_acquire);
    const size_t available = (write >= read) ? (write - read) : (RING_BUFFER_SAMPLES - read + write);
    if (available < _min_available_samples) {
        _min_available_samples = available;
    }

    size_t free_space = RING_BUFFER_SAMPLES - 1 - available; // -1 to distinguish full from empty

    if (free_space == 0) return 0;

    size_t to_read = std::min({free_space, REFILL_WATERMARK, MAX_SD_CHUNK_SAMPLES});

    // Read directly into the ring buffer at the write index. Split into up to two
    // contiguous reads when the target region wraps past the end of the buffer.
    bool eof = false;
    size_t contiguous = std::min(to_read, RING_BUFFER_SAMPLES - write);
    size_t total_read = readFromFile(_ring_buffer + write, contiguous, eof);
    write = (write + total_read) % RING_BUFFER_SAMPLES;

    if (total_read == contiguous && to_read > contiguous && !eof) {
        const size_t second = readFromFile(_ring_buffer + write, to_read - contiguous, eof);
        write = (write + second) % RING_BUFFER_SAMPLES;
        total_read += second;
    }

    _write_index.store(write, std::memory_order_release);
    if (eof) {
        _eof.store(true, std::memory_order_release);
        closeFile();
    }
    return total_read;
}

bool AudioChannel::closeIfClosing(bool memory_reader) {
    const uint8_t status = _status.load(std::memory_order_acquire);
    if (stateOf(status) != State::Closing) return false;
    if (((status & MEMORY_BACKED_BIT) != 0) != memory_reader) return false;

    release();
    _status.store(toStatus(State::Idle), std::memory_order_release);
    return true;
}


size_t AudioChannel::readFromFile(int16_t* dest, size_t samples_requested, bool& eof) {
    FILE* const file = _file;
    if (!file || samples_requested == 0) return 0;

    size_t total_read = 0;

    while (total_read < samples_requested) {
        if (state() == State::Closing) break;

        // How many bytes remain in the data section?
        uint32_t bytes_remaining = _wav_header.data_size - _file_position;
        if (bytes_remaining < sizeof(int16_t)) {
            if (!_loop_enabled) {
                eof = true;
                break;
            }
            fseek(file, static_cast<long>(_wav_header.data_offset), SEEK_SET);
            _file_position = 0;
            bytes_remaining = _wav_header.data_size;
        }

        size_t samples_remaining_in_file = bytes_remaining / sizeof(int16_t);
        size_t to_read = std::min(samples_requested - total_read, samples_remaining_in_file);

        size_t actually_read = fread(dest + total_read, sizeof(int16_t), to_read, file);
        if (actually_read == 0) {
            ESP_LOGW(TAG, "Unexpected read failure on: %s", _file_path.c_str());
            if (!_loop_enabled) {
                eof = true;
            }
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
            if (header.sample_rate != 44100) {
                ESP_LOGE(TAG, "Unsupported sample rate: %luHz (expected 44100). "
                              "Re-export this WAV at 44.1kHz mono 16-bit.",
                         static_cast<unsigned long>(header.sample_rate));
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
