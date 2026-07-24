idf_component_register(
    SRCS
        "qsee_hardware.cpp"
        "qsee_radios.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp32_s3_touch_amoled_1_75
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
        bt
        esp_codec_dev
        qmi8658
)
