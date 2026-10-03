idf_component_register(
    SRCS "LUNASHELL_logger.c" "LUNASHELL_logger_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos esp_timer heap esp_driver_usb_serial_jtag esp_rom esp_app_format esp_system esp_hw_support
)
