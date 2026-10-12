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

cmake_minimum_required(VERSION 3.16)

# Keep ESP-IDF compile and link invocations below Windows' command-line limit.
# This is required by the Brookesia System Super example as well: the aggregate
# ESP-IDF component include graph otherwise exceeds CreateProcess limits.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE INTERNAL "")

# Keep generated runtime resources inside the active build tree. Sharing the
# source-tree littlefs/ directory across profiles lets one configured variant
# overwrite another variant's package without invalidating its Ninja stamps.
set(
    BROOKESIA_SYSTEM_CORE_ESP_RESOURCE_STAGE_ROOT
    "${CMAKE_BINARY_DIR}/littlefs"
    CACHE PATH
    "Build-local Brookesia runtime resource staging root"
    FORCE
)

# The versioned profile catalog supplies roots needed before Kconfig is
# available. Conditional CMake dependencies can then link only components that
# discovery has already admitted to this build graph.
if(NOT DEFINED ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS} OR
   "$ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS}" STREQUAL "" OR
   NOT DEFINED ENV{LUNASHELL_PROFILE_COMPONENT_DIRS} OR
   "$ENV{LUNASHELL_PROFILE_COMPONENT_DIRS}" STREQUAL "")
    message(FATAL_ERROR
        "LUNASHELL profile component graph is missing; use scripts/device.ps1"
    )
endif()
set(COMPONENTS "$ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS}")
set(
    COMPONENT_DIRS
    "$ENV{IDF_PATH}/components;$ENV{LUNASHELL_PROFILE_COMPONENT_DIRS}"
)
if(NOT DEFINED LUNASHELL_BUILD_FEATURE OR
   NOT LUNASHELL_BUILD_FEATURE MATCHES "^(none|distance|wake-word|tts-sd|tts-flash)$")
    message(FATAL_ERROR "Explicit firmware feature is missing; use scripts/device.ps1")
endif()
set(_wake_word_selected OFF)
if(LUNASHELL_BUILD_FEATURE STREQUAL "wake-word")
    set(_wake_word_selected ON)
    if(NOT "espressif__esp-sr" IN_LIST COMPONENTS)
        message(FATAL_ERROR "Wake word requires explicit ESP-SR component admission")
    endif()
else()
    foreach(_profile_component IN LISTS COMPONENTS)
        if(_profile_component MATCHES "(^|__)(esp-sr|esp-dsp|dl_fft)$")
            message(FATAL_ERROR "Disabled wake word rejected component before model discovery: ${_profile_component}")
        endif()
    endforeach()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED LUNASHELL_BUILD_MODE OR
   NOT LUNASHELL_BUILD_MODE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Explicit firmware build mode is missing; use scripts/device.ps1")
endif()
# The panic component must be discovered before Kconfig can select UART dumps.
# Debug/Release remains independent of hardware and UI profile selection.
if(LUNASHELL_BUILD_MODE STREQUAL "Debug")
    list(APPEND COMPONENTS espcoredump)
endif()
if(NOT DEFINED LUNASHELL_DEPENDENCIES_LOCK OR
   LUNASHELL_DEPENDENCIES_LOCK STREQUAL "")
    message(FATAL_ERROR
        "LUNASHELL profile dependency lock is missing; use scripts/device.ps1"
    )
endif()
get_filename_component(
    LUNASHELL_DEPENDENCIES_LOCK
    "${LUNASHELL_DEPENDENCIES_LOCK}"
    ABSOLUTE
)
idf_build_set_property(
    DEPENDENCIES_LOCK
    "${LUNASHELL_DEPENDENCIES_LOCK}"
)
project(esp32_luna_shell)

if(LUNASHELL_BUILD_MODE STREQUAL "Debug" AND
   NOT CONFIG_ESP_COREDUMP_ENABLE_TO_UART)
    message(FATAL_ERROR "Debug panic task-stack capture requires UART core dumps")
endif()

set(_tts_roots "$ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS}")
if("LUNASHELL_tts" IN_LIST _tts_roots)
    if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_TTS)
        message(FATAL_ERROR "Disabled TTS must not admit its engine component")
    endif()
elseif(CONFIG_LUNASHELL_BUILD_ENABLE_TTS)
    message(FATAL_ERROR "TTS opt-in requires an explicit tts-sd or tts-flash catalog variant")
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_TTS)
    if("$ENV{LUNASHELL_PROFILE_TTS_BACKEND}" STREQUAL "sd")
        if(NOT CONFIG_LUNASHELL_TTS_DATA_SD OR CONFIG_LUNASHELL_TTS_DATA_FLASH)
            message(FATAL_ERROR "SD feature cannot configure embedded flash speech data")
        endif()
    elseif("$ENV{LUNASHELL_PROFILE_TTS_BACKEND}" STREQUAL "flash")
        if(NOT CONFIG_LUNASHELL_TTS_DATA_FLASH OR CONFIG_LUNASHELL_TTS_DATA_SD)
            message(FATAL_ERROR "Flash feature requires the English-only data package")
        endif()
    else()
        message(FATAL_ERROR "Explicit TTS backend is missing; use scripts/device.ps1")
    endif()
endif()
if((CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD AND NOT _wake_word_selected) OR
   (_wake_word_selected AND NOT CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD) OR
   (CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE AND NOT CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD))
    message(FATAL_ERROR "Wake word, its internal AFE and the public feature selector must agree")
endif()
if(_wake_word_selected)
    set(_feature_partition_table "config/partitions/wake-word.csv")
else()
    set(_feature_partition_table "partitions.csv")
endif()
if(NOT CONFIG_PARTITION_TABLE_CUSTOM OR
   NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL _feature_partition_table)
    message(FATAL_ERROR "Selected feature requires its reviewed partition table: ${_feature_partition_table}")
endif()
partition_table_get_partition_info(_wake_model_size "--partition-name model" "size")
if(NOT _wake_word_selected AND (_wake_model_size OR CONFIG_ESP_SR_EXTERNAL_MODEL_PATH OR CONFIG_MODEL_IN_FLASH))
    message(FATAL_ERROR "Disabled wake word cannot reserve or pack recognition models")
endif()

if(LUNASHELL_BUILD_MODE STREQUAL "Debug")
    if(NOT CONFIG_LUNASHELL_BUILD_MODE_DEBUG OR CONFIG_LUNASHELL_BUILD_MODE_RELEASE)
        message(FATAL_ERROR "Debug command cannot use a Release configuration")
    endif()
else()
    if(NOT CONFIG_LUNASHELL_BUILD_MODE_RELEASE OR CONFIG_LUNASHELL_BUILD_MODE_DEBUG)
        message(FATAL_ERROR "Release command requires its own Release configuration")
    endif()
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH AND NOT CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY)
    message(FATAL_ERROR "Touch requires the selected physical display")
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI AND
   (NOT CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR NOT CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH))
    message(FATAL_ERROR "The Luna UI requires both display and touch hardware")
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_DISPLAY_TOUCH AND
   (NOT CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR
    NOT CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH OR
    CONFIG_LUNASHELL_BUILD_ENABLE_UI))
    message(FATAL_ERROR "The display-touch profile requires display and touch without the Luna UI")
endif()
if(CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION AND
   (NOT CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY OR
    NOT CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH OR
    NOT CONFIG_LUNASHELL_BUILD_ENABLE_WIFI OR
    NOT CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH OR
    CONFIG_LUNASHELL_BUILD_ENABLE_UI OR
    CONFIG_LUNASHELL_BUILD_ENABLE_GNSS))
    message(FATAL_ERROR "Cody requires display, touch, Wi-Fi and BLE with the Luna UI and GNSS excluded")
endif()
if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    set(_reviewed_profile_roots "$ENV{LUNASHELL_PROFILE_COMPONENT_ROOTS}")
    foreach(_profile_root IN LISTS _reviewed_profile_roots)
        if(_profile_root MATCHES "lvgl|brookesia_gui" OR _profile_root STREQUAL "LUNASHELL_app")
            message(FATAL_ERROR "UI-off firmware requires a profile without GUI component roots")
        endif()
    endforeach()
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE AND
   (NOT CONFIG_LUNASHELL_BUILD_VOICE_CAPTURE_AVAILABLE OR
    NOT CONFIG_LUNASHELL_BUILD_VOICE_ENGINE_AVAILABLE))
    message(FATAL_ERROR "Voice AFE requires enabled PCM16 capture, a supported CPU and PSRAM")
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD)
    if(NOT CONFIG_SPIRAM OR NOT CONFIG_SPIRAM_BOOT_INIT OR
       NOT CONFIG_SPIRAM_USE_MALLOC OR
       NOT CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY OR
       NOT CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION OR
       NOT CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
        message(FATAL_ERROR "Wake word requires initialized PSRAM, external BSS and static task stacks")
    endif()
    if(NOT CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL EQUAL 0 OR
       CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL LESS 65536)
        message(FATAL_ERROR "Wake word requires PSRAM-preferred heap allocation and at least 64 KiB reserved internal RAM")
    endif()
    if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE OR
       NOT CONFIG_MODEL_IN_FLASH OR
       NOT CONFIG_ESP_SR_EXTERNAL_MODEL_PATH)
        message(FATAL_ERROR "Wake word requires AFE and the reviewed internal-flash model path")
    endif()
    if(CONFIG_FREERTOS_UNICORE OR NOT CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS OR
       NOT CONFIG_LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING)
        message(FATAL_ERROR "The wake-word trial requires dual cores and software standby listening")
    endif()
    if(CONFIG_LUNASHELL_BUILD_ENABLE_UI AND
       (NOT CONFIG_BROOKESIA_GUI_LVGL_DISPLAY_SOURCE_TASK_CORE_ID EQUAL 1 OR
        NOT CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT EQUAL 1))
        message(FATAL_ERROR "Wake word requires the Luna LVGL renderer on CPU1 with one software draw unit")
    endif()
    if(NOT CONFIG_ESP_SR_EXTERNAL_MODEL_PATH STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/assets/wake-word/models")
        message(FATAL_ERROR "Wake word model path must belong to the selected checkout")
    endif()
    if(NOT _wake_model_size OR _wake_model_size LESS 1048576)
        message(FATAL_ERROR "Wake word requires an internal model partition of at least 1024 KiB")
    endif()
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING AND
   (NOT CONFIG_LUNASHELL_BUILD_ENABLE_WIFI OR
    NOT CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH))
    message(FATAL_ERROR "Provisioning requires both Wi-Fi and Bluetooth")
endif()

# Create the filesystem image only after every UI component has published its
# final package stamp. Both package preparation and image generation are then
# normal timestamp-driven Ninja edges rather than unconditional commands.
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    if(NOT TARGET LUNASHELL_prepare_runtime_app_package)
        message(FATAL_ERROR "LUNASHELL runtime package preparation target was not created")
    endif()
    get_property(
        LUNASHELL_runtime_app_prepare_stamp
        GLOBAL PROPERTY LUNASHELL_RUNTIME_APP_PREPARE_STAMP
    )
    if(NOT LUNASHELL_runtime_app_prepare_stamp)
        message(FATAL_ERROR "LUNASHELL runtime package stamp was not published")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/LUNASHELLIncrementalLittleFS.cmake")
    lunashell_create_incremental_littlefs_image(
        littlefs_data
        "${CMAKE_BINARY_DIR}/littlefs"
        FLASH_IN_PROJECT
        DEPENDS
            "${LUNASHELL_runtime_app_prepare_stamp}"
    )
    if(NOT TARGET littlefs_littlefs_data_bin)
        message(FATAL_ERROR "LUNASHELL incremental LittleFS target was not created")
    endif()
    add_dependencies(
        littlefs_littlefs_data_bin
        LUNASHELL_prepare_runtime_app_package
    )
endif()

# Keep the Brookesia C++ runtime compact under the firmware's -O2 build,
# matching the upstream System Super example integration.
idf_build_get_property(brookesia_build_components BUILD_COMPONENTS)
foreach(brookesia_component ${brookesia_build_components})
    if(NOT _wake_word_selected AND brookesia_component MATCHES "(^|__)(esp-sr|esp-dsp|dl_fft)$")
        message(FATAL_ERROR "Disabled wake word rejected recognition dependency: ${brookesia_component}")
    endif()
    if(brookesia_component MATCHES "brookesia")
        set(brookesia_library "__idf_${brookesia_component}")
        if(TARGET ${brookesia_library})
            get_target_property(brookesia_library_type ${brookesia_library} TYPE)
            if(NOT brookesia_library_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(
                    ${brookesia_library}
                    PRIVATE
                        -fno-ipa-cp-clone
                        -fno-ipa-sra
                )
            endif()
        endif()
    endif()
endforeach()

# Apply after ESP-DL's own -O3. Keep renderers, Sophia and assembly optimized
# for speed; size-optimize the observer and control plane within existing OTA slots.
if(CONFIG_LUNASHELL_ACTIVITY_MODEL)
    idf_component_get_property(activity_dl_library espressif__esp-dl COMPONENT_LIB)
    target_compile_options(${activity_dl_library} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-Os>")
    idf_component_get_property(activity_platform_library LUNASHELL_platform COMPONENT_LIB)
    target_compile_options(${activity_platform_library} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-Os>")
    # Scene/runtime sources coordinate events and screens. Separate rendering
    # and character-model translation units retain performance optimization.
    idf_component_get_property(activity_cody_library CODY_companion COMPONENT_LIB)
    get_target_property(activity_cody_sources ${activity_cody_library} SOURCES)
    foreach(activity_cody_source IN LISTS activity_cody_sources)
        if(activity_cody_source MATCHES "(^|/)src/(runtime|scenes)/.*[.]cpp$")
            set_property(SOURCE "${activity_cody_source}"
                DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/components/CODY_companion"
                APPEND PROPERTY COMPILE_OPTIONS -Os)
        endif()
    endforeach()
endif()

if(CONFIG_LUNASHELL_BUILD_ENABLE_TTS AND CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    # Supply the shared core constants without a circular LVGL/BSP dependency.
    idf_component_get_property(_tts_lvgl_library lvgl__lvgl COMPONENT_LIB)
    target_include_directories(${_tts_lvgl_library} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/components/esp32_s3_touch_amoled_1_75/include")
endif()
