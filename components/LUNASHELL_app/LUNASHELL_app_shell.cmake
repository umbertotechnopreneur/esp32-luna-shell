# The UI baseline compiles a single visual app and stages only its one screen.
idf_component_register(
    SRCS "LUNASHELL_app_shell.cpp"
    INCLUDE_DIRS "include"
    REQUIRES brookesia_system_core LUNASHELL_platform
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(
    ${COMPONENT_LIB}
    PUBLIC "-u LUNASHELL_app_provider_symbol"
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
brookesia_system_core_get_esp_runtime_app_stage_root(LUNASHELL_app_stage_root)
brookesia_system_core_cleanup_runtime_stage_root("${LUNASHELL_app_stage_root}")

set(LUNASHELL_shell_source "${COMPONENT_DIR}/package_shell/res/root.json")
set(LUNASHELL_shell_stage_dir "${LUNASHELL_app_stage_root}/io.esp32.lunashell")
set(LUNASHELL_shell_stamp
    "${CMAKE_CURRENT_BINARY_DIR}/LUNASHELL_prepare_runtime_app_package.stamp"
)
add_custom_command(
    OUTPUT "${LUNASHELL_shell_stamp}"
    BYPRODUCTS "${LUNASHELL_shell_stage_dir}/res/root.json"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${LUNASHELL_shell_stage_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LUNASHELL_shell_stage_dir}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LUNASHELL_shell_source}"
        "${LUNASHELL_shell_stage_dir}/res/root.json"
    COMMAND "${PYTHON}" "${PROJECT_DIR}/scripts/minify_json_tree.py"
        "${LUNASHELL_shell_stage_dir}/res"
    COMMAND ${CMAKE_COMMAND} -E touch "${LUNASHELL_shell_stamp}"
    DEPENDS
        "${LUNASHELL_shell_source}"
        "${PROJECT_DIR}/scripts/minify_json_tree.py"
        "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Preparing minimal display and touch app package"
    VERBATIM
)
add_custom_target(
    LUNASHELL_prepare_runtime_app_package
    DEPENDS "${LUNASHELL_shell_stamp}"
)
set_property(
    GLOBAL PROPERTY
    LUNASHELL_RUNTIME_APP_PREPARE_STAMP
    "${LUNASHELL_shell_stamp}"
)
set_property(
    GLOBAL PROPERTY
    BROOKESIA_RUNTIME_APP_STAGE_TARGETS
    "LUNASHELL_prepare_runtime_app_package"
)
