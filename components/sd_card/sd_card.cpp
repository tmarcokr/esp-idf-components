#include "sd_card.hpp"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace Espressif::Wrappers {

static constexpr const char* TAG = "SdCardWrapper";

SdCard::SdCard(const Config& config) 
    : _config(config), _card(nullptr), _mounted(false), _spi_initialized(false) {}

SdCard::~SdCard() {
    (void)deinit();
}

esp_err_t SdCard::init() {
    if (_mounted) return ESP_OK;

    ESP_LOGI(TAG, "Initializing SPI bus for SD (Optimal Mode)...");

    gpio_set_pull_mode(_config.miso, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(_config.mosi, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(_config.cs, GPIO_PULLUP_ONLY);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = _config.mosi;
    bus_cfg.miso_io_num = _config.miso;
    bus_cfg.sclk_io_num = _config.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    _spi_initialized = true;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = _config.format_if_mount_failed;
    mount_config.max_files = _config.max_files;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = _config.cs;
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD; 
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP; 

    ESP_LOGI(TAG, "Mounting filesystem at %s", _config.mount_point.c_str());
    ret = esp_vfs_fat_sdspi_mount(_config.mount_point.c_str(), &host, &slot_config, &mount_config, &_card);

    if (ret == ESP_OK) {
        _mounted = true;
        ESP_LOGI(TAG, "SD Card mounted successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to mount SD: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t SdCard::deinit() {
    esp_err_t ret = ESP_OK;
    
    if (_mounted) {
        ret = esp_vfs_fat_sdcard_unmount(_config.mount_point.c_str(), _card);
        if (ret == ESP_OK) {
            _mounted = false;
            ESP_LOGI(TAG, "SD Card unmounted.");
        } else {
            ESP_LOGE(TAG, "Failed to unmount SD: %s", esp_err_to_name(ret));
        }
    }
    
    if (_spi_initialized) {
        esp_err_t spi_ret = spi_bus_free(SPI2_HOST);
        if (spi_ret == ESP_OK) {
            _spi_initialized = false;
        } else {
            ESP_LOGE(TAG, "Failed to free SPI bus: %s", esp_err_to_name(spi_ret));
            if (ret == ESP_OK) ret = spi_ret;
        }
    }
    
    return ret;
}

std::string SdCard::getFullPath(const std::string& path) const {
    if (path.empty()) return _config.mount_point;
    
    std::string full = _config.mount_point;
    if (path[0] != '/') full += "/";
    full += path;
    return full;
}

esp_err_t SdCard::writeFile(const std::string& path, const std::string& data) {
    if (!_mounted) return ESP_ERR_INVALID_STATE;

    std::string full_path = getFullPath(path);
    FILE* f = fopen(full_path.c_str(), "w");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path.c_str());
        return ESP_FAIL;
    }

    size_t written = fwrite(data.c_str(), 1, data.length(), f);
    fclose(f);
    
    if (written != data.length()) {
        ESP_LOGE(TAG, "Incomplete write to %s", full_path.c_str());
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t SdCard::readFile(const std::string& path, std::string& out_data) {
    if (!_mounted) return ESP_ERR_INVALID_STATE;

    std::string full_path = getFullPath(path);
    FILE* f = fopen(full_path.c_str(), "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", full_path.c_str());
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 0) {
        out_data.resize(size);
        size_t read_bytes = fread(&out_data[0], 1, size, f);
        if (read_bytes != (size_t)size) {
            ESP_LOGW(TAG, "Read %zu bytes out of %ld expected", read_bytes, size);
            out_data.resize(read_bytes);
        }
    } else {
        out_data.clear();
    }
    
    fclose(f);
    return ESP_OK;
}

bool SdCard::fileExists(const std::string& path) const {
    if (!_mounted) return false;
    struct stat st;
    return (stat(getFullPath(path).c_str(), &st) == 0);
}

esp_err_t SdCard::deleteFile(const std::string& path) {
    if (!_mounted) return ESP_ERR_INVALID_STATE;
    if (unlink(getFullPath(path).c_str()) == 0) return ESP_OK;
    return ESP_FAIL;
}

void SdCard::printCardInfo() const {
    if (!_mounted) return;
    sdmmc_card_print_info(stdout, _card);
}

} // namespace Espressif::Wrappers
