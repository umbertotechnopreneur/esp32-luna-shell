idf_component_register(
    SRCS "src/CODY2D.cpp" "src/CODY2D_paths.cpp" "src/CODY2D_text.cpp" "src/CODY2D_controls.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
