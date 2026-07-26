set(qsee_boot_srcs)
set(qsee_boot_requires
    qsee_logger
    qsee_platform
)

if(CONFIG_QSEE_BUILD_ENABLE_BOOT_UI)
    list(APPEND qsee_boot_srcs "qsee_boot_ui.c")
    list(APPEND qsee_boot_requires esp32_s3_touch_amoled_1_75)
else()
    list(APPEND qsee_boot_srcs "qsee_boot_ui_stub.c")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_BOOT_AUDIO)
    list(APPEND qsee_boot_srcs "qsee_boot_audio.c")
    list(APPEND qsee_boot_requires esp_codec_dev)
else()
    list(APPEND qsee_boot_srcs "qsee_boot_audio_stub.c")
endif()

idf_component_register(
    SRCS ${qsee_boot_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${qsee_boot_requires}
)

if(CONFIG_QSEE_BUILD_ENABLE_BOOT_UI)
    idf_component_optional_requires(PUBLIC esp32_s3_touch_amoled_1_75)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_BOOT_AUDIO)
    idf_component_optional_requires(PUBLIC esp_codec_dev)
endif()

if(CONFIG_QSEE_BUILD_ENABLE_BOOT_UI)
    target_add_binary_data(
        ${COMPONENT_LIB}
        "${PROJECT_DIR}/assets/branding/qsee/generated/qsee-boot-466x466.rgb565le.bin"
        BINARY
        RENAME_TO qsee_boot_rgb565
    )
endif()

if(CONFIG_QSEE_BUILD_ENABLE_BOOT_AUDIO)
    target_add_binary_data(
        ${COMPONENT_LIB}
        "${PROJECT_DIR}/assets/sfx/generated/qsee-boot.wav"
        BINARY
        RENAME_TO qsee_boot_wav
    )
endif()
