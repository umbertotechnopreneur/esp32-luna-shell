

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_types.h"
#include "esp_err.h"

/* LCD color formats */
#define ESP_LCD_COLOR_FORMAT_RGB565    (1)
#define ESP_LCD_COLOR_FORMAT_RGB888    (2)

/* LCD display color format */
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB565)
/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)
/* LCD display color space */
#define BSP_LCD_COLOR_SPACE         (LCD_RGB_ELEMENT_ORDER_RGB)
/* LCD display definition */
#define BSP_LCD_H_RES              (466)
#define BSP_LCD_V_RES              (466)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    int max_transfer_sz;    /*!< Maximum transfer size, in bytes. */
} bsp_display_config_t;

typedef enum {
    BSP_DISPLAY_ROTATE_UNAVAILABLE = -1,
    BSP_DISPLAY_ROTATE_0   = 0,
    BSP_DISPLAY_ROTATE_90  = 1,
    BSP_DISPLAY_ROTATE_180 = 2,
    BSP_DISPLAY_ROTATE_270 = 3,
} bsp_display_rotation_t;

/** Initialize enabled panel/touch, clear a black frame while dark, then enable output. */
esp_err_t bsp_display_base_start(void);

/** True only after the panel has completed its initialization. */
bool bsp_display_is_ready(void);

/**
 * Atomic content generation, zero before panel initialization or when excluded.
 * Successful rotation and panel wake change the generation; standalone renderers must
 * redraw the entire frame when it changes, including while otherwise idle.
 */
uint32_t bsp_display_content_revision(void);

/**
 * Write a packed RGB565 rectangle synchronously, with exclusive end coordinates.
 * Pixels are native little-endian RGB565; the driver converts to panel wire order.
 * Source memory may be PSRAM and can be reused immediately after this call returns.
 * This API is available only when the legacy UI is disabled. A transfer timeout
 * retains its DMA buffer and rejects later writes until an explicit reset.
 * @param x_start First column included in the rectangle.
 * @param y_start First row included in the rectangle.
 * @param x_end First column after the rectangle.
 * @param y_end First row after the rectangle.
 * @param pixels Packed native RGB565 source, containing the complete rectangle.
 */
esp_err_t bsp_display_write_rgb565(
    int x_start, int y_start, int x_end, int y_end, const void *pixels
);

// Bounded standalone frame ownership. No GUI headers are admitted here.
typedef struct {
    uint32_t duration_us;
    uint32_t rectangles;
    uint32_t retained_dma_bytes;
    esp_err_t error;
} bsp_display_frame_result_t;

// budget_ms: Complete-frame budget in 1..1000 ms, starting after lock acquisition.
esp_err_t bsp_display_frame_begin(uint32_t budget_ms);
// Check the current owner's aggregate deadline, including geometry and raster work.
esp_err_t bsp_display_frame_check_deadline(void);
// frame_error: Original render/transfer failure; success still checks the deadline.
// result: Receives measured duration and quarantined DMA bytes before lock release.
esp_err_t bsp_display_frame_end(esp_err_t frame_error, bsp_display_frame_result_t *result);

/**
 * @param timeout_ms Maximum wait in milliseconds; standalone calls accept at most 1000 ms.
 */
esp_err_t bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

/**
 * Rotation failed and its rollback could not restore one coherent panel/touch
 * mapping. This terminal result must not be retried until display state is
 * explicitly restored.
 */
#define BSP_DISPLAY_ERR_ROTATION_INCONSISTENT ESP_ERR_INVALID_RESPONSE

/**
 * @brief Create new display panel
 *
 * This function resets and initializes the panel and its transport. Standalone
 * initialization leaves output disabled until bsp_display_base_start() clears
 * the initial frame. The existing Luna UI path retains its enabled-output
 * startup behavior. Shared writes and control use the BSP transaction APIs.
 *
 * If you want to free resources allocated by this function, you can use esp_lcd API, ie.:
 *
 * \code{.c}
 * esp_lcd_panel_del(panel);
 * esp_lcd_panel_io_del(io);
 * spi_bus_free(spi_num_from_configuration);
 * \endcode
 *
 * @param[in]  config    display configuration
 * @param[out] ret_panel esp_lcd panel handle
 * @param[out] ret_io    esp_lcd IO handle
 * @return
 *      - ESP_OK         On success
 *      - Else           esp_lcd failure
 */
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief Initialize display's brightness
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_brightness_init(void);

/**
 * @brief Rotate the CO5300 scan direction and matching CST9217 coordinates.
 *
 * Rendering keeps its logical coordinates. The operation is serialized with
 * display writes and rejected while a pointer is pressed. The legacy UI redraws
 * after rotation; standalone callers must redraw their complete frame.
 * A failed rollback returns BSP_DISPLAY_ERR_ROTATION_INCONSISTENT and blocks
 * subsequent changes.
 */
esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation);
bsp_display_rotation_t bsp_display_rotation_get(void);
/** Distinguish a failed-but-rolled-back call from a terminal mapping failure. */
bool bsp_display_rotation_is_consistent(void);

/**
 * @brief Set display's brightness
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @param[in] brightness_percent Brightness in [%]
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);

int bsp_display_brightness_get(void);

/**
 * @brief Enable or disable CO5300 panel output.
 *
 * Unlike brightness zero, disabling output sends the controller's display-off
 * command so the AMOLED panel can enter its lower-power blank state.
 */
esp_err_t bsp_display_panel_set_enabled(bool enabled);

/**
 * @brief Turn on display backlight
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_backlight_on(void);

/**
 * @brief Turn off display backlight
 *
 * Brightness is controlled with PWM signal to a pin controlling backlight.
 * Brightness must be already initialized by calling bsp_display_brightness_init() or bsp_display_new()
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   Parameter error
 */
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
