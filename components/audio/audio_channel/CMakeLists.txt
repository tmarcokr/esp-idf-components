idf_component_register(SRCS "AudioChannel.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
