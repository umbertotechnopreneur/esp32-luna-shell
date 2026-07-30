idf_component_register(
    SRCS
        "qsee_alert_service.cpp"
        "qsee_app.cpp"
        "qsee_adaptive_chrome.cpp"
        "qsee_adaptive_command_bar.cpp"
        "qsee_background_asset.cpp"
        "qsee_edge_navigation.cpp"
        "qsee_navigation_service.cpp"
        "qsee_pairing_qr.cpp"
        "qsee_status_bar.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_system_core
        esp_hw_support
        qsee_logger
        qsee_platform
        qrcode
    WHOLE_ARCHIVE
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(
    ${COMPONENT_LIB}
    PUBLIC "-u qsee_app_provider_symbol"
)

add_custom_target(
    qsee_verify_lazy_gui_documents ALL
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/generate_qsee_gui_documents.py"
        --check
    COMMAND
        "${PYTHON}"
        "${PROJECT_DIR}/scripts/verify_tof_build_gate.py"
    WORKING_DIRECTORY "${PROJECT_DIR}"
    COMMENT "Verifying generated QSee GUI documents and TOF build gate"
    VERBATIM
)
add_dependencies(${COMPONENT_LIB} qsee_verify_lazy_gui_documents)

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
brookesia_system_core_get_esp_runtime_app_stage_root(qsee_app_stage_root)
set(qsee_app_package_id "ai.qsee.device-console")
get_property(
    qsee_previous_runtime_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
brookesia_stage_runtime_app_package(
    PACKAGE_ID "${qsee_app_package_id}"
    SOURCE_DIR "${COMPONENT_DIR}/package"
    STAGE_ROOT "${qsee_app_stage_root}"
    NO_INDEX
)

get_property(
    qsee_app_package_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
if(qsee_previous_runtime_stage_targets)
    list(
        REMOVE_ITEM
        qsee_app_package_stage_targets
        ${qsee_previous_runtime_stage_targets}
    )
endif()
list(LENGTH qsee_app_package_stage_targets qsee_app_package_stage_target_count)
if(NOT qsee_app_package_stage_target_count EQUAL 1)
    message(
        FATAL_ERROR
        "Expected one QSee runtime package staging target, found "
        "${qsee_app_package_stage_target_count}"
    )
endif()

set(qsee_app_staged_package_dir
    "${qsee_app_stage_root}/${qsee_app_package_id}"
)
set(qsee_distance_disabled_hardware_variant
    "${qsee_app_staged_package_dir}/res/.variants/distance_disabled/screen_hardware.json"
)

# The checked-in package contains the complete opt-in UI plus a generated
# hardware-screen variant. Filter only the staged copy so gui/root.json remains
# the single canonical document for both build profiles.
if(CONFIG_QSEE_BUILD_ENABLE_DISTANCE AND
   CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    add_custom_target(
        qsee_prepare_runtime_app_package ALL
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${qsee_app_staged_package_dir}/res/.variants"
        COMMAND
            "${PYTHON}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
            "${qsee_app_staged_package_dir}/res"
        DEPENDS
            ${qsee_app_package_stage_targets}
            qsee_verify_lazy_gui_documents
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing QSee runtime package with distance UI"
        VERBATIM
    )
else()
    add_custom_target(
        qsee_prepare_runtime_app_package ALL
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${qsee_distance_disabled_hardware_variant}"
            "${qsee_app_staged_package_dir}/res/screen_hardware.json"
        COMMAND
            ${CMAKE_COMMAND} -E rm -f
            "${qsee_app_staged_package_dir}/res/screen_tof.json"
            "${qsee_app_staged_package_dir}/res/screen_tof_settings.json"
            "${qsee_app_staged_package_dir}/res/templates/qsee_settings_slider.json"
        COMMAND
            ${CMAKE_COMMAND} -E remove_directory
            "${qsee_app_staged_package_dir}/res/.variants"
        COMMAND
            "${PYTHON}"
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
            "${qsee_app_staged_package_dir}/res"
        DEPENDS
            ${qsee_app_package_stage_targets}
            qsee_verify_lazy_gui_documents
            "${PROJECT_DIR}/scripts/minify_json_tree.py"
        COMMENT "Preparing QSee runtime package without distance UI"
        VERBATIM
    )
endif()

# Keep later consumers on the filtered package target. The root CMakeLists also
# links the already-created LittleFS target here after all components exist:
# LittleFS -> prepare -> stage + verify.
set(qsee_runtime_stage_targets ${qsee_previous_runtime_stage_targets})
list(APPEND qsee_runtime_stage_targets qsee_prepare_runtime_app_package)
set_property(
    GLOBAL PROPERTY
    BROOKESIA_RUNTIME_APP_STAGE_TARGETS "${qsee_runtime_stage_targets}"
)
