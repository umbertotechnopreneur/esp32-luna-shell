if("$ENV{LUNASHELL_PROFILE_BUILD_KEY}" STREQUAL "ui-minimal")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION AND
       NOT CONFIG_LUNASHELL_BUILD_PROFILE_UI_MINIMAL)
        message(FATAL_ERROR "UI shell profile and Kconfig selection disagree")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/LUNASHELL_app_shell.cmake")
    return()
endif()

idf_component_register(
    SRCS
        "LUNASHELL_alert_service.cpp"
        "LUNASHELL_app.cpp"
        "LUNASHELL_app_documents.cpp"
        "LUNASHELL_adaptive_chrome.cpp"
        "LUNASHELL_adaptive_command_bar.cpp"
        "LUNASHELL_background_asset.cpp"
        "LUNASHELL_edge_navigation.cpp"
        "LUNASHELL_navigation_service.cpp"
        "LUNASHELL_pairing_qr.cpp"
        "LUNASHELL_status_bar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_system_core
        LUNASHELL_platform
    PRIV_REQUIRES
        esp_hw_support
        esp_timer
        LUNASHELL_logger
        qrcode
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
# Keep the Brookesia provider registrar reachable without forcing every archive
# member into the firmware image.
target_link_libraries(
    ${COMPONENT_LIB}
    PUBLIC "-u LUNASHELL_app_provider_symbol"
)

set(LUNASHELL_gui_document_source "${COMPONENT_DIR}/gui/root.json")
idf_build_get_property(
    LUNASHELL_active_sdkconfig_defaults
    SDKCONFIG_DEFAULTS
)
file(
    GLOB_RECURSE
    LUNASHELL_app_package_inputs
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${COMPONENT_DIR}/package/*"
)
list(SORT LUNASHELL_app_package_inputs)
if(NOT LUNASHELL_app_package_inputs)
    message(FATAL_ERROR "LUNASHELL runtime package has no source files")
endif()

set(
    LUNASHELL_gui_verification_inputs
    "${PROJECT_DIR}/CMakeLists.txt"
    "${PROJECT_DIR}/main/Kconfig.projbuild"
    ${LUNASHELL_active_sdkconfig_defaults}
    "${PROJECT_DIR}/components/LUNASHELL_platform/CMakeLists.txt"
    "${PROJECT_DIR}/components/LUNASHELL_platform/LUNASHELL_hardware.cpp"
    "${PROJECT_DIR}/scripts/generate_LUNASHELL_gui_documents.py"
    "${PROJECT_DIR}/scripts/verify_tof_build_gate.py"
    "${LUNASHELL_gui_document_source}"
    "${CMAKE_CURRENT_LIST_FILE}"
    ${LUNASHELL_app_package_inputs}
)
set(
    LUNASHELL_gui_verification_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_verify_lazy_gui_documents.stamp"
)
add_custom_command(
    OUTPUT "${LUNASHELL_gui_verification_stamp}"
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/generate_LUNASHELL_gui_documents.py"
        --check
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/verify_tof_build_gate.py"
    COMMAND
        ${CMAKE_COMMAND} -E touch
        "${LUNASHELL_gui_verification_stamp}"
    DEPENDS ${LUNASHELL_gui_verification_inputs}
    WORKING_DIRECTORY "${PROJECT_DIR}"
    COMMENT "Verifying generated LUNASHELL GUI documents and TOF build gate"
    VERBATIM
)
add_custom_target(
    LUNASHELL_verify_lazy_gui_documents
    DEPENDS "${LUNASHELL_gui_verification_stamp}"
)
add_dependencies(${COMPONENT_LIB} LUNASHELL_verify_lazy_gui_documents)

if(DEFINED BROOKESIA_SYSTEM_CORE_COMPONENT_DIR AND
   NOT "${BROOKESIA_SYSTEM_CORE_COMPONENT_DIR}" STREQUAL "")
    set(brookesia_system_core_dir "${BROOKESIA_SYSTEM_CORE_COMPONENT_DIR}")
else()
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if("espressif__brookesia_system_core" IN_LIST build_components)
        idf_component_get_property(
            brookesia_system_core_dir
            espressif__brookesia_system_core
            COMPONENT_DIR
        )
    else()
        idf_component_get_property(
            brookesia_system_core_dir
            brookesia_system_core
            COMPONENT_DIR
        )
    endif()
endif()

include("${brookesia_system_core_dir}/cmake/runtime_app_stage.cmake")
brookesia_system_core_get_esp_runtime_app_stage_root(LUNASHELL_app_stage_root)
set(LUNASHELL_app_package_id "io.esp32.lunashell")
get_property(
    LUNASHELL_previous_runtime_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)

brookesia_system_core_cleanup_runtime_stage_root("${LUNASHELL_app_stage_root}")
string(MD5 LUNASHELL_app_stage_key "${LUNASHELL_app_stage_root}")
get_property(
    LUNASHELL_app_legacy_cleanup_commands
    GLOBAL PROPERTY
    "BROOKESIA_RUNTIME_APP_STAGE_LEGACY_CLEANUP_${LUNASHELL_app_stage_key}"
)

set(
    LUNASHELL_app_staged_package_dir
    "${LUNASHELL_app_stage_root}/${LUNASHELL_app_package_id}"
)
set(
    LUNASHELL_app_raw_stage_root
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_runtime_app_stage"
)
set(
    LUNASHELL_app_raw_package_dir
    "${LUNASHELL_app_raw_stage_root}/${LUNASHELL_app_package_id}"
)
set(
    LUNASHELL_app_raw_stage_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_stage_runtime_app_package.stamp"
)
set(
    LUNASHELL_app_package_input_manifest
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_runtime_app_package_inputs.txt"
)

set(LUNASHELL_app_package_relative_inputs)
set(LUNASHELL_app_raw_stage_byproducts)
foreach(LUNASHELL_app_package_input IN LISTS LUNASHELL_app_package_inputs)
    file(
        RELATIVE_PATH
        LUNASHELL_app_package_relative_input
        "${COMPONENT_DIR}/package"
        "${LUNASHELL_app_package_input}"
    )
    file(
        TO_CMAKE_PATH
        "${LUNASHELL_app_package_relative_input}"
        LUNASHELL_app_package_relative_input
    )
    list(
        APPEND
        LUNASHELL_app_package_relative_inputs
        "${LUNASHELL_app_package_relative_input}"
    )
    list(
        APPEND
        LUNASHELL_app_raw_stage_byproducts
        "${LUNASHELL_app_raw_package_dir}/${LUNASHELL_app_package_relative_input}"
    )
endforeach()
# File timestamps cover edits; the generated manifest also invalidates the raw
# stage when CONFIGURE_DEPENDS detects that a package file was added or removed.
list(
    JOIN
    LUNASHELL_app_package_relative_inputs
    "\n"
    LUNASHELL_app_package_input_manifest_contents
)
file(
    GENERATE
    OUTPUT "${LUNASHELL_app_package_input_manifest}"
    CONTENT "${LUNASHELL_app_package_input_manifest_contents}\n"
)

add_custom_command(
    OUTPUT "${LUNASHELL_app_raw_stage_stamp}"
    BYPRODUCTS ${LUNASHELL_app_raw_stage_byproducts}
    COMMAND
        ${CMAKE_COMMAND} -E remove_directory
        "${LUNASHELL_app_raw_package_dir}"
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${LUNASHELL_app_raw_stage_root}"
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${COMPONENT_DIR}/package"
        "${LUNASHELL_app_raw_package_dir}"
    COMMAND
        ${CMAKE_COMMAND} -E touch
        "${LUNASHELL_app_raw_stage_stamp}"
    DEPENDS
        "${LUNASHELL_app_package_input_manifest}"
        ${LUNASHELL_app_package_inputs}
    COMMENT "Staging raw LUNASHELL runtime app package"
    VERBATIM
)
set(LUNASHELL_app_package_stage_target LUNASHELL_stage_runtime_app_package)
# A command-free target keeps Brookesia's target contract while the stamp and
# byproducts decide whether the raw copy actually needs to run.
add_custom_target(
    ${LUNASHELL_app_package_stage_target}
    DEPENDS "${LUNASHELL_app_raw_stage_stamp}"
)
set(LUNASHELL_runtime_stage_targets ${LUNASHELL_previous_runtime_stage_targets})
list(APPEND LUNASHELL_runtime_stage_targets ${LUNASHELL_app_package_stage_target})
set_property(
    GLOBAL PROPERTY
    BROOKESIA_RUNTIME_APP_STAGE_TARGETS "${LUNASHELL_runtime_stage_targets}"
)

get_property(
    LUNASHELL_app_package_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
if(LUNASHELL_previous_runtime_stage_targets)
    list(
        REMOVE_ITEM
        LUNASHELL_app_package_stage_targets
        ${LUNASHELL_previous_runtime_stage_targets}
    )
endif()
list(LENGTH LUNASHELL_app_package_stage_targets LUNASHELL_app_package_stage_target_count)
if(NOT LUNASHELL_app_package_stage_target_count EQUAL 1)
    message(
        FATAL_ERROR
        "Expected one LUNASHELL runtime package staging target, found "
        "${LUNASHELL_app_package_stage_target_count}"
    )
endif()

set(
    LUNASHELL_app_package_profile_signature
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_runtime_app_package_profile.txt"
)
set(
    LUNASHELL_app_package_prepare_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_prepare_runtime_app_package.stamp"
)
set_property(
    GLOBAL PROPERTY
    LUNASHELL_RUNTIME_APP_PREPARE_STAMP
    "${LUNASHELL_app_package_prepare_stamp}"
)
set(LUNASHELL_distance_disabled_hardware_variant
    "${LUNASHELL_app_raw_package_dir}/res/.variants/distance_disabled/screen_hardware.json"
)

# The checked-in package contains the complete opt-in UI plus a generated
# hardware-screen variant. Filter only the staged copy so gui/root.json remains
# the single canonical document for both build profiles.
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND
   CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    set(LUNASHELL_app_package_profile "distance_ui=enabled")
    set(LUNASHELL_app_final_stage_byproducts)
    foreach(
        LUNASHELL_app_package_relative_input
        IN LISTS LUNASHELL_app_package_relative_inputs
    )
        if(NOT LUNASHELL_app_package_relative_input MATCHES "^res/\\.variants/")
            list(
                APPEND
                LUNASHELL_app_final_stage_byproducts
                "${LUNASHELL_app_staged_package_dir}/${LUNASHELL_app_package_relative_input}"
            )
        endif()
    endforeach()
    file(
        GENERATE
        OUTPUT "${LUNASHELL_app_package_profile_signature}"
        CONTENT "${LUNASHELL_app_package_profile}\n"
    )
    add_custom_command(
        OUTPUT "${LUNASHELL_app_package_prepare_stamp}"
        BYPRODUCTS ${LUNASHELL_app_final_stage_byproducts}
        ${LUNASHELL_app_legacy_cleanup_commands}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            "${LUNASHELL_app_stage_root}"
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${LUNASHELL_app_staged_package_dir}"
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${LUNASHELL_app_raw_package_dir}"
            "${LUNASHELL_app_staged_package_dir}"
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${LUNASHELL_app_staged_package_dir}/res/.variants"
        COMMAND
            "${PYTHON}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
            "${LUNASHELL_app_staged_package_dir}/res"
        COMMAND
            ${CMAKE_COMMAND} -E touch
            "${LUNASHELL_app_package_prepare_stamp}"
        DEPENDS
            ${LUNASHELL_app_package_stage_targets}
            LUNASHELL_verify_lazy_gui_documents
            "${LUNASHELL_app_raw_stage_stamp}"
            "${LUNASHELL_app_package_profile_signature}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing LUNASHELL runtime package with distance UI"
        VERBATIM
    )
else()
    set(LUNASHELL_app_package_profile "distance_ui=disabled")
    set(LUNASHELL_app_final_stage_byproducts)
    foreach(
        LUNASHELL_app_package_relative_input
        IN LISTS LUNASHELL_app_package_relative_inputs
    )
        if(LUNASHELL_app_package_relative_input MATCHES "^res/\\.variants/" OR
           LUNASHELL_app_package_relative_input STREQUAL "res/screen_tof.json" OR
           LUNASHELL_app_package_relative_input STREQUAL "res/screen_tof_settings.json" OR
           LUNASHELL_app_package_relative_input STREQUAL
               "res/templates/LUNASHELL_settings_slider.json")
            continue()
        endif()
        list(
            APPEND
            LUNASHELL_app_final_stage_byproducts
            "${LUNASHELL_app_staged_package_dir}/${LUNASHELL_app_package_relative_input}"
        )
    endforeach()
    file(
        GENERATE
        OUTPUT "${LUNASHELL_app_package_profile_signature}"
        CONTENT "${LUNASHELL_app_package_profile}\n"
    )
    add_custom_command(
        OUTPUT "${LUNASHELL_app_package_prepare_stamp}"
        BYPRODUCTS ${LUNASHELL_app_final_stage_byproducts}
        ${LUNASHELL_app_legacy_cleanup_commands}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            "${LUNASHELL_app_stage_root}"
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${LUNASHELL_app_staged_package_dir}"
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${LUNASHELL_app_raw_package_dir}"
            "${LUNASHELL_app_staged_package_dir}"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${LUNASHELL_distance_disabled_hardware_variant}"
            "${LUNASHELL_app_staged_package_dir}/res/screen_hardware.json"
        COMMAND
            ${CMAKE_COMMAND} -E rm -f
            "${LUNASHELL_app_staged_package_dir}/res/screen_tof.json"
            "${LUNASHELL_app_staged_package_dir}/res/screen_tof_settings.json"
            "${LUNASHELL_app_staged_package_dir}/res/templates/LUNASHELL_settings_slider.json"
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${LUNASHELL_app_staged_package_dir}/res/.variants"
        COMMAND
            "${PYTHON}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
            "${LUNASHELL_app_staged_package_dir}/res"
        COMMAND
            ${CMAKE_COMMAND} -E touch
            "${LUNASHELL_app_package_prepare_stamp}"
        DEPENDS
            ${LUNASHELL_app_package_stage_targets}
            LUNASHELL_verify_lazy_gui_documents
            "${LUNASHELL_app_raw_stage_stamp}"
            "${LUNASHELL_app_package_profile_signature}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing LUNASHELL runtime package without distance UI"
        VERBATIM
    )
endif()

add_custom_target(
    LUNASHELL_prepare_runtime_app_package
    DEPENDS "${LUNASHELL_app_package_prepare_stamp}"
)

# Keep later consumers on the filtered package target. The root CMakeLists also
# links the already-created LittleFS target here after all components exist:
# LittleFS -> prepare -> stage + verify.
set(LUNASHELL_runtime_stage_targets ${LUNASHELL_previous_runtime_stage_targets})
list(APPEND LUNASHELL_runtime_stage_targets LUNASHELL_prepare_runtime_app_package)
set_property(
    GLOBAL PROPERTY
    BROOKESIA_RUNTIME_APP_STAGE_TARGETS "${LUNASHELL_runtime_stage_targets}"
)
