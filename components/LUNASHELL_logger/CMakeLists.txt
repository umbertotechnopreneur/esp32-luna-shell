idf_component_register(
    SRCS "LUNASHELL_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos esp_timer heap
)
