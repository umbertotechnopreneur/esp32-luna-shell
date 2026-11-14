#pragma once
#include <cstdint>
#include "esp_err.h"

// Geometry and RGB565 identity of the simulated Waveshare 1.75C panel.
#define ESP_LCD_COLOR_FORMAT_RGB565 0
#define BSP_LCD_COLOR_FORMAT ESP_LCD_COLOR_FORMAT_RGB565
#define BSP_LCD_H_RES 466
#define BSP_LCD_V_RES 466

// Physical BSP operations used by the unchanged display-settings owner.
extern "C" {
esp_err_t bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int brightness_percent);
int bsp_display_brightness_get();
esp_err_t bsp_display_panel_set_enabled(bool enabled);
}
