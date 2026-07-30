if(CONFIG_QSEE_BUILD_ENABLE_DISTANCE AND
   CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART)
    idf_component_register(
        SRCS
            "qsee_tof400f.c"
            "qsee_tof400f_protocol.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_common
        PRIV_REQUIRES esp_driver_uart esp_timer freertos log
    )
else()
    idf_component_register()
endif()
