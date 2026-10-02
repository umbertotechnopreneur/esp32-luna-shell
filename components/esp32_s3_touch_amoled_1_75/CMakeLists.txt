set(REQ esp_driver_gpio esp_driver_i2c)
set(PRIV_REQ esp_driver_ledc heap)
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY)
    list(APPEND PRIV_REQ esp_driver_spi)
endif()
if("$ENV{LUNASHELL_PROFILE_BUILD_KEY}" MATCHES "^(full(-distance)?|leg-edition)$")
    list(APPEND REQ esp_driver_i2s esp_driver_sdmmc fatfs)
endif()
if("$ENV{LUNASHELL_BOARD_ENABLE_AUDIO}" STREQUAL "1")
    list(APPEND REQ esp_driver_i2s)
endif()
if("$ENV{LUNASHELL_BOARD_ENABLE_SD}" STREQUAL "1")
    list(APPEND REQ esp_driver_sdmmc fatfs)
endif()

# component: Admitted managed component whose exact pinned source must accept the patch.
# ARGN: Required repository-owned patch files, applied in their supplied order.
function(bsp_apply_required_patches component)
    idf_component_get_property(
        _component_dir
        ${component}
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
        _component_dir_relative
        "${_repo_root}"
        "${_component_dir}"
    )
    foreach(_component_patch IN LISTS ARGN)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} apply --reverse --check
                --ignore-space-change
                "--directory=${_component_dir_relative}"
                -p1 "${_component_patch}"
            WORKING_DIRECTORY "${_repo_root}"
            RESULT_VARIABLE _component_patch_present
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _component_patch_present EQUAL 0)
            execute_process(
                COMMAND
                    ${GIT_EXECUTABLE} apply --check
                    --ignore-space-change
                    "--directory=${_component_dir_relative}"
                    -p1 "${_component_patch}"
                WORKING_DIRECTORY "${_repo_root}"
                RESULT_VARIABLE _component_patch_check
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(NOT _component_patch_check EQUAL 0)
                message(FATAL_ERROR
                    "The pinned ${component} no longer accepts required patch: "
                    "${_component_patch}"
                )
            endif()
            execute_process(
                COMMAND
                    ${GIT_EXECUTABLE} apply
                    --ignore-space-change
                    "--directory=${_component_dir_relative}"
                    -p1 "${_component_patch}"
                WORKING_DIRECTORY "${_repo_root}"
                RESULT_VARIABLE _component_patch_result
            )
            if(NOT _component_patch_result EQUAL 0)
                message(FATAL_ERROR
                    "Applying required ${component} patch failed: ${_component_patch}"
                )
            endif()
        endif()
    endforeach()
endfunction()

idf_build_get_property(_build_components BUILD_COMPONENTS)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if("espressif__esp_lvgl_adapter" IN_LIST _build_components)
        bsp_apply_required_patches(
            espressif__esp_lvgl_adapter
            "${CMAKE_CURRENT_SOURCE_DIR}/patches/esp_lvgl_adapter_synchronous_custom_draw.patch"
            "${CMAKE_CURRENT_SOURCE_DIR}/patches/esp_lvgl_adapter_pause_recovery.patch"
        )
    endif()
    if("waveshare__esp_lcd_touch_cst9217" IN_LIST _build_components)
        bsp_apply_required_patches(
            waveshare__esp_lcd_touch_cst9217
            "${CMAKE_CURRENT_SOURCE_DIR}/patches/cst9217_fail_fast_reads.patch"
        )
    endif()
endif()

idf_component_register(
    SRCS "esp32_s3_touch_amoled_1_75.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    REQUIRES ${REQ} esp_lcd
    PRIV_REQUIRES ${PRIV_REQ} esp_timer spiffs esp_psram
)

# Profile roots admit the board dependencies before project Kconfig is loaded.
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY)
    idf_component_optional_requires(PRIVATE esp_driver_spi esp_lcd_co5300 esp_lcd_panel_io_additions)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO)
    idf_component_optional_requires(PUBLIC esp_driver_i2s esp_codec_dev)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH)
    idf_component_optional_requires(PUBLIC esp_lcd_touch_cst9217)
endif()
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    idf_component_optional_requires(PUBLIC lvgl esp_lvgl_adapter)
endif()
