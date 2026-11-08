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

# Incremental LittleFS image integration for ESP-IDF 6.0.2.

include_guard(GLOBAL)

function(lunashell_create_incremental_littlefs_image partition base_dir)
    set(options FLASH_IN_PROJECT)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(
        LUNASHELL_LITTLEFS
        "${options}"
        ""
        "${multi_value_args}"
        ${ARGN}
    )

    get_filename_component(base_dir_full_path "${base_dir}" ABSOLUTE)
    partition_table_get_partition_info(
        partition_size
        "--partition-name ${partition}"
        "size"
    )
    partition_table_get_partition_info(
        partition_offset
        "--partition-name ${partition}"
        "offset"
    )
    if(NOT partition_size OR NOT partition_offset)
        message(FATAL_ERROR
            "LittleFS partition '${partition}' is missing from the active table"
        )
    endif()

    idf_component_get_property(
        littlefs_component_dir
        joltwallet__littlefs
        COMPONENT_DIR
    )
    set(
        littlefs_requirements
        "${littlefs_component_dir}/image-building-requirements.txt"
    )
    set(littlefs_venv "${CMAKE_BINARY_DIR}/LUNASHELL_littlefs_py_venv")
    set(littlefs_venv_stamp "${littlefs_venv}/.ready")
    if(CMAKE_HOST_WIN32)
        set(littlefs_executable "${littlefs_venv}/Scripts/littlefs-python.exe")
        set(littlefs_pip "${littlefs_venv}/Scripts/pip.exe")
    else()
        set(littlefs_executable "${littlefs_venv}/bin/littlefs-python")
        set(littlefs_pip "${littlefs_venv}/bin/pip")
    endif()

    add_custom_command(
        OUTPUT "${littlefs_venv_stamp}"
        BYPRODUCTS "${littlefs_executable}"
        COMMAND "${PYTHON}" -m venv "${littlefs_venv}"
        COMMAND
            "${littlefs_pip}" install
            --disable-pip-version-check
            -r "${littlefs_requirements}"
        COMMAND ${CMAKE_COMMAND} -E touch "${littlefs_venv_stamp}"
        DEPENDS "${littlefs_requirements}"
        COMMENT "Preparing deterministic LittleFS image tooling"
        VERBATIM
    )

    set(image_file "${CMAKE_BINARY_DIR}/${partition}.bin")
    add_custom_command(
        OUTPUT "${image_file}"
        COMMAND
            "${littlefs_executable}" create
            "${base_dir_full_path}"
            "${image_file}"
            -v
            "--fs-size=${partition_size}"
            "--name-max=${CONFIG_LITTLEFS_OBJ_NAME_LEN}"
            --block-size=4096
        DEPENDS
            ${LUNASHELL_LITTLEFS_DEPENDS}
            "${littlefs_venv_stamp}"
        COMMENT "Generating incremental ${partition} LittleFS image"
        VERBATIM
    )
    add_custom_target(
        littlefs_${partition}_bin ALL
        DEPENDS "${image_file}"
    )

    set_property(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        APPEND PROPERTY ADDITIONAL_CLEAN_FILES
        "${image_file};${littlefs_venv}"
    )

    idf_component_get_property(main_flash_args esptool_py FLASH_ARGS)
    idf_component_get_property(sub_flash_args esptool_py FLASH_SUB_ARGS)
    esptool_py_flash_target(
        ${partition}-flash
        "${main_flash_args}"
        "${sub_flash_args}"
    )
    esptool_py_flash_target_image(
        ${partition}-flash
        "${partition}"
        "${partition_offset}"
        "${image_file}"
    )
    add_dependencies(${partition}-flash littlefs_${partition}_bin)

    if(LUNASHELL_LITTLEFS_FLASH_IN_PROJECT)
        esptool_py_flash_target_image(
            flash
            "${partition}"
            "${partition_offset}"
            "${image_file}"
        )
        add_dependencies(flash littlefs_${partition}_bin)
    endif()
endfunction()
