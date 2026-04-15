#pragma once

#include "esp_err.h"
#include "hal/gpio_types.h" // IWYU pragma: keep
#include "sdmmc_cmd.h" // IWYU pragma: keep
#include <string>

namespace Espressif::Wrappers {

/**
 * @brief Wrapper for SD card management over SPI bus using FATFS.
 */
class SdCard {
public:
    /**
     * @brief Configuration for the SD card SPI interface.
     */
    struct Config {
        gpio_num_t miso;                ///< I2C MISO pin
        gpio_num_t mosi;                ///< I2C MOSI pin
        gpio_num_t sck;                 ///< I2C SCK pin
        gpio_num_t cs;                  ///< SD CS pin
        std::string mount_point;        ///< VFS mount point (e.g. "/sdcard")
        int max_files;                  ///< Max simultaneous open files
        bool format_if_mount_failed;    ///< Auto-format flag
    };

    /**
     * @brief Construct a new Sd Card manager.
     * @param config SPI pins and filesystem settings.
     */
    explicit SdCard(const Config& config);
    
    /**
     * @brief Destroy the Sd Card manager, unmounting and freeing resources.
     */
    ~SdCard();

    SdCard(const SdCard&) = delete;
    SdCard& operator=(const SdCard&) = delete;

    /**
     * @brief Initialize the SPI bus and mount the FAT filesystem.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t init();

    /**
     * @brief Unmount the filesystem and free SPI bus resources.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t deinit();

    /**
     * @brief Write data to a file on the SD card.
     * @param path Relative path to the file.
     * @param data String content to write.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t writeFile(const std::string& path, const std::string& data);

    /**
     * @brief Read data from a file on the SD card.
     * @param path Relative path to the file.
     * @param out_data String to store the read content.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t readFile(const std::string& path, std::string& out_data);

    /**
     * @brief Delete a file from the SD card.
     * @param path Relative path to the file.
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t deleteFile(const std::string& path);
    
    /**
     * @brief Check if a file exists.
     * @param path Relative path to the file.
     * @return true if file exists.
     */
    bool fileExists(const std::string& path) const;

    /**
     * @brief Print card information (Name, Type, Speed, Size) to stdout.
     */
    void printCardInfo() const;

private:
    Config _config;
    sdmmc_card_t* _card;
    bool _mounted;
    bool _spi_initialized;
    
    std::string getFullPath(const std::string& path) const;
};

} // namespace Espressif::Wrappers
