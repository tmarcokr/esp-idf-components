#include "Mpu6050.hpp"
#include "mpu6050_dmp_firmware.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>

namespace Espressif::Wrappers::Sensors {


static constexpr size_t kDmpMemChunkSize = 16;

/// DMP Program Start Address written to registers 0x70-0x71.
static constexpr uint16_t kDmpProgramStart = 0x0400;


Mpu6050::Mpu6050(gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t int_pin,
                 uint8_t i2c_address)
    : m_sda(sda_pin)
    , m_scl(scl_pin)
    , m_int(int_pin)
    , m_i2c_address(i2c_address) {}

Mpu6050::~Mpu6050() {
    if (m_dev_handle) {
        i2c_master_bus_rm_device(m_dev_handle);
        ESP_LOGI(TAG, "I2C device handle released.");
    }
    if (m_bus_handle) {
        i2c_del_master_bus(m_bus_handle);
        ESP_LOGI(TAG, "I2C bus handle released.");
    }
}


esp_err_t Mpu6050::writeRegister(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(m_dev_handle, buf, sizeof(buf), -1);
}

esp_err_t Mpu6050::readRegisters(uint8_t reg, uint8_t* buffer, size_t len) {
    return i2c_master_transmit_receive(m_dev_handle, &reg, 1, buffer, len, -1);
}


esp_err_t Mpu6050::loadDmpFirmware() {
    ESP_LOGI(TAG, "Loading DMP firmware (%zu bytes)...", kDmpFirmwareSize);

    size_t bytes_written = 0;

    while (bytes_written < kDmpFirmwareSize) {
        // Calculate bank and address within that bank
        uint8_t bank = static_cast<uint8_t>(bytes_written / 256);
        uint8_t addr = static_cast<uint8_t>(bytes_written % 256);

        // Calculate chunk size (limited by bank boundary and remaining data)
        size_t remaining_in_bank = 256 - addr;
        size_t remaining_data = kDmpFirmwareSize - bytes_written;
        size_t chunk_size = std::min({kDmpMemChunkSize, remaining_in_bank, remaining_data});

        // Set bank
        ESP_RETURN_ON_ERROR(writeRegister(kRegBankSel, bank), TAG, "Bank select failed");

        // Set start address within bank
        ESP_RETURN_ON_ERROR(writeRegister(kRegMemStartAddr, addr), TAG, "Mem addr failed");

        // Write chunk: [register_address, data...]
        uint8_t write_buf[kDmpMemChunkSize + 1];
        write_buf[0] = kRegMemRW;
        std::memcpy(&write_buf[1], &kDmpFirmware[bytes_written], chunk_size);

        ESP_RETURN_ON_ERROR(
            i2c_master_transmit(m_dev_handle, write_buf, chunk_size + 1, -1),
            TAG, "DMP memory write failed at offset %zu", bytes_written
        );

        bytes_written += chunk_size;
    }

    // Verify a sample from the uploaded firmware (first bank, first byte)
    ESP_RETURN_ON_ERROR(writeRegister(kRegBankSel, 0), TAG, "Verify bank select failed");
    ESP_RETURN_ON_ERROR(writeRegister(kRegMemStartAddr, 0), TAG, "Verify addr failed");

    uint8_t verify_buf[1] = {0};
    ESP_RETURN_ON_ERROR(readRegisters(kRegMemRW, verify_buf, 1), TAG, "Verify read failed");

    if (verify_buf[0] != kDmpFirmware[0]) {
        ESP_LOGE(TAG, "DMP verify failed: expected 0x%02X, got 0x%02X",
                 kDmpFirmware[0], verify_buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "DMP firmware loaded and verified successfully.");
    return ESP_OK;
}


uint16_t Mpu6050::readFifoCount() {
    uint8_t buf[2] = {0, 0};
    esp_err_t err = readRegisters(kRegFifoCountH, buf, 2);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read FIFO count: %s", esp_err_to_name(err));
        return 0;
    }
    return (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
}

esp_err_t Mpu6050::resetFifo() {
    // Read current USER_CTRL, set FIFO_RESET bit (bit 2), then re-enable FIFO + DMP
    // Sequence: disable FIFO+DMP → reset FIFO → re-enable FIFO+DMP
    ESP_RETURN_ON_ERROR(writeRegister(kRegUserCtrl, 0x00), TAG, "Disable FIFO failed");
    ESP_RETURN_ON_ERROR(writeRegister(kRegUserCtrl, 0x04), TAG, "FIFO reset failed");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(writeRegister(kRegUserCtrl, 0xC0), TAG, "Re-enable FIFO+DMP failed");
    return ESP_OK;
}


MotionData Mpu6050::parseDmpPacket(const uint8_t* p) {
    MotionData data{};

    // Bytes 0-15: Quaternion as 4x int32 (big-endian), scaled by 16384.0
    auto read_i32 = [](const uint8_t* b) -> int32_t {
        return static_cast<int32_t>(
            (static_cast<uint32_t>(b[0]) << 24) |
            (static_cast<uint32_t>(b[1]) << 16) |
            (static_cast<uint32_t>(b[2]) << 8)  |
             static_cast<uint32_t>(b[3])
        );
    };

    auto read_i16 = [](const uint8_t* b) -> int16_t {
        return static_cast<int16_t>(
            (static_cast<uint16_t>(b[0]) << 8) | b[1]
        );
    };

    constexpr float kQuatScale = 1073741824.0f; // 2^30 for DMP v6.12 32-bit quaternions
    data.quaternion_w = static_cast<float>(read_i32(&p[0]))  / kQuatScale;
    data.quaternion_x = static_cast<float>(read_i32(&p[4]))  / kQuatScale;
    data.quaternion_y = static_cast<float>(read_i32(&p[8]))  / kQuatScale;
    data.quaternion_z = static_cast<float>(read_i32(&p[12])) / kQuatScale;

    // Bytes 16-21: Raw Accelerometer (3x int16, big-endian)
    data.accel_x = read_i16(&p[16]);
    data.accel_y = read_i16(&p[18]);
    data.accel_z = read_i16(&p[20]);

    // Bytes 22-27: Raw Gyroscope (3x int16, big-endian)
    data.gyro_x = read_i16(&p[22]);
    data.gyro_y = read_i16(&p[24]);
    data.gyro_z = read_i16(&p[26]);

    return data;
}


esp_err_t Mpu6050::initialize() {
    ESP_LOGI(TAG, "Initializing MPU-6050 on SDA=%d, SCL=%d, INT=%d, addr=0x%02X",
             m_sda, m_scl, m_int, m_i2c_address);

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = m_sda;
    bus_config.scl_io_num = m_scl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    // Enable internal pullups. They will run in parallel with the GY-521 onboard
    // 2.2k resistors, effectively reducing resistance slightly to improve rise
    // times over breadboard wiring, silencing the ESP-IDF i2c.master warning.
    bus_config.flags.enable_internal_pullup = true;

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&bus_config, &m_bus_handle),
        TAG, "I2C bus creation failed"
    );

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = m_i2c_address;
    dev_config.scl_speed_hz = 400000;

    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(m_bus_handle, &dev_config, &m_dev_handle),
        TAG, "I2C device add failed"
    );

    ESP_RETURN_ON_ERROR(writeRegister(kRegPwrMgmt1, 0x80), TAG, "Device reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(writeRegister(kRegUserCtrl, 0x07), TAG, "Signal path reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(writeRegister(kRegPwrMgmt1, 0x01), TAG, "Wake/clock failed");

    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(readRegisters(kRegWhoAmI, &who_am_i, 1), TAG, "WHO_AM_I read failed");

    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: expected 0x68, got 0x%02X", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "WHO_AM_I verified: 0x%02X", who_am_i);

    // Disable interrupts during setup
    ESP_RETURN_ON_ERROR(writeRegister(kRegIntEnable, 0x00), TAG, "INT disable failed");

    ESP_RETURN_ON_ERROR(writeRegister(0x23, 0x00), TAG, "FIFO_EN clear failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegAccelConfig, 0x00), TAG, "Accel config failed");

    // INT pin: active HIGH, 50us pulse mode, cleared on any read
    // 0x10 = ACTL(0) | LATCH_INT_EN(0) | INT_RD_CLEAR(1)
    ESP_RETURN_ON_ERROR(writeRegister(kRegIntPinCfg, 0x10), TAG, "INT pin config failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegPwrMgmt1, 0x01), TAG, "Clock reconfig failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegSmplrtDiv, 0x04), TAG, "Sample rate failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegConfig, 0x01), TAG, "DLPF config failed");

    ESP_RETURN_ON_ERROR(loadDmpFirmware(), TAG, "DMP firmware load failed");

    uint8_t start_addr[2] = {
        static_cast<uint8_t>((kDmpProgramStart >> 8) & 0xFF),
        static_cast<uint8_t>(kDmpProgramStart & 0xFF)
    };
    ESP_RETURN_ON_ERROR(writeRegister(kRegDmpCfg1, start_addr[0]), TAG, "DMP start H failed");
    ESP_RETURN_ON_ERROR(writeRegister(kRegDmpCfg2, start_addr[1]), TAG, "DMP start L failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegGyroConfig, 0x18), TAG, "Gyro config failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegUserCtrl, 0xC0), TAG, "Enable FIFO+DMP failed");

    ESP_RETURN_ON_ERROR(writeRegister(kRegIntEnable, 0x02), TAG, "INT enable failed");

    ESP_RETURN_ON_ERROR(resetFifo(), TAG, "Final FIFO reset failed");

    m_initialized = true;
    ESP_LOGI(TAG, "MPU-6050 initialized with DMP v6.12 (packet=%zu bytes).", kDmpPacketSize);
    return ESP_OK;
}


std::optional<MotionData> Mpu6050::readData() {
    if (!m_initialized) {
        return std::nullopt;
    }

    uint16_t fifo_count = readFifoCount();

    // Overflow detection: FIFO is 1024 bytes, if full the data is corrupt
    if (fifo_count >= kFifoMaxBytes) {
        ESP_LOGW(TAG, "FIFO overflow (%u bytes), resetting.", fifo_count);
        (void)resetFifo();
        return std::nullopt;
    }

    // Not enough data for a complete packet
    if (fifo_count < kDmpPacketSize) {
        return std::nullopt;
    }

    // Misalignment detection: byte count should be a multiple of packet size
    if ((fifo_count % kDmpPacketSize) != 0) {
        ESP_LOGW(TAG, "FIFO misaligned (%u bytes), resetting.", fifo_count);
        (void)resetFifo();
        return std::nullopt;
    }

    // Drain stale packets, keep only the latest one
    uint8_t packet[kDmpPacketSize];
    size_t packets_available = fifo_count / kDmpPacketSize;

    for (size_t i = 0; i < packets_available; ++i) {
        esp_err_t err = readRegisters(kRegFifoRw, packet, kDmpPacketSize);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "FIFO read error: %s", esp_err_to_name(err));
            (void)resetFifo();
            return std::nullopt;
        }
    }

    // Parse the last (freshest) packet
    return parseDmpPacket(packet);
}

} // namespace Espressif::Wrappers::Sensors
