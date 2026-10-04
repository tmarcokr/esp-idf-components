# 08 Audio Stress

Stress test for `AudioEngine` on an SDMMC 1-bit SD card and a MAX98357A amplifier. It writes its own
test sounds to `/sdcard/stress/`, then for `CONFIG_STRESS_DURATION_S` seconds restarts looping pairs,
fires and stops one-shots, runs stop storms and, periodically, a null test (two opposite loops started
in the same mixer cycle must cancel). It then checks that a truncated looping WAV wraps without
underruns, that every channel can be reused, and that 20 engine lifecycles leave the heap stable.
The log ends with `PASS` or `FAIL`; a report line is printed every `CONFIG_STRESS_REPORT_PERIOD_S`.

## Targets and pins

The example needs an SDMMC host: it runs on the ESP32-S3 and the ESP32. On the ESP32-C6 (no SDMMC
host) it logs an error and exits; no pin is used there.

| Target | SD CLK | SD CMD | SD D0 | I2S BCLK | I2S WS (LRC) | I2S DOUT (DIN) | Amp SD_MODE |
|:--|:--|:--|:--|:--|:--|:--|:--|
| ESP32-S3 (DevKitC-1) | GPIO 9 | GPIO 8 | GPIO 7 | GPIO 11 | GPIO 12 | GPIO 13 | GPIO 14 |
| ESP32 | GPIO 14 | GPIO 15 | GPIO 2 | GPIO 26 | GPIO 25 | GPIO 22 | GPIO 21 |
| ESP32-C6 | not used | not used | not used | not used | not used | not used | not used |

On the ESP32 the SD pins are fixed by the IOMUX (SDMMC slot 1) and cannot be changed. The pin map is in
`main/BoardPins.hpp`.

## Pull resistors

- **SD_MODE, every target:** 10 kΩ pull-down to GND. The pin floats from reset until
  `AudioEngine::init()`; a breakout with a pull-up keeps the amplifier on and pops at boot (47 kΩ is
  marginal).
- **ESP32 SDMMC 1-bit:** 10 kΩ pull-ups to 3V3 on CMD (GPIO 15) and D0 (GPIO 2), and 10 kΩ pull-ups at
  the card on DAT1, DAT2 and DAT3. Do **not** wire DAT1, DAT2 or DAT3 to GPIO 4, 12 or 13: DAT2 on
  GPIO 12 (MTDI) selects 1.8 V flash at reset and boot-loops modules with 3.3 V flash. The only escape
  from that state is `espefuse.py set_flash_voltage 3.3V`, a permanent eFuse write.

## ESP32 boot-time risks

1. Flashing fails with "Wrong boot mode detected" while the SD card is attached: the D0 pull-up holds
   GPIO 2 high. Unpower or remove the SD module while flashing, or jumper GPIO 0 to GPIO 2.
2. Boards with an LED on GPIO 2 (many DevKit V1 clones) can break the SD card initialization.
3. SDMMC takes GPIO 14 and 15 (JTAG TMS and TDO): no external JTAG with this example on the ESP32.
4. Do not put peripherals on GPIO 16 and 17: they are the PSRAM pins on WROVER modules and are probed at
   boot because PSRAM support is enabled. GPIO 6 to 11 (flash) and 34 to 39 (input only) stay unused.
5. GPIO 15 low at reset only silences the ROM boot log.

## Power

- Power the MAX98357A from 5 V, not 3V3, with 100 µF and 100 nF capacitors close to the amplifier.
- Put 10 µF and 100 nF capacitors at the SD socket: SD writes draw peaks of 100 to 200 mA.

## Memory and configuration

- `sdkconfig.defaults.esp32s3` enables octal PSRAM (for example on N16R8 modules).
- `sdkconfig.defaults.esp32` enables PSRAM with `CONFIG_SPIRAM_IGNORE_NOTFOUND`, so one build boots on
  modules with PSRAM (WROVER) and without it (WROOM).
- `idf.py menuconfig` → "Audio Stress Test" sets the run time, channel count, volumes, timings and:
  - `STRESS_RING_MEMORY`: ring buffer memory, Auto (PSRAM when present, internal RAM otherwise), PSRAM
    or Internal.
  - `STRESS_RING_SAMPLES`: samples per channel ring; 0 selects 16384 in PSRAM or 4096 in internal RAM,
    any other value must be a power of two from 2048 to 65536.

  To exercise the internal RAM path on a board with PSRAM, select Internal with 4096 samples. The
  report line shows the ring size and memory in use (`engine ring <samples> in <PSRAM|internal RAM>`).
- `STRESS_SD_CONTENTION` adds a task that reads a 2 MiB scratch file in a loop to compete for the SD
  card; underruns are then counted but must not crash the engine.

## Build

```bash
cd examples/08_audio_stress
idf.py set-target esp32s3   # or esp32
idf.py build flash monitor
```
