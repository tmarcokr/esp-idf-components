idf_component_register(
    SRCS "Mpu6050.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer freertos
)
