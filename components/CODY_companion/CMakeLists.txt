idf_component_register(
    SRCS
        "CODY_companion.cpp"
        "../../assets/cody/boot/generated/cody_sleep_blue.cpp"
        "../../assets/cody/boot/generated/cody_waking_blue.cpp"
        "../../assets/cody/boot/generated/cody_ready_blue.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../assets/cody/boot/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
