#include "PolyphonicMixer.hpp"
#include "AudioChannel.hpp"
#include <algorithm>
#include <cmath>

namespace Espressif::Wrappers::Audio {


PolyphonicMixer::PolyphonicMixer(AudioChannel** channels, uint8_t max_channels)
    : _channels(channels),
      _max_channels(max_channels),
      _global_volume(MAX_VOLUME),
      _rms_accumulator(0),
      _rms_sample_count(0),
      _rms_level(0) {}


void PolyphonicMixer::mixFrames(int16_t* output, size_t frame_count) {
    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t mixed = 0;

        for (uint8_t ch = 0; ch < _max_channels; ++ch) {
            if (_channels[ch] && _channels[ch]->isActive()) {
                mixed += static_cast<int32_t>(_channels[ch]->getNextSample());
            }
        }

        mixed = (mixed * static_cast<int32_t>(_global_volume)) >> 14;

        int16_t sample = applySoftClipping(mixed);

        output[frame] = sample;

        updateRms(sample);
    }
}


void PolyphonicMixer::setGlobalVolume(uint16_t volume) {
    _global_volume = std::min(volume, MAX_VOLUME);
}

uint16_t PolyphonicMixer::getOutputLevel() const {
    return _rms_level;
}


int16_t PolyphonicMixer::applySoftClipping(int32_t mixed) {
    constexpr int32_t KNEE = 24576;    // 75% of 32767 — compression begins here
    constexpr int32_t MAX  = 32767;
    constexpr float LIMIT  = static_cast<float>(MAX - KNEE);

    if (mixed > KNEE) {
        float over = static_cast<float>(mixed - KNEE);
        float compressed = static_cast<float>(KNEE) + LIMIT * (over / (LIMIT + over));
        return static_cast<int16_t>(compressed);
    }
    if (mixed < -KNEE) {
        float over = static_cast<float>(-mixed - KNEE);
        float compressed = static_cast<float>(KNEE) + LIMIT * (over / (LIMIT + over));
        return static_cast<int16_t>(-compressed);
    }
    return static_cast<int16_t>(mixed);
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
