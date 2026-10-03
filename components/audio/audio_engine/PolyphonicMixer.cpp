#include "PolyphonicMixer.hpp"
#include "AudioChannel.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace Espressif::Wrappers::Audio {

static void publish_max(std::atomic<int32_t>& target, int32_t value) {
    int32_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

int32_t PolyphonicMixer::volumeToCompressorGain(uint16_t q14_volume) const {
    return (static_cast<int32_t>(q14_volume) * _compressor_gain_threshold) / 16384;
}


PolyphonicMixer::PolyphonicMixer(AudioChannel** channels, uint8_t max_channels, uint16_t compressor_gain_threshold, DcBlocker::CutoffPreset dc_cutoff)
    : _channels(channels),
      _max_channels(std::min(max_channels, MAX_CHANNELS)),
      _global_volume(MAX_VOLUME),
      _compressor_gain_threshold(compressor_gain_threshold),
      _compressor(volumeToCompressorGain(MAX_VOLUME)),
      _dc_blocker(dc_cutoff),
      _rms_accumulator(0),
      _rms_sample_count(0),
      _rms_level(0) {}


void PolyphonicMixer::mixFrames(int16_t* output, size_t frame_count) {
    uint32_t mixed_mask = 0;
    for (uint8_t ch = 0; ch < _max_channels; ++ch) {
        if (_channels[ch] && _channels[ch]->beginMixCycle()) {
            mixed_mask |= (1U << ch);
        }
    }

    int32_t peak_in = 0;
    int32_t peak_out = 0;
    uint32_t clipped = 0;

    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t mixed = 0;

        for (uint32_t pending = mixed_mask; pending != 0; pending &= pending - 1) {
            mixed += static_cast<int32_t>(_channels[std::countr_zero(pending)]->getNextSample());
        }

        peak_in = std::max(peak_in, mixed < 0 ? -mixed : mixed);

        // Apply DC blocking filter and square-root-law compression.
        // The master volume is integrated into the compressor gain term to avoid
        // a separate post-mix scaling stage.
        mixed = _dc_blocker.process(mixed);
        int16_t sample = _compressor.process(mixed);

        const int32_t out_sample = sample;
        peak_out = std::max(peak_out, out_sample < 0 ? -out_sample : out_sample);
        if (sample >= 32767 || sample <= -32768) ++clipped;

        output[frame] = sample;

        updateRms(sample);
    }

    uint32_t underruns = 0;
    for (uint32_t pending = mixed_mask; pending != 0; pending &= pending - 1) {
        underruns += _channels[std::countr_zero(pending)]->endMixCycle();
    }

    publish_max(_peak_in, peak_in);
    publish_max(_peak_out, peak_out);
    if (clipped != 0) _clipped_samples.fetch_add(clipped, std::memory_order_relaxed);
    if (underruns != 0) _underruns.fetch_add(underruns, std::memory_order_relaxed);
}

PolyphonicMixer::Stats PolyphonicMixer::takeStats() {
    return Stats{
        .underruns = _underruns.load(std::memory_order_relaxed),
        .peak_in = _peak_in.exchange(0, std::memory_order_relaxed),
        .peak_out = _peak_out.exchange(0, std::memory_order_relaxed),
        .clipped_samples = _clipped_samples.exchange(0, std::memory_order_relaxed),
    };
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
