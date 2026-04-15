#include "Mpu6050.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPU6050_EXAMPLE";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting MPU-6050 DMP Example...");

    Espressif::Wrappers::Sensors::Mpu6050 mpu(GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_5);

    if (mpu.initialize() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MPU-6050!");
        return;
    }

    ESP_LOGI(TAG, "MPU-6050 Initialized. Waiting for data...");

    while (true) {
        auto data = mpu.readData();
        if (data.has_value()) {
            auto accel = data->getLinearAcceleration();
            auto euler = data->getEulerAngles();

            ESP_LOGI(TAG, "Accel (g): x=%.2f y=%.2f z=%.2f | Euler (rad): p=%.2f r=%.2f y=%.2f",
                     accel.x, accel.y, accel.z, euler.pitch, euler.roll, euler.yaw);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
