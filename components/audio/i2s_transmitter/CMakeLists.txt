idf_component_register(SRCS "I2sTransmitter.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2s esp_driver_gpio)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
