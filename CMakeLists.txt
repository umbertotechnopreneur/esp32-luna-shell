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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT DEFINED LUNASHELL_BUILD_MODE OR
   NOT LUNASHELL_BUILD_MODE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Explicit firmware build mode is missing; use scripts/device.ps1")
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
    if(NOT CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE OR
       NOT CONFIG_MODEL_IN_FLASH OR
       NOT CONFIG_ESP_SR_EXTERNAL_MODEL_PATH)
        message(FATAL_ERROR "Wake word requires AFE and the reviewed internal-flash model path")
    endif()
    partition_table_get_partition_info(_wake_model_size "--partition-name model" "size")
    if(NOT _wake_model_size OR _wake_model_size LESS 524288)
        message(FATAL_ERROR "Wake word requires an internal model partition of at least 512 KiB")
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
