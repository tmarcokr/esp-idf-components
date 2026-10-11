# SD Card Component

C++ wrapper designed to manage the mounting and basic interaction with SD and MicroSD cards over SPI or SDMMC (1-bit / 4-bit) using ESP-IDF FATFS.

## Features
- **RAII Compliance:** Mounts the SD card upon initialization and safely unmounts it automatically when the object is destroyed or `deinit()` is called.
- **Simple File API:** Allows writing (`writeFile`) and reading (`readFile`) full file contents intuitively using `std::string`.
- **SPI mode (`HostMode::SPI`, default):** Uses the `miso`, `mosi`, `sck` and `cs` pins. Configures MISO, MOSI and CS in "Pull-Up" mode, initializes the SPI2 bus at up to 20 MHz and mounts through the `sdspi_host` driver. `deinit()` also frees the SPI bus.
- **SDMMC mode (`HostMode::SDMMC_1BIT` / `HostMode::SDMMC_4BIT`):** Uses the SDMMC host with the 1-bit or 4-bit host flag. Uses the `clk`, `cmd` and `d0` pins; `d1`, `d2` and `d3` are used in 4-bit mode only. Requires a target with an SDMMC host (for example ESP32-S3); on targets without one (for example ESP32-C6) `init()` logs that SDMMC is not supported and returns `ESP_ERR_NOT_SUPPORTED`, so use SPI mode there.

## Basic Usage

```cpp
#include "sd_card.hpp"

Espressif::Wrappers::SdCard::Config sd_cfg = {
    .miso = GPIO_NUM_4, 
    .mosi = GPIO_NUM_11, 
    .sck = GPIO_NUM_7, 
    .cs = GPIO_NUM_10,
    .mount_point = "/sdcard", 
    .max_files = 5, 
    .format_if_mount_failed = false
};

Espressif::Wrappers::SdCard sd(sd_cfg);

if (sd.init() == ESP_OK) {
    // Write file
    sd.writeFile("/test.txt", "Hello world from ESP32!");
    
    // Read file
    std::string content;
    sd.readFile("/test.txt", content);
    
    // Safely unmount
    sd.deinit();
}
```

### SDMMC (1-bit)

```cpp
Espressif::Wrappers::SdCard::Config sd_cfg = {
    .mode = Espressif::Wrappers::SdCard::HostMode::SDMMC_1BIT,
    // Pins are board specific: adjust them to your wiring.
    .clk = GPIO_NUM_7,
    .cmd = GPIO_NUM_9,
    .d0 = GPIO_NUM_8,
    .mount_point = "/sdcard",
    .max_files = 5,
    .format_if_mount_failed = false
};

Espressif::Wrappers::SdCard sd(sd_cfg);
if (sd.init() == ESP_OK) {
    // Same file API as in SPI mode
}
```
