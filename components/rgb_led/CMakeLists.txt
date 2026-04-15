idf_component_register(
    SRCS "RgbLed.cpp"
    INCLUDE_DIRS "include"
    REQUIRES led_strip
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)