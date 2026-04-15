idf_component_register(SRCS "AudioEngine.cpp" "PolyphonicMixer.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_channel i2s_transmitter freertos)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
