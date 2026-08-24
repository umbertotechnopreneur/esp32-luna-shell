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

add_custom_target(
    LUNASHELL_verify_lazy_gui_documents ALL
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/generate_LUNASHELL_gui_documents.py"
        --check
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/verify_tof_build_gate.py"
    WORKING_DIRECTORY "${PROJECT_DIR}"
    COMMENT "Verifying generated LUNASHELL GUI documents and TOF build gate"
    VERBATIM
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
set(LUNASHELL_app_package_id "ai.LUNASHELL.device-console")
get_property(
    LUNASHELL_previous_runtime_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
brookesia_stage_runtime_app_package(
    PACKAGE_ID "${LUNASHELL_app_package_id}"
    SOURCE_DIR "${COMPONENT_DIR}/package"
    STAGE_ROOT "${LUNASHELL_app_stage_root}"
    NO_INDEX
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

set(LUNASHELL_app_staged_package_dir
    "${LUNASHELL_app_stage_root}/${LUNASHELL_app_package_id}"
)
set(LUNASHELL_distance_disabled_hardware_variant
    "${LUNASHELL_app_staged_package_dir}/res/.variants/distance_disabled/screen_hardware.json"
)

# The checked-in package contains the complete opt-in UI plus a generated
# hardware-screen variant. Filter only the staged copy so gui/root.json remains
# the single canonical document for both build profiles.
if(CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE AND
   CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    add_custom_target(
        LUNASHELL_prepare_runtime_app_package ALL
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${LUNASHELL_app_staged_package_dir}/res/.variants"
        COMMAND
            "${PYTHON}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
            "${LUNASHELL_app_staged_package_dir}/res"
        DEPENDS
            ${LUNASHELL_app_package_stage_targets}
            LUNASHELL_verify_lazy_gui_documents
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing LUNASHELL runtime package with distance UI"
        VERBATIM
    )
else()
    add_custom_target(
        LUNASHELL_prepare_runtime_app_package ALL
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
        DEPENDS
            ${LUNASHELL_app_package_stage_targets}
            LUNASHELL_verify_lazy_gui_documents
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing LUNASHELL runtime package without distance UI"
        VERBATIM
    )
endif()

# Keep later consumers on the filtered package target. The root CMakeLists also
# links the already-created LittleFS target here after all components exist:
# LittleFS -> prepare -> stage + verify.
set(LUNASHELL_runtime_stage_targets ${LUNASHELL_previous_runtime_stage_targets})
list(APPEND LUNASHELL_runtime_stage_targets LUNASHELL_prepare_runtime_app_package)
set_property(
    GLOBAL PROPERTY
    BROOKESIA_RUNTIME_APP_STAGE_TARGETS "${LUNASHELL_runtime_stage_targets}"
)
