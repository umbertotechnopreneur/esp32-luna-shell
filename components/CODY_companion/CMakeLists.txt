idf_component_register(
    SRCS
        "CODY_companion.cpp"
        "CODY_debug_hud.cpp"
        "CODY_rig.cpp"
        "CODY_response.cpp"
        "CODY_response_inputs.cpp"
        "../../assets/cody/boot/generated/cody_sleep_blue.cpp"
        "../../assets/cody/boot/generated/cody_waking_blue.cpp"
        "../../assets/cody/boot/generated/cody_ready_blue.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../assets/cody/boot/generated" "../../assets/cody/rig/generated"
    REQUIRES CODY2D LUNASHELL_platform heap
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75 esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
