set(LUNASHELL_main_srcs)
set(LUNASHELL_main_requires
    littlefs
    LUNASHELL_logger
    LUNASHELL_platform
    fatfs
)

if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    list(APPEND LUNASHELL_main_srcs "main.cpp")
    list(APPEND LUNASHELL_main_requires
        brookesia_hal_adaptor
        brookesia_gui_lvgl
        brookesia_lib_utils
        brookesia_system_core
        esp32_s3_touch_amoled_1_75
        LUNASHELL_app
    )
    if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI OR CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
        list(APPEND LUNASHELL_main_requires LUNASHELL_boot)
    endif()
else()
    list(APPEND LUNASHELL_main_srcs "main_headless.cpp")
endif()

idf_component_register(
    SRCS ${LUNASHELL_main_srcs}
    INCLUDE_DIRS "."
    REQUIRES ${LUNASHELL_main_requires}
)

# ESP-IDF discovers the component graph before Kconfig values are available.
# Attach profile-specific public dependencies in the normal evaluation pass so
# the full build receives their include and link interfaces.
if(CONFIG_LUNASHELL_BUILD_ENABLE_UI)
    idf_component_optional_requires(
        PUBLIC
        brookesia_hal_adaptor
        brookesia_gui_lvgl
        brookesia_lib_utils
        brookesia_system_core
        esp32_s3_touch_amoled_1_75
        LUNASHELL_app
    )
    if(CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI OR CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO)
        idf_component_optional_requires(PUBLIC LUNASHELL_boot)
    endif()
endif()

get_property(
    brookesia_runtime_app_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
if(brookesia_runtime_app_stage_targets)
    list(REMOVE_DUPLICATES brookesia_runtime_app_stage_targets)
endif()

get_filename_component(
    brookesia_runtime_mount_root
    "${CMAKE_CURRENT_LIST_DIR}/../littlefs"
    ABSOLUTE
)

# System Core needs only the LUNASHELL app package. Remove stale System Super
# resources left by older configurations before every LittleFS image.
file(REMOVE_RECURSE "${brookesia_runtime_mount_root}/system")
add_custom_target(LUNASHELL_remove_legacy_super_stage ALL
    COMMAND
        ${CMAKE_COMMAND} -E remove_directory
        "${brookesia_runtime_mount_root}/system"
    COMMENT "Removing legacy Brookesia System Super runtime resources"
    VERBATIM
)

littlefs_create_partition_image(
    littlefs_data
    ../littlefs
    FLASH_IN_PROJECT
    DEPENDS
        LUNASHELL_remove_legacy_super_stage
        ${brookesia_runtime_app_stage_targets}
)
