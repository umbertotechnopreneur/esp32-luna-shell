set(qsee_platform_srcs
    "qsee_hardware.cpp"
    "qsee_snapshot_scheduler.cpp"
    "qsee_sensor_fusion.cpp"
    "qsee_storage.cpp"
    "qsee_time.cpp"
    "qsee_settings.cpp"
    "qsee_http_server.cpp"
    "qsee_http_json.cpp"
    "qsee_performance.cpp"
)

set(qsee_platform_requires
    esp32_s3_touch_amoled_1_75
    esp_event
    esp_http_server
    esp_timer
    esp_driver_gpio
    esp_hw_support
    fatfs
    app_update
    bootloader_support
    esp_psram
    spi_flash
    nvs_flash
    qsee_logger
)

if(CONFIG_QSEE_BUILD_ENABLE_IMU AND CONFIG_QSEE_BUILD_IMU_DRIVER_QMI8658)
    list(APPEND qsee_platform_requires qmi8658)
endif()

if(CONFIG_QSEE_BUILD_ENABLE_AUDIO)
    list(APPEND qsee_platform_srcs "qsee_audio.cpp")
    list(APPEND qsee_platform_requires esp_codec_dev)
else()
    list(APPEND qsee_platform_srcs "qsee_audio_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_VOICE_AFE)
    list(APPEND qsee_platform_srcs "qsee_voice_afe.cpp")
    list(APPEND qsee_platform_requires esp-sr)
else()
    list(APPEND qsee_platform_srcs "qsee_voice_afe_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_UI)
    list(APPEND qsee_platform_srcs "qsee_display_settings.cpp")
    list(APPEND qsee_platform_requires esp_lvgl_adapter)
else()
    list(APPEND qsee_platform_srcs "qsee_display_settings_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_POWER_INPUTS)
    list(APPEND qsee_platform_srcs "qsee_power.cpp")
    list(APPEND qsee_platform_requires esp_pm)
else()
    list(APPEND qsee_platform_srcs "qsee_power_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_WIFI)
    list(APPEND qsee_platform_srcs "qsee_wifi.cpp")
    list(APPEND qsee_platform_requires esp_netif esp_wifi)
else()
    list(APPEND qsee_platform_srcs "qsee_wifi_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_PROVISIONING)
    list(APPEND qsee_platform_srcs "qsee_provisioning.cpp")
    list(APPEND qsee_platform_requires network_provisioning)
else()
    list(APPEND qsee_platform_srcs "qsee_provisioning_stub.cpp")
endif()

if(CONFIG_QSEE_BUILD_ENABLE_BLUETOOTH)
    list(APPEND qsee_platform_srcs "qsee_radios.cpp")
    list(APPEND qsee_platform_requires bt)
else()
    list(APPEND qsee_platform_srcs "qsee_radios_stub.cpp")
endif()

idf_component_register(
    SRCS ${qsee_platform_srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${qsee_platform_requires}
)

# Profile-specific dependencies are resolved after Kconfig becomes available;
# the early component-discovery pass cannot reliably consume conditional
# entries appended to REQUIRES.
if(CONFIG_QSEE_BUILD_ENABLE_AUDIO)
    idf_component_optional_requires(PUBLIC esp_codec_dev)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_VOICE_AFE)
    idf_component_optional_requires(PUBLIC esp-sr)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_IMU AND CONFIG_QSEE_BUILD_IMU_DRIVER_QMI8658)
    idf_component_optional_requires(PUBLIC qmi8658)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_UI)
    idf_component_optional_requires(PUBLIC esp_lvgl_adapter)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_POWER_INPUTS)
    idf_component_optional_requires(PUBLIC esp_pm)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_WIFI)
    idf_component_optional_requires(PUBLIC esp_netif esp_wifi)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_PROVISIONING)
    idf_component_optional_requires(PUBLIC network_provisioning)
endif()
if(CONFIG_QSEE_BUILD_ENABLE_BLUETOOTH)
    idf_component_optional_requires(PUBLIC bt)
endif()
