idf_component_register(
    SRCS "CODY2D.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
