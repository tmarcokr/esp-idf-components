idf_component_register(
    SRCS "Mpu6050.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c esp_driver_gpio esp_timer freertos
)
