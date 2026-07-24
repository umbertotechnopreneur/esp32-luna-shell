idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        brookesia_core
        esp32_s3_touch_amoled_1_75
        qsee_app
        qsee_boot
        qsee_logger
        qsee_platform
)
