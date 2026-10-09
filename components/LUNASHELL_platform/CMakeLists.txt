set(LUNASHELL_platform_srcs
    "src/snapshot/LUNASHELL_hardware.cpp"
    "src/snapshot/LUNASHELL_snapshot_reports.cpp"
    "src/runtime/LUNASHELL_liveness.cpp"
    "src/snapshot/LUNASHELL_snapshot_scheduler.cpp"
    "src/snapshot/LUNASHELL_sensor_fusion.cpp"
    "src/runtime/LUNASHELL_time.cpp"
    "src/runtime/LUNASHELL_settings.cpp"
    "src/snapshot/LUNASHELL_performance.cpp"
    "src/display/LUNASHELL_display_runtime.cpp"
    "src/runtime/LUNASHELL_sleep.cpp"
    "src/network/LUNASHELL_pairing_service.cpp"
    "src/network/LUNASHELL_codex_link.cpp"
    "src/network/LUNASHELL_codex_events.cpp"
)

set(LUNASHELL_platform_requires
    esp_event
    esp_timer
    heap
    esp_driver_gpio
    esp_driver_i2c
    esp_hw_support
    esp_system
    app_update
    bootloader_support
    esp_psram
    spi_flash
    xtensa
    nvs_flash
    LUNASHELL_logger
)

set(LUNASHELL_platform_uses_waveshare_bsp OFF)
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR
   CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH OR
   CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO OR
   CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS OR
   CONFIG_LUNASHELL_BUILD_ENABLE_IMU OR
   CONFIG_LUNASHELL_BUILD_ENABLE_RTC OR
   CONFIG_LUNASHELL_BUILD_ENABLE_GNSS OR
   CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    set(LUNASHELL_platform_uses_waveshare_bsp ON)
    list(APPEND LUNASHELL_platform_requires esp32_s3_touch_amoled_1_75)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    list(APPEND LUNASHELL_platform_srcs "src/storage/LUNASHELL_storage.cpp")
    list(APPEND LUNASHELL_platform_requires fatfs)
else()
    list(APPEND LUNASHELL_platform_srcs "src/storage/LUNASHELL_storage_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API)
    list(APPEND LUNASHELL_platform_srcs
        "src/http/LUNASHELL_http_server.cpp"
        "src/http/LUNASHELL_http_json.cpp"
    )
    list(APPEND LUNASHELL_platform_requires esp_http_server)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_IMU AND CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658)
    list(APPEND LUNASHELL_platform_requires qmi8658)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO)
    list(APPEND LUNASHELL_platform_srcs "src/audio/LUNASHELL_audio.cpp")
    list(APPEND LUNASHELL_platform_requires esp_codec_dev)
else()
    list(APPEND LUNASHELL_platform_srcs "src/audio/LUNASHELL_audio_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD)
    list(APPEND LUNASHELL_platform_srcs "src/audio/LUNASHELL_voice_afe.cpp")
    list(APPEND LUNASHELL_platform_requires esp-sr)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY)
    list(APPEND LUNASHELL_platform_srcs "src/display/LUNASHELL_display_settings.cpp")
else()
    list(APPEND LUNASHELL_platform_srcs "src/display/LUNASHELL_display_settings_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS)
    list(APPEND LUNASHELL_platform_srcs "src/power/LUNASHELL_power.cpp")
    list(APPEND LUNASHELL_platform_requires esp_pm)
else()
    list(APPEND LUNASHELL_platform_srcs "src/power/LUNASHELL_power_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_WIFI)
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_wifi.cpp")
    list(APPEND LUNASHELL_platform_requires esp_netif esp_wifi lwip)
else()
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_wifi_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION AND
   CONFIG_LUNASHELL_BUILD_ENABLE_WIFI AND CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX)
    list(APPEND LUNASHELL_platform_requires mbedtls)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING)
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_provisioning.cpp")
    list(APPEND LUNASHELL_platform_requires network_provisioning)
else()
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_provisioning_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH)
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_radios.cpp")
    list(APPEND LUNASHELL_platform_requires bt)
else()
    list(APPEND LUNASHELL_platform_srcs "src/network/LUNASHELL_radios_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND
   CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    list(APPEND LUNASHELL_platform_srcs "src/distance/LUNASHELL_distance_service.cpp")
else()
    list(APPEND LUNASHELL_platform_srcs "src/distance/LUNASHELL_distance_service_stub.cpp")
endif()

idf_component_register(
    SRCS ${LUNASHELL_platform_srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src/snapshot"
    REQUIRES ${LUNASHELL_platform_requires}
)

# Profile-specific dependencies are resolved after Kconfig becomes available;
# the early component-discovery pass cannot reliably consume conditional
# entries appended to REQUIRES.
if(LUNASHELL_platform_uses_waveshare_bsp)
    idf_component_optional_requires(PUBLIC esp32_s3_touch_amoled_1_75)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE)
    idf_component_optional_requires(PUBLIC fatfs)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API)
    idf_component_optional_requires(PUBLIC esp_http_server)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO)
    idf_component_optional_requires(PUBLIC esp_codec_dev)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD)
    idf_component_optional_requires(PUBLIC esp-sr)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_IMU AND CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658)
    idf_component_optional_requires(PUBLIC qmi8658)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    idf_component_optional_requires(PUBLIC esp_lvgl_adapter)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS)
    idf_component_optional_requires(PUBLIC esp_pm)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_WIFI)
    idf_component_optional_requires(PUBLIC esp_netif esp_wifi lwip)
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION AND
   CONFIG_LUNASHELL_BUILD_ENABLE_WIFI AND CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX)
    idf_component_optional_requires(PRIVATE mbedtls)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING)
    idf_component_optional_requires(PUBLIC network_provisioning)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH)
    idf_component_optional_requires(PUBLIC bt)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND
   CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    idf_component_optional_requires(PRIVATE LUNASHELL_tof400f)
endif()

if(CONFIG_LUNASHELL_TTS_DATA_SD)
    idf_component_optional_requires(PRIVATE mbedtls)
endif()
