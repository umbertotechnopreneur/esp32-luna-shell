idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        brookesia_hal_adaptor
        brookesia_gui_lvgl
        brookesia_system_super
        esp32_s3_touch_amoled_1_75
        littlefs
        qsee_app
        qsee_boot
        qsee_logger
        qsee_platform
        fatfs
)

get_property(
    brookesia_runtime_app_stage_targets
    GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS
)
if(brookesia_runtime_app_stage_targets)
    list(REMOVE_DUPLICATES brookesia_runtime_app_stage_targets)
endif()

get_property(
    brookesia_system_super_stage_targets
    GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS
)
if(brookesia_system_super_stage_targets)
    list(REMOVE_DUPLICATES brookesia_system_super_stage_targets)
endif()

littlefs_create_partition_image(
    littlefs_data
    ../littlefs
    FLASH_IN_PROJECT
    DEPENDS
        ${brookesia_runtime_app_stage_targets}
        ${brookesia_system_super_stage_targets}
)
