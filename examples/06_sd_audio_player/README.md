# 06 SD Audio Player

Mounts an SD card over SPI and loops `/sdcard/test.wav` through `AudioEngine` to an I2S amplifier
such as the MAX98357A, while logging a simple output level meter.

The WAV file must be 44.1 kHz, 16-bit, mono PCM.

## Targets and pins

| Target | SD MISO | SD MOSI | SD SCK | SD CS | I2S BCLK | I2S WS (LRC) | I2S DOUT (DIN) |
|:--|:--|:--|:--|:--|:--|:--|:--|
| ESP32 | GPIO 19 | GPIO 23 | GPIO 18 | GPIO 5 | GPIO 26 | GPIO 25 | GPIO 22 |
| ESP32-S3 | GPIO 4 | GPIO 11 | GPIO 7 | GPIO 10 | GPIO 18 | GPIO 19 | GPIO 20 |
| ESP32-C6 | GPIO 4 | GPIO 11 | GPIO 7 | GPIO 10 | GPIO 18 | GPIO 19 | GPIO 20 |

The SD card runs in SPI mode on `SPI2_HOST` (HSPI on the ESP32) at 20 MHz.

- **ESP32-S3 DevKitC-1:** GPIO 19 and 20 are the native USB D- and D+ pins. The USB-Serial-JTAG port
  drops as soon as I2S starts, so flash and monitor through the UART port of the board.
- **ESP32-C6:** the pins above suit the ESP32-C6-DevKitC-1. GPIO 10 and 11 are not bonded on QFN32
  parts (ESP32-C6-MINI-1, ESP32-C6-DevKitM-1): move SD CS and MOSI to free pins on those boards.
- **ESP32:** the SPI signals go through the GPIO matrix at 20 MHz, so keep the SD wires shorter than
  about 10 cm. Do not put peripherals on GPIO 16 and 17 (PSRAM on WROVER modules); GPIO 6 to 11 (flash)
  and 34 to 39 (input only) stay unused.

## Pull resistors

- 10 kΩ pull-ups to 3V3 on SD MISO, MOSI and CS, and on the card's unused DAT1 and DAT2 pins.
- The example leaves `sd_mode_pin` unset, so SD_MODE is not driven. If you wire the MAX98357A SD_MODE
  pin to a GPIO and set `sd_mode_pin`, add a 10 kΩ pull-down to GND on it: the pin floats from reset
  until `AudioEngine::init()`, and a breakout pull-up keeps the amplifier on and pops at boot
  (47 kΩ is marginal).

## Power

- Power the MAX98357A from 5 V, not 3V3, with 100 µF and 100 nF capacitors close to the amplifier.
- Put 10 µF and 100 nF capacitors at the SD socket: SD writes draw peaks of 100 to 200 mA.

## Memory

`sdkconfig.defaults.esp32` enables PSRAM with `CONFIG_SPIRAM_IGNORE_NOTFOUND`, so one build boots on
modules with PSRAM (WROVER) and without it (WROOM). The engine places its ring buffers in PSRAM when
the heap has PSRAM and in internal RAM otherwise; the init log reports the choice. On the ESP32-S3,
`sdkconfig.defaults.esp32s3` enables octal PSRAM (for example on N16R8 modules).

## Build

```bash
cd examples/06_sd_audio_player
idf.py set-target esp32s3   # or esp32, esp32c6
idf.py build flash monitor
```
