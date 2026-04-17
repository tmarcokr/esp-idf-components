idf_component_register(
    SRCS "LedStrip.cpp"
         "Canvas.cpp"
         "Engine.cpp"
         "effects/SolidColor.cpp"
         "effects/Breathe.cpp"
         "effects/RainbowCycle.cpp"
         "effects/Chase.cpp"
         "effects/Flash.cpp"
    INCLUDE_DIRS "include"
    REQUIRES led_strip freertos esp_timer
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
