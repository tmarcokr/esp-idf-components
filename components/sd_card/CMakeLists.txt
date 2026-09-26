idf_component_register(SRCS "sd_card.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES fatfs sdmmc esp_driver_sdmmc esp_driver_sdspi esp_driver_gpio)
