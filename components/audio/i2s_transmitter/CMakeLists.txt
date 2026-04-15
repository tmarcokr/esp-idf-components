idf_component_register(SRCS "I2sTransmitter.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES driver)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
