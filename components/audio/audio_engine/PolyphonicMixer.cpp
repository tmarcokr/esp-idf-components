#include "PolyphonicMixer.hpp"
#include "AudioChannel.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

namespace Espressif::Wrappers::Audio {

static constexpr const char* TAG = "AudioCalib";

int32_t PolyphonicMixer::volumeToCompressorGain(uint16_t q14_volume) const {
    return (static_cast<int32_t>(q14_volume) * _compressor_gain_threshold) / 16384;
}


PolyphonicMixer::PolyphonicMixer(AudioChannel** channels, uint8_t max_channels, uint16_t compressor_gain_threshold, DcBlocker::CutoffPreset dc_cutoff)
    : _channels(channels),
      _max_channels(max_channels),
      _global_volume(MAX_VOLUME),
      _compressor_gain_threshold(compressor_gain_threshold),
      _compressor(volumeToCompressorGain(MAX_VOLUME)),
      _dc_blocker(dc_cutoff),
      _rms_accumulator(0),
      _rms_sample_count(0),
      _rms_level(0) {}


void PolyphonicMixer::mixFrames(int16_t* output, size_t frame_count) {
    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t mixed = 0;
        uint8_t active = 0;

        for (uint8_t ch = 0; ch < _max_channels; ++ch) {
            if (_channels[ch] && _channels[ch]->isActive()) {
                mixed += static_cast<int32_t>(_channels[ch]->getNextSample());
                ++active;
            }
        }

        // --- CALIBRATION TELEMETRY: raw summed peak, BEFORE any DSP ---
        int32_t raw_abs = mixed < 0 ? -mixed : mixed;
        if (raw_abs > _calib_peak_in) _calib_peak_in = raw_abs;
        if (active > _calib_max_active) _calib_max_active = active;

        // Apply DC blocking filter and square-root-law compression.
        // The master volume is integrated into the compressor gain term to avoid
        // a separate post-mix scaling stage.
        mixed = _dc_blocker.process(mixed);
        int16_t sample = _compressor.process(mixed);

        // --- CALIBRATION TELEMETRY: output peak + clip count, AFTER DSP ---
        int32_t out_abs = sample < 0 ? -sample : sample;
        if (out_abs > _calib_peak_out) _calib_peak_out = out_abs;
        if (sample >= 32767 || sample <= -32768) ++_calib_clip_count;
        ++_calib_total;

        if (_calib_total >= 44100) {  // ~1 second window @ 44.1kHz
            ESP_LOGI(TAG,
                     "in_peak=%ld out_peak=%ld clip=%.3f%% vol_avg=%lu maxCh=%u vol=%ld",
                     static_cast<long>(_calib_peak_in),
                     static_cast<long>(_calib_peak_out),
                     100.0 * static_cast<double>(_calib_clip_count) /
                         static_cast<double>(_calib_total),
                     static_cast<unsigned long>(_compressor.averageVolume()),
                     _calib_max_active,
                     static_cast<long>(_compressor.volume()));
            _calib_peak_in = 0;
            _calib_peak_out = 0;
            _calib_clip_count = 0;
            _calib_total = 0;
            _calib_max_active = 0;
        }

        output[frame] = sample;

        updateRms(sample);
    }
}


void PolyphonicMixer::setGlobalVolume(uint16_t volume) {
    _global_volume = std::min(volume, MAX_VOLUME);
    _compressor.setVolume(volumeToCompressorGain(_global_volume));
}

uint16_t PolyphonicMixer::getOutputLevel() const {
    return _rms_level;
}


void PolyphonicMixer::updateRms(int16_t sample) {
    int32_t s = static_cast<int32_t>(sample);
    _rms_accumulator += static_cast<uint64_t>(s * s);
    ++_rms_sample_count;

    if (_rms_sample_count >= RMS_WINDOW_SAMPLES) {
        // RMS = sqrt(sum_of_squares / N)
        double rms_raw = std::sqrt(static_cast<double>(_rms_accumulator) /
                                   static_cast<double>(_rms_sample_count));

        // Normalize to 0–16384 range (32767 = 100%)
        double normalized = (rms_raw / 32767.0) * static_cast<double>(MAX_VOLUME);
        _rms_level = static_cast<uint16_t>(std::min(normalized, static_cast<double>(MAX_VOLUME)));

        _rms_accumulator = 0;
        _rms_sample_count = 0;
    }
}

} // namespace Espressif::Wrappers::Audio
