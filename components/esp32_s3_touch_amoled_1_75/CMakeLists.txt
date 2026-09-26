set(REQ esp_driver_gpio esp_driver_i2c)
set(PRIV_REQ esp_driver_ledc heap)
if("$ENV{LUNASHELL_PROFILE_BUILD_KEY}" MATCHES "^full(-distance)?$")
    list(APPEND REQ esp_driver_i2s esp_driver_sdmmc fatfs)
endif()
if("$ENV{LUNASHELL_BOARD_ENABLE_AUDIO}" STREQUAL "1")
    list(APPEND REQ esp_driver_i2s)
endif()
if("$ENV{LUNASHELL_BOARD_ENABLE_SD}" STREQUAL "1")
    list(APPEND REQ esp_driver_sdmmc fatfs)
endif()

# Patch the pinned adapter for the board's synchronous multi-strip flush and
# for transactional worker pause/resume with real tick-timer error recovery.
set(_adapter_patches
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/esp_lvgl_adapter_synchronous_custom_draw.patch"
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/esp_lvgl_adapter_pause_recovery.patch"
)
idf_build_get_property(_build_components BUILD_COMPONENTS)
if(
    NOT CMAKE_BUILD_EARLY_EXPANSION
    AND "espressif__esp_lvgl_adapter" IN_LIST _build_components
)
    idf_component_get_property(
        _adapter_dir
        espressif__esp_lvgl_adapter
        COMPONENT_DIR
    )
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _repo_root_result
        OUTPUT_VARIABLE _repo_root
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _repo_root_result EQUAL 0)
        message(FATAL_ERROR "Resolving the repository root failed")
    endif()
    file(
        RELATIVE_PATH
        _adapter_dir_relative
        "${_repo_root}"
        "${_adapter_dir}"
    )
    foreach(_adapter_patch IN LISTS _adapter_patches)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} apply --reverse --check
                --ignore-space-change
                "--directory=${_adapter_dir_relative}"
                -p1 "${_adapter_patch}"
            WORKING_DIRECTORY "${_repo_root}"
            RESULT_VARIABLE _adapter_patch_present
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _adapter_patch_present EQUAL 0)
            execute_process(
                COMMAND
                    ${GIT_EXECUTABLE} apply --check
                    --ignore-space-change
                    "--directory=${_adapter_dir_relative}"
                    -p1 "${_adapter_patch}"
                WORKING_DIRECTORY "${_repo_root}"
                RESULT_VARIABLE _adapter_patch_check
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(NOT _adapter_patch_check EQUAL 0)
                message(FATAL_ERROR
                    "The pinned esp_lvgl_adapter no longer accepts required patch: "
                    "${_adapter_patch}"
                )
            endif()
            execute_process(
                COMMAND
                    ${GIT_EXECUTABLE} apply
                    --ignore-space-change
                    "--directory=${_adapter_dir_relative}"
                    -p1 "${_adapter_patch}"
                WORKING_DIRECTORY "${_repo_root}"
                RESULT_VARIABLE _adapter_patch_result
            )
            if(NOT _adapter_patch_result EQUAL 0)
                message(FATAL_ERROR
                    "Applying required esp_lvgl_adapter patch failed: ${_adapter_patch}"
                )
            endif()
        endif()
    endforeach()
endif()

idf_component_register(
    SRCS "esp32_s3_touch_amoled_1_75.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    REQUIRES ${REQ} esp_lcd
    PRIV_REQUIRES ${PRIV_REQ} esp_timer spiffs esp_psram
)
