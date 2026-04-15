# SD Card Component

C++ wrapper designed to manage the mounting and basic interaction with SD and MicroSD cards over the SPI bus using ESP-IDF FATFS.

## Features
- **RAII Compliance:** Mounts the SD card upon initialization and safely unmounts it automatically when the object is destroyed or `deinit()` is called.
- **Simple File API:** Allows writing (`writeFile`) and reading (`readFile`) full file contents intuitively using `std::string`.
- **SPI Management:** Automatically configures the SPI pins in "Pull-Up" mode for signal stability and initializes the `sdspi_host` driver.

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
