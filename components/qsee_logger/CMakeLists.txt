idf_component_register(
    SRCS "qsee_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos esp_timer heap
)
