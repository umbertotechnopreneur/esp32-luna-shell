idf_component_register(
    SRCS
        "qsee_app.cpp"
        "qsee_adaptive_command_bar.cpp"
        "qsee_background_asset.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        brookesia_system_core
        qsee_logger
        qsee_platform
    WHOLE_ARCHIVE
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(
    ${COMPONENT_LIB}
    PUBLIC "-u qsee_app_provider_symbol"
)

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
brookesia_stage_runtime_app_package(
    PACKAGE_ID "ai.qsee.device-console"
    SOURCE_DIR "${COMPONENT_DIR}/package"
    STAGE_ROOT "${qsee_app_stage_root}"
    NO_INDEX
)
