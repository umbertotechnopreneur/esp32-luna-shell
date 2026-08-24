set(LUNASHELL_boot_srcs)
set(LUNASHELL_boot_requires
    LUNASHELL_logger
    LUNASHELL_platform
)

if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI)
    list(APPEND LUNASHELL_boot_srcs "LUNASHELL_boot_ui.c")
    list(APPEND LUNASHELL_boot_requires esp32_s3_touch_amoled_1_75)
else()
    list(APPEND LUNASHELL_boot_srcs "LUNASHELL_boot_ui_stub.c")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
    list(APPEND LUNASHELL_boot_srcs "LUNASHELL_boot_audio.c")
    list(APPEND LUNASHELL_boot_requires esp_codec_dev)
else()
    list(APPEND LUNASHELL_boot_srcs "LUNASHELL_boot_audio_stub.c")
endif()

idf_component_register(
    SRCS ${LUNASHELL_boot_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${LUNASHELL_boot_requires}
)

if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI)
    idf_component_optional_requires(PUBLIC esp32_s3_touch_amoled_1_75)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
    idf_component_optional_requires(PUBLIC esp_codec_dev)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI)
    target_add_binary_data(
        ${COMPONENT_LIB}
        "${PROJECT_DIR}/assets/branding/LUNASHELL/generated/LUNASHELL-boot-466x466.rgb565le.bin"
        BINARY
        RENAME_TO LUNASHELL_boot_rgb565
    )
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
    target_add_binary_data(
        ${COMPONENT_LIB}
        "${PROJECT_DIR}/assets/sfx/generated/lunashell-boot.wav"
        BINARY
        RENAME_TO LUNASHELL_boot_wav
    )
endif()
