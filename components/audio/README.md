# ESP32 Audio Architecture

This component provides a complete, modular audio engine for the ESP32, capable of streaming WAV files from an SD card, mixing multiple tracks in real-time, and outputting high-quality I2S digital audio.

## Features
- **Modular Design:** Divided into `i2s_transmitter` (hardware driver), `audio_channel` (individual track state), and `audio_engine` (polyphonic mixer and orchestrator).
- **Real-Time Polyphony:** Mixes multiple simultaneous audio tracks smoothly with no stuttering.
- **DMA-Driven I2S Output:** Uses the ESP-IDF v5 I2S API to stream audio to a DAC (like the MAX98357A) with virtually zero CPU overhead on the main application thread.
- **Lock-Free Volume Control:** Adjust global or per-channel volumes safely in real-time without interrupting playback or causing clicks.

## Architecture Flow
`SD Card (WAV)` -> `AudioChannel` -> `PolyphonicMixer` -> `I2sTransmitter` -> `MAX98357A Amplifier`

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