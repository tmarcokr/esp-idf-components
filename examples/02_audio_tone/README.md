# 02 Audio Tone

Plays a continuous 440 Hz sine wave through `I2sTransmitter` to an I2S amplifier such as the
MAX98357A. No SD card is needed.

## Targets and pins

| Target | BCLK | WS (LRC) | DOUT (DIN) |
|:--|:--|:--|:--|
| ESP32 | GPIO 26 | GPIO 25 | GPIO 22 |
| ESP32-S3 | GPIO 18 | GPIO 19 | GPIO 20 |
| ESP32-C6 | GPIO 18 | GPIO 19 | GPIO 20 |

- **ESP32-S3 DevKitC-1:** GPIO 19 and 20 are the native USB D- and D+ pins. The USB-Serial-JTAG port
  drops as soon as I2S starts, so flash and monitor through the UART port of the board.
- **ESP32:** avoid GPIO 6 to 11 (flash), 16 and 17 (PSRAM on WROVER modules) and 34 to 39 (input only)
  for anything you add.

## Amplifier wiring

- This example does not drive the MAX98357A SD_MODE pin: keep the breakout's own SD_MODE wiring, which
  leaves the amplifier enabled.
- Power the MAX98357A from 5 V, not 3V3, with 100 µF and 100 nF capacitors close to the amplifier.

## Build

```bash
cd examples/02_audio_tone
idf.py set-target esp32s3   # or esp32, esp32c6
idf.py build flash monitor
```
