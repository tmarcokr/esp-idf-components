idf_component_register(
    SRCS "GpioButton.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
