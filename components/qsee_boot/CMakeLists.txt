idf_component_register(
    SRCS
        "qsee_boot_ui.c"
        "qsee_boot_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp32_s3_touch_amoled_1_75
        qsee_logger
        qsee_platform
        esp_codec_dev
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "${PROJECT_DIR}/assets/branding/qsee/generated/qsee-boot-466x466.rgb565le.bin"
    BINARY
    RENAME_TO qsee_boot_rgb565
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "${PROJECT_DIR}/assets/sfx/generated/qsee-boot.wav"
    BINARY
    RENAME_TO qsee_boot_wav
)
