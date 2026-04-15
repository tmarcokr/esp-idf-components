#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace Espressif::Wrappers::Sensors {

// ---------------------------------------------------------------------------
// MPU-6050 Protocol Constants (from RM-MPU-6000A-00 Rev 4.0)
// ---------------------------------------------------------------------------

/// 7-bit I2C address. AD0 LOW = 0x68 (GY-521 default), AD0 HIGH = 0x69.
static constexpr uint8_t kMpu6050DefaultAddress = 0x68;

/// DMP v6.12 FIFO packet size in bytes (16 quat + 6 accel + 6 gyro).
static constexpr size_t kDmpPacketSize = 28;

/// MPU-6050 hardware FIFO capacity in bytes.
static constexpr size_t kFifoMaxBytes = 1024;

// Key Register Addresses
static constexpr uint8_t kRegSmplrtDiv  = 0x19; ///< Sample Rate Divider
static constexpr uint8_t kRegConfig     = 0x1A; ///< DLPF Configuration
static constexpr uint8_t kRegGyroConfig = 0x1B; ///< Gyroscope Configuration
static constexpr uint8_t kRegAccelConfig = 0x1C; ///< Accelerometer Configuration
static constexpr uint8_t kRegIntPinCfg  = 0x37; ///< INT Pin / Bypass Enable
static constexpr uint8_t kRegIntEnable  = 0x38; ///< Interrupt Enable
static constexpr uint8_t kRegIntStatus  = 0x3A; ///< Interrupt Status
static constexpr uint8_t kRegAccelXOutH = 0x3B; ///< Accelerometer X High Byte
static constexpr uint8_t kRegGyroXOutH  = 0x43; ///< Gyroscope X High Byte
static constexpr uint8_t kRegUserCtrl   = 0x6A; ///< User Control (FIFO/DMP)
static constexpr uint8_t kRegPwrMgmt1   = 0x6B; ///< Power Management 1
static constexpr uint8_t kRegPwrMgmt2   = 0x6C; ///< Power Management 2
static constexpr uint8_t kRegBankSel    = 0x6D; ///< DMP Bank Select
static constexpr uint8_t kRegMemStartAddr = 0x6E; ///< DMP Memory Start Address
static constexpr uint8_t kRegMemRW      = 0x6F; ///< DMP Memory Read/Write
static constexpr uint8_t kRegDmpCfg1    = 0x70; ///< DMP Configuration 1
static constexpr uint8_t kRegDmpCfg2    = 0x71; ///< DMP Configuration 2
static constexpr uint8_t kRegFifoCountH = 0x72; ///< FIFO Count High Byte
static constexpr uint8_t kRegFifoCountL = 0x73; ///< FIFO Count Low Byte
static constexpr uint8_t kRegFifoRw     = 0x74; ///< FIFO Read/Write
static constexpr uint8_t kRegWhoAmI     = 0x75; ///< WHO_AM_I (expected: 0x68)

// ---------------------------------------------------------------------------
// Data Transfer Object
// ---------------------------------------------------------------------------

/**
 * @brief Universal motion data DTO produced by the MPU-6050 DMP.
 *
 * Contains raw sensor readings, DMP-processed quaternion, and helper methods
 * for derived quantities (gravity vector, Euler angles, linear acceleration).
 * Domain-agnostic: suitable for any embedded consumer.
 */
struct MotionData {
    // Raw Accelerometer and Gyroscope Data
    int16_t accel_x = 0, accel_y = 0, accel_z = 0;
    int16_t gyro_x  = 0, gyro_y  = 0, gyro_z  = 0;

    // DMP-processed quaternion (normalized)
    float quaternion_w = 1.0f, quaternion_x = 0.0f;
    float quaternion_y = 0.0f, quaternion_z = 0.0f;

    // --- Return Structures ---
    struct Vector3D    { float x, y, z; };
    struct EulerAngles { float pitch, roll, yaw; };

    /**
     * @brief Returns the Earth's gravity vector derived from the quaternion.
     * Represents the absolute subjective 'down' direction.
     */
    [[nodiscard]] Vector3D getGravityVector() const {
        return {
            .x = 2.0f * (quaternion_x * quaternion_z - quaternion_w * quaternion_y),
            .y = 2.0f * (quaternion_w * quaternion_x + quaternion_y * quaternion_z),
            .z = quaternion_w * quaternion_w - quaternion_x * quaternion_x
                 - quaternion_y * quaternion_y + quaternion_z * quaternion_z
        };
    }

    /**
     * @brief Extracts Euler angles (pitch, roll, yaw) from the quaternion.
     * @return EulerAngles in radians.
     */
    [[nodiscard]] EulerAngles getEulerAngles() const {
        auto g = getGravityVector();
        return {
            .pitch = std::atan2(g.x, std::sqrt(g.y * g.y + g.z * g.z)),
            .roll  = std::atan2(g.y, std::sqrt(g.x * g.x + g.z * g.z)),
            .yaw   = std::atan2(
                2.0f * (quaternion_x * quaternion_y - quaternion_w * quaternion_z),
                2.0f * (quaternion_w * quaternion_w + quaternion_x * quaternion_x) - 1.0f
            )
        };
    }

    /**
     * @brief True Linear Acceleration: raw accel minus Earth's gravity.
     * Eliminates the constant 1G offset for clean motion/impact detection.
     * @return Acceleration in g-units (assuming ±2g full-scale default).
     */
    [[nodiscard]] Vector3D getLinearAcceleration() const {
        auto g = getGravityVector();
        // Default full-scale ±2g → 16384 LSB/g
        constexpr float kAccelScale = 16384.0f;
        return {
            .x = (static_cast<float>(accel_x) / kAccelScale) - g.x,
            .y = (static_cast<float>(accel_y) / kAccelScale) - g.y,
            .z = (static_cast<float>(accel_z) / kAccelScale) - g.z
        };
    }
};

// ---------------------------------------------------------------------------
// Driver Class
// ---------------------------------------------------------------------------

/**
 * @brief RAII wrapper for the MPU-6050 sensor over I2C with DMP support.
 *
 * Manages the I2C bus and device handles. Loads DMP firmware, configures
 * the FIFO for interrupt-driven data delivery, and provides overflow-safe
 * data reads.
 *
 * Non-copyable, non-movable (owns hardware resources).
 */
class Mpu6050 {
public:
    /**
     * @brief Construct the driver.
     * @param sda_pin I2C data pin.
     * @param scl_pin I2C clock pin.
     * @param int_pin MPU-6050 INT output pin.
     * @param i2c_address 7-bit I2C address.
     */
    Mpu6050(gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t int_pin,
            uint8_t i2c_address = kMpu6050DefaultAddress);

    /**
     * @brief Destroy the driver and release I2C handles.
     */
    ~Mpu6050();

    // Non-copyable, non-movable
    Mpu6050(const Mpu6050&) = delete;
    Mpu6050& operator=(const Mpu6050&) = delete;
    Mpu6050(Mpu6050&&) = delete;
    Mpu6050& operator=(Mpu6050&&) = delete;

    /**
     * @brief Initialize I2C, verify WHO_AM_I, wake chip, load DMP, configure
     *        FIFO and interrupt pin (latched mode).
     * @return esp_err_t ESP_OK on success.
     */
    [[nodiscard]] esp_err_t initialize();

    /**
     * @brief Read latest DMP data from FIFO with overflow protection.
     *
     * Returns std::nullopt if:
     * - FIFO empty (no complete DMP packet available)
     * - FIFO overflow detected (>= 1024 bytes) → automatic reset
     * - FIFO misalignment (count not multiple of packet size) → automatic reset
     *
     * When multiple packets are queued, drains to keep only the latest.
     */
    [[nodiscard]] std::optional<MotionData> readData();

private:
    static constexpr const char* TAG = "Mpu6050";

    gpio_num_t m_sda;
    gpio_num_t m_scl;
    gpio_num_t m_int;
    uint8_t    m_i2c_address;

    i2c_master_bus_handle_t m_bus_handle  = nullptr;
    i2c_master_dev_handle_t m_dev_handle  = nullptr;
    bool m_initialized = false;

    [[nodiscard]] esp_err_t writeRegister(uint8_t reg, uint8_t data);
    [[nodiscard]] esp_err_t readRegisters(uint8_t reg, uint8_t* buffer, size_t len);
    [[nodiscard]] esp_err_t loadDmpFirmware();
    [[nodiscard]] esp_err_t resetFifo();
    [[nodiscard]] uint16_t  readFifoCount();

    MotionData parseDmpPacket(const uint8_t* buffer);
};

} // namespace Espressif::Wrappers::Sensors
