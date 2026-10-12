# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

set(LUNASHELL_platform_srcs
    "src/snapshot/LUNASHELL_hardware.cpp"
    "src/snapshot/LUNASHELL_acquisition_i2c.cpp"
    "src/snapshot/LUNASHELL_acquisition_pmu.cpp"
    "src/snapshot/LUNASHELL_acquisition_rtc.cpp"
    "src/snapshot/LUNASHELL_acquisition_imu.cpp"
    "src/snapshot/LUNASHELL_activity.cpp"
    "src/power/LUNASHELL_battery_endurance.cpp"
    "src/snapshot/LUNASHELL_acquisition_gnss.cpp"
    "src/snapshot/LUNASHELL_acquisition_audio.cpp"
    "src/snapshot/LUNASHELL_acquisition_system.cpp"
    "src/snapshot/LUNASHELL_rtc_calendar.cpp"
    "src/snapshot/LUNASHELL_snapshot_aging.cpp"
    "src/snapshot/LUNASHELL_snapshot_names.cpp"
    "src/snapshot/LUNASHELL_snapshot_reports.cpp"
    "src/runtime/LUNASHELL_liveness.cpp"
    "src/snapshot/LUNASHELL_snapshot_scheduler.cpp"
    "src/snapshot/LUNASHELL_sensor_fusion.cpp"
    "src/runtime/LUNASHELL_time.cpp"
    "src/runtime/LUNASHELL_clock_policy.cpp"
    "src/runtime/LUNASHELL_settings.cpp"
    "src/snapshot/LUNASHELL_performance.cpp"
    "src/display/LUNASHELL_display_runtime.cpp"
    "src/runtime/LUNASHELL_sleep.cpp"
    "src/network/LUNASHELL_pairing_service.cpp"
    "src/network/LUNASHELL_codex_link.cpp"
    "src/network/LUNASHELL_codex_events.cpp"
    "src/network/LUNASHELL_codex_transaction.cpp"
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
    esp_app_format
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

if(CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT)
    list(APPEND LUNASHELL_platform_srcs "src/audio/LUNASHELL_voice_input.cpp")
    list(APPEND LUNASHELL_platform_requires mbedtls lwip)
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY)
    list(APPEND LUNASHELL_platform_srcs "src/display/LUNASHELL_display_settings.cpp")
else()
    list(APPEND LUNASHELL_platform_srcs "src/display/LUNASHELL_display_settings_stub.cpp")
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS)
    list(APPEND LUNASHELL_platform_srcs
        "src/power/LUNASHELL_power.cpp"
        "src/power/LUNASHELL_standby_resources.cpp")
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

if(CONFIG_LUNASHELL_ACTIVITY_MODEL)
    idf_component_optional_requires(PRIVATE esp-dl)
    set(activity_model "${PROJECT_DIR}/artifacts/models/activity/activity.espdl")
    # These files participate in configure-time provenance and graph validation.
    # Reconfigure before an incremental compile can consume changed model bytes,
    # preprocessing constants or their manifest; object dependencies alone cannot
    # enforce those checks. Absolute paths are independent of component cwd.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${activity_model}"
        "${PROJECT_DIR}/artifacts/models/activity/activity_preprocessing.hpp"
        "${PROJECT_DIR}/artifacts/models/activity/provenance.json"
    )
    if(NOT EXISTS "${activity_model}")
        message(FATAL_ERROR "Generate the local pretrained model with scripts/device.ps1 generate -Generator activity-model before enabling activity inference")
    endif()
    if(NOT EXISTS "${PROJECT_DIR}/artifacts/models/activity/activity_preprocessing.hpp")
        message(FATAL_ERROR "Recover the pretrained activity preprocessing through the activity-model generator")
    endif()
    file(READ "${PROJECT_DIR}/artifacts/models/activity/provenance.json" activity_provenance)
    string(JSON activity_expected_hash GET "${activity_provenance}" model_sha256)
    string(JSON activity_expected_preprocessing GET "${activity_provenance}" preprocessing_sha256)
    file(SHA256 "${activity_model}" activity_actual_hash)
    file(SHA256 "${PROJECT_DIR}/artifacts/models/activity/activity_preprocessing.hpp" activity_actual_preprocessing)
    if(NOT activity_expected_hash STREQUAL activity_actual_hash OR
       NOT activity_expected_preprocessing STREQUAL activity_actual_preprocessing)
        message(FATAL_ERROR "Local activity model/preprocessing changed since conversion; regenerate explicitly")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/artifacts/models/activity")
    # IDF 6.0.2 target_add_binary_data has no alignment argument and emits no
    # alignment directive. ESP-DL requires a 16-byte flash model base for zero-copy
    # loading; native assembler embedding preserves every reviewed artifact byte.
    set(activity_model_assembly "${CMAKE_CURRENT_BINARY_DIR}/activity_model.S")
    file(GENERATE OUTPUT "${activity_model_assembly}" CONTENT
".section .rodata.activity_model,\"a\",@progbits
.balign 16
.global _binary_activity_model_start
_binary_activity_model_start:
.incbin \"${activity_model}\"
.global _binary_activity_model_end
_binary_activity_model_end:
")
    set_source_files_properties("${activity_model_assembly}" PROPERTIES
        OBJECT_DEPENDS "${activity_model}")
    target_sources(${COMPONENT_LIB} PRIVATE "${activity_model_assembly}")
    # The reviewed requirement set describes this exact converted graph. A new
    # export needs a new operator/kernel review before its requirements are reused.
    if(NOT activity_actual_hash STREQUAL "b5333dafe62835cc9999fe27ea652802fcdcaa5250604c5136087d1c1044d4e0")
        message(FATAL_ERROR "Activity model differs from the reviewed ESP-DL compile requirements; review the graph and update the requirement binding")
    endif()
    idf_build_get_property(activity_build_components BUILD_COMPONENTS)
    if(NOT "espressif__esp-dl" IN_LIST activity_build_components)
        message(FATAL_ERROR "Activity inference requires the pinned managed ESP-DL component")
    endif()
    idf_component_get_property(activity_esp_dl_dir espressif__esp-dl COMPONENT_DIR)
    include("${activity_esp_dl_dir}/cmake/compile_register.cmake")
    esp_dl_compile_add_yml("${CMAKE_CURRENT_LIST_DIR}/activity.compile_req.yml")
endif()

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
