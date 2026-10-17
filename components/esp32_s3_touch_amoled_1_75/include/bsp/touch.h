#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
#include "esp_lcd_touch.h"
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP touch configuration structure
 *
 */
typedef struct {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} bsp_touch_config_t;

typedef struct {
    bool valid;
    bool pressed;
    uint16_t x;
    uint16_t y;
    esp_err_t error;
    int64_t sample_timestamp_us;
} bsp_touch_point_t;

bool bsp_touch_is_ready(void);

#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
// No parameters: observe IRQ wake intent in native and LVGL paths, including standby.
// This never reads hardware, consumes a report, or publishes measured contact data.
uint32_t bsp_touch_interrupt_sequence(void);
#endif

// timeout_ms: Maximum wait for a report; NOT_FOUND is a normal idle timeout.
esp_err_t bsp_touch_wait_for_report(uint32_t timeout_ms);

/**
 * Read one IRQ-triggered report. A successful report without contact is released.
 * ESP_ERR_NOT_FOUND means no new report: valid is false and the timestamp is zero.
 * Failed hardware reads remain invalid with their real error and attempt timestamp.
 * @param point Current sample output; coordinates are meaningful only when pressed and valid.
 */
esp_err_t bsp_touch_read_point(bsp_touch_point_t *point);

#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
/**
 * @param config Controller coordinate transform, independent of any GUI backend.
 * @param ret_touch Initialized controller handle, cleared before a failed attempt.
 */
esp_err_t bsp_touch_new_raw(
    const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch
);
#endif

/**
 * @brief Create new touchscreen
 *
 * If you want to free resources allocated by this function, you can use esp_lcd_touch API, ie.:
 *
 * \code{.c}
 * esp_lcd_touch_del(tp);
 * \endcode
 *
 * @param[in]  config    touch configuration
 * @param[out] ret_touch esp_lcd_touch touchscreen handle
 * @return
 *      - ESP_OK         On success
 *      - Else           esp_lcd_touch failure
 */
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch);
#endif

#ifdef __cplusplus
}
#endif
