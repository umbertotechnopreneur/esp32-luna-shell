set(REQ esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdmmc fatfs)
set(PRIV_REQ esp_driver_ledc heap)

idf_component_register(
    SRCS "esp32_s3_touch_amoled_1_75.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    REQUIRES ${REQ} esp_lcd
    PRIV_REQUIRES ${PRIV_REQ} esp_timer spiffs esp_psram
)
