# ESP32 Audio Architecture

This component provides a complete, modular audio engine for the ESP32 family, capable of streaming WAV files from an SD card (or from memory-backed files), mixing multiple tracks in real-time, and outputting high-quality I2S digital audio.

## Features
- **Modular Design:** Divided into `i2s_transmitter` (hardware driver), `audio_channel` (individual track state), and `audio_engine` (polyphonic mixer and orchestrator).
- **Real-Time Polyphony:** Mixes up to 32 simultaneous audio tracks; the real-time tasks never log and never wait for the caller.
- **Sample-Aligned Starts:** Several sounds can start in the same mixer cycle and stay linked (`prepare()` + `startGroup()`, or `playLinked()`).
- **DMA-Driven I2S Output:** Uses the ESP-IDF I2S standard-mode driver (`driver/i2s_std.h`, ESP-IDF v5.3+ and v6.x) to stream audio to a DAC (like the MAX98357A) with virtually zero CPU overhead on the main application thread.
- **Lock-Free Control:** Start, stop and adjust global or per-channel volumes from any task without interrupting playback or causing clicks.
- **PSRAM Optional:** Ring buffers go to PSRAM when the board has it, to internal RAM otherwise.

## Supported Targets

| Target | Ring buffers | Notes |
|:--|:--|:--|
| ESP32-S3 | PSRAM if present, internal RAM otherwise | SD card over SDMMC (any GPIO) or SPI. |
| ESP32 | PSRAM if present, internal RAM otherwise | SDMMC uses fixed IOMUX pins (slot 1: CLK 14, CMD 15, D0 2), or SPI. |
| ESP32-C6 | Internal RAM | No SDMMC host: SD card over SPI only. No FPU: the compressor uses an exact integer square root. |

On dual-core targets (ESP32, ESP32-S3) the audio tasks are pinned to core 1; on single-core targets they have no core affinity.

## Architecture Flow
`SD Card (WAV)` -> `AudioChannel` (ring buffer) -> `PolyphonicMixer` -> `DcBlocker` -> `DynamicRangeCompressor` -> `I2sTransmitter` -> `MAX98357A Amplifier`

`AudioEngine::start()` creates three tasks:

| Task | Priority | Role |
|:--|:--|:--|
| Mixer | 10 | DMA-paced: mixes 256 frames per cycle (~172 cycles/s at 44.1 kHz), starts pending groups at the beginning of a cycle, wakes the readers. |
| PSRAM reader | 9 | Refills and closes channels whose file is under `/mem/` (memcpy only). |
| SD reader | 6 | Refills and closes every other channel, most starved first. |

Each channel follows one lifecycle: `Idle` → `Loading` (the caller opens, parses and prefills the file) → `Ready` → `Active` (started by the mixer) → `Stopping` (fade-out after `stop()`) → `Closing` → `Idle` (the owning reader closes the file). Only one task owns a channel's file at any time, and the mixer never touches files. Problems in the real-time path are counted, not logged: read them with `getStats()` and `channelInfo()`.

## WAV Requirements
PCM WAV, 44.1 kHz, mono, 16-bit. Other formats are rejected at load time. A header that declares more data than the file holds is clamped to the real size (a warning is logged), so a truncated loop wraps at the real end of its data.

## Configuration

| `AudioEngine::Config` field | Default | Meaning |
|:--|:--|:--|
| `bclk_pin`, `ws_pin`, `dout_pin` | — | I2S pins. |
| `sd_mode_pin` | `GPIO_NUM_NC` | MAX98357A SD_MODE pin, held low until `start()` (anti-pop). |
| `sample_rate` | 44100 | Output sample rate (Hz). |
| `max_channels` | 9 | Simultaneous channels, 1 to 32. |
| `compressor_gain_threshold` | 800 | Compressor baseline threshold. |
| `dc_cutoff` | `Hz50` | DC blocker cutoff preset. |
| `ring_memory` | `RingMemory::Auto` | Where the ring buffers live (see below). |
| `ring_buffer_samples` | 0 | Samples per channel ring; 0 selects the default of the chosen memory. |

### Ring Buffer Memory
- `RingMemory::Auto` checks once, in `init()`, whether the heap has PSRAM, and uses it if so; `RingMemory::Psram` and `RingMemory::Internal` force one memory.
- Default size: 16384 samples (32 KB, 371 ms) in PSRAM, 4096 samples (8 KB, 93 ms) in internal RAM. Any other size must be a power of two from 2048 to 65536.
- Budget: `max_channels x ring_buffer_samples x 2 bytes`. With 9 channels: 288 KB of PSRAM, or 72 KB of internal RAM on a board without PSRAM.
- There is no per-channel fallback: if the chosen memory cannot hold every ring (for example, PSRAM present but full), `init()` fails with `ESP_ERR_NO_MEM`.
- Without PSRAM, keep `max_channels` at 6 or fewer while Wi-Fi, Bluetooth or Thread is active. A smaller ring buffers less time, so SD latency spikes from other tasks show up as underruns sooner; use 8192 samples when internal RAM allows.
- `ringBufferSamples()` and `ringBuffersInPsram()` report the choice made by `init()`.

## Basic Usage

```cpp
#include "AudioEngine.hpp"
#include "esp_log.h"

// 1. Configure the I2S pins for your amplifier (e.g., MAX98357A)
Espressif::Wrappers::Audio::AudioEngine::Config audio_cfg = {
    .bclk_pin = GPIO_NUM_18,
    .ws_pin = GPIO_NUM_19,
    .dout_pin = GPIO_NUM_20,
    .sample_rate = 44100,
    .max_channels = 2
};

Espressif::Wrappers::Audio::AudioEngine engine(audio_cfg);

// 2. Initialize and start the background mixing and streaming tasks
if (engine.init() == ESP_OK && engine.start() == ESP_OK) {
    ESP_LOGI("APP", "Audio engine started successfully!");
    
    // 3. Play a WAV file from the SD Card (ensure the SD card is mounted first)
    // Parameters: path, loop_enable, initial_volume (0-16384)
    auto channel_id = engine.play("/sdcard/test.wav", true, 16384);
    
    if (channel_id != Espressif::Wrappers::Audio::INVALID_CHANNEL) {
        // Audio is now playing in the background!
        
        // Example: Lower the volume to 50% safely
        engine.setChannelVolume(channel_id, 8192); 
    }
}
```

## Grouped Start

`prepare()` loads a file without making it audible; `startGroup()` starts every listed channel in the same mixer cycle. Two or more members are linked: if one runs short of buffered samples, the whole group is held for that cycle so the members stay sample-aligned (counted in `Stats::group_holds`). `playLinked()` does both steps for a pair.

```cpp
using namespace Espressif::Wrappers::Audio;

// Two loops of the same length, crossfaded later with setChannelVolume().
ChannelId low = engine.prepare("/sdcard/low.wav", true, 16384);
ChannelId high = engine.prepare("/sdcard/high.wav", true, 0);
const ChannelId group[] = {low, high};
if (low == INVALID_CHANNEL || high == INVALID_CHANNEL || engine.startGroup(group) != ESP_OK) {
    engine.stop(low);
    engine.stop(high);
}

// The same in one call.
AudioEngine::LinkedChannels pair = engine.playLinked("/sdcard/low.wav", "/sdcard/high.wav", true, 16384, 0);
```

A prepared channel stays silent until `startGroup()` starts it or `stop()` releases it. Linked loops of different lengths drift apart by the difference on every loop (`playLinked()` logs a warning).

## Statistics

`getStats()` returns cumulative counters (underruns, group holds, I2S write errors, load failures, read failures) with the busy channels and open files at the time of the call, plus the peaks and clipped samples since the previous call; it is meant for one periodic consumer. `channelInfo(id)` returns the state, group, start cycle and underruns of one channel.

## Notes
- A `ChannelId` is reused once its channel is free again, so an id kept after its sound ended can address a newer sound.
- The destructor joins the three tasks; with a stuck task it waits up to 5 s per task, then leaks the engine instead of freeing it under a running task.
- `DynamicRangeCompressor` is an alias of the class template `BasicDynamicRangeCompressor`, so `class DynamicRangeCompressor;` forward declarations no longer compile; include `DynamicRangeCompressor.hpp` instead.
