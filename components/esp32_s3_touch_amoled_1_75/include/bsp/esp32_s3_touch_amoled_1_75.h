#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
#include "driver/sdmmc_host.h"
#include "ff.h"
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#endif
#include "bsp/config.h"
#include "bsp/display.h"

#include "esp_io_expander_tca9554.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
#include "lvgl.h"
#include "esp_lv_adapter.h"
#endif


/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#define BSP_CAPS_DISPLAY        1
#else
#define BSP_CAPS_DISPLAY        0
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
#define BSP_CAPS_TOUCH          1
#else
#define BSP_CAPS_TOUCH          0
#endif
#define BSP_CAPS_BUTTONS        1
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#define BSP_CAPS_AUDIO          1
#define BSP_CAPS_AUDIO_SPEAKER  1
#define BSP_CAPS_AUDIO_MIC      1
#else
#define BSP_CAPS_AUDIO          0
#define BSP_CAPS_AUDIO_SPEAKER  0
#define BSP_CAPS_AUDIO_MIC      0
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
#define BSP_CAPS_SDCARD         1
#else
#define BSP_CAPS_SDCARD         0
#endif
#define BSP_CAPS_IMU            1

/**************************************************************************************************
 * ESP-SparkBot-BSP pinout
 **************************************************************************************************/

/* I2C */
#define BSP_I2C_SCL           (GPIO_NUM_14)
#define BSP_I2C_SDA           (GPIO_NUM_15)

#define BSP_I2S_SCLK          (GPIO_NUM_9)
#define BSP_I2S_MCLK          (GPIO_NUM_42)
#define BSP_I2S_LCLK          (GPIO_NUM_45)
#define BSP_I2S_DOUT          (GPIO_NUM_8)
#define BSP_I2S_DSIN          (GPIO_NUM_10)
#define BSP_POWER_AMP_IO      (GPIO_NUM_46)

/* Display */
#define BSP_LCD_CS        (GPIO_NUM_12)
#define BSP_LCD_PCLK      (GPIO_NUM_38)
#define BSP_LCD_DATA0     (GPIO_NUM_4)
#define BSP_LCD_DATA1     (GPIO_NUM_5)
#define BSP_LCD_DATA2     (GPIO_NUM_6)
#define BSP_LCD_DATA3     (GPIO_NUM_7)

#define BSP_LCD_BACKLIGHT     (GPIO_NUM_NC)
#define BSP_LCD_RST           (GPIO_NUM_39)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_40)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_11)

/* uSD card */
#define BSP_SD_D0            (GPIO_NUM_3)
#define BSP_SD_CMD           (GPIO_NUM_1)
#define BSP_SD_CLK           (GPIO_NUM_2)

#define BSP_IO_EXPANDER_I2C_ADDRESS     (ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000)

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
#define LVGL_BUFFER_HEIGHT          (CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 *
 * I2C interface
 *
 * There are two devices connected to I2C peripheral:
 *  - QMA7981 Inertial measurement unit
 *  - OV2640 Camera module
 **************************************************************************************************/
#define BSP_I2C_NUM     CONFIG_BSP_I2C_NUM

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Get I2C driver handle
 *
 * @param[out] out_handle Initialized I2C master bus handle
 * @return ESP_OK or the exact I2C initialization error
 *
 */
esp_err_t bsp_i2c_get_handle(i2c_master_bus_handle_t *out_handle);


#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
/**************************************************************************************************
 *
 * I2S audio interface
 *
 * There are two devices connected to the I2S peripheral:
 *  - Codec ES8311 for output(playback) and input(recording) path
 *
 * bsp_audio_codec_init() creates the complete speaker + microphone route as
 * one transaction. After initialization, use esp_codec_dev for play/record.
 * Example audio play:
 * \code{.c}
 * esp_codec_dev_set_out_vol(spk_codec_dev, DEFAULT_VOLUME);
 * esp_codec_dev_open(spk_codec_dev, &fs);
 * esp_codec_dev_write(spk_codec_dev, wav_bytes, bytes_read_from_spiffs);
 * esp_codec_dev_close(spk_codec_dev);
 * \endcode
 **************************************************************************************************/

/**
 * @brief Init audio
 *
 * @param[in]  i2s_config I2S configuration. Pass NULL to use default values (Mono, duplex, 16bit, 22050 Hz)
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_NOT_SUPPORTED The communication mode is not supported on the current chip
 *      - ESP_ERR_INVALID_ARG   NULL pointer or invalid configuration
 *      - ESP_ERR_NOT_FOUND     No available I2S channel found
 *      - ESP_ERR_NO_MEM        No memory for storing the channel information
 *      - ESP_ERR_INVALID_STATE This channel has not initialized or already started
 */
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);

/**
 * @brief Suspend or resume the initialized I2S transport for system standby
 *
 * Codec streams must be closed before entering standby. The codec route and
 * I2S channels remain allocated so resume does not recreate ownership. Calls
 * that request the current state are idempotent.
 *
 * @param[in] standby true to stop TX/RX, false to restore selected directions
 * @param[in] resume_tx true to restore TX when leaving standby; ignored while
 *                      entering standby
 * @param[in] resume_rx true to restore RX when leaving standby; ignored while
 *                      entering standby
 * @return ESP_OK or the exact I2S driver error. A failed transition is rolled
 *         back to its previous state when possible.
 */
esp_err_t bsp_audio_set_transport_standby(
    bool standby,
    bool resume_tx,
    bool resume_rx
);

/**
 * @brief Initialize the complete speaker and microphone codec route
 *
 * @param[out] out_speaker Initialized ES8311 codec device
 * @param[out] out_microphone Initialized ES7210 codec device
 * @return ESP_OK or the exact initialization error. Outputs remain NULL on
 *         failure.
 */
esp_err_t bsp_audio_codec_init(
    esp_codec_dev_handle_t *out_speaker,
    esp_codec_dev_handle_t *out_microphone
);

/**
 * @brief Close and release all BSP-owned codec and I2S resources
 *
 * @return ESP_OK when cleanup is complete. A cleanup failure poisons the
 *         audio route and is returned explicitly.
 */
esp_err_t bsp_audio_codec_deinit(void);
#endif

/**************************************************************************************************
 *
 * SPIFFS
 *
 * After mounting the SPIFFS, it can be accessed with stdio functions ie.:
 * \code{.c}
 * FILE* f = fopen(BSP_SPIFFS_MOUNT_POINT"/hello.txt", "w");
 * fprintf(f, "Hello World!\n");
 * fclose(f);
 * \endcode
 **************************************************************************************************/
#define BSP_SPIFFS_MOUNT_POINT      CONFIG_BSP_SPIFFS_MOUNT_POINT

/**
 * @brief Mount SPIFFS to virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if esp_vfs_spiffs_register was already called
 *      - ESP_ERR_NO_MEM if memory can not be allocated
 *      - ESP_FAIL if partition can not be mounted
 *      - other error codes
 */
esp_err_t bsp_spiffs_mount(void);

/**
 * @brief Unmount SPIFFS from virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already unmounted
 */
esp_err_t bsp_spiffs_unmount(void);

#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
/**************************************************************************************************
 *
 * uSD card
 *
 * After mounting the uSD card, it can be accessed with stdio functions ie.:
 * \code{.c}
 * FILE* f = fopen(BSP_MOUNT_POINT"/hello.txt", "w");
 * fprintf(f, "Hello %s!\n", bsp_sdcard->cid.name);
 * fclose(f);
 * \endcode
 **************************************************************************************************/
#define BSP_SD_MOUNT_POINT      CONFIG_BSP_SD_MOUNT_POINT
extern sdmmc_card_t *bsp_sdcard;

typedef struct {
    int32_t manufacturer_id;
    int32_t oem_id;
    char product_name[8];
    int32_t revision;
    int32_t serial;
    int32_t manufacturing_date;
    uint64_t capacity_bytes;
} bsp_sdcard_identity_t;

typedef struct {
    /**
     * Single-use permission for this call only. When true,
     * expected_identity_valid must also be true.
     */
    bool format_authorized;
    bool expected_identity_valid;
    bsp_sdcard_identity_t expected_identity;
} bsp_sdcard_mount_request_t;

typedef enum {
    BSP_SDCARD_MOUNT_OUTCOME_MOUNTED = 0,
    BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED,
    BSP_SDCARD_MOUNT_OUTCOME_FORMAT_REQUIRED,
    BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED,
    BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED,
    BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED,
} bsp_sdcard_mount_outcome_t;

typedef enum {
    BSP_SDCARD_STAGE_NONE = 0,
    BSP_SDCARD_STAGE_REQUEST,
    BSP_SDCARD_STAGE_HOST_INIT,
    BSP_SDCARD_STAGE_SLOT_INIT,
    BSP_SDCARD_STAGE_CARD_INIT,
    BSP_SDCARD_STAGE_CARD_IDENTITY,
    BSP_SDCARD_STAGE_DRIVE_ALLOC,
    BSP_SDCARD_STAGE_DISKIO_REGISTER,
    BSP_SDCARD_STAGE_VFS_REGISTER,
    BSP_SDCARD_STAGE_FAT_MOUNT,
    BSP_SDCARD_STAGE_PRE_FORMAT_UNMOUNT,
    BSP_SDCARD_STAGE_PARTITION,
    BSP_SDCARD_STAGE_FORMAT,
    BSP_SDCARD_STAGE_POST_FORMAT_MOUNT,
} bsp_sdcard_mount_stage_t;

typedef enum {
    BSP_SDCARD_CLEANUP_NONE = 0,
    BSP_SDCARD_CLEANUP_FAT_UNMOUNT,
    BSP_SDCARD_CLEANUP_VFS_UNREGISTER,
    BSP_SDCARD_CLEANUP_HOST_DEINIT,
} bsp_sdcard_cleanup_stage_t;

typedef struct {
    bsp_sdcard_mount_outcome_t outcome;
    bsp_sdcard_mount_stage_t stage;
    esp_err_t error;

    FRESULT initial_mount_result;
    bool initial_mount_result_valid;
    FRESULT fatfs_result;
    bool fatfs_result_valid;

    esp_err_t cleanup_error;
    bsp_sdcard_cleanup_stage_t cleanup_stage;
    FRESULT cleanup_fatfs_result;
    bool cleanup_fatfs_result_valid;
    bool cleanup_complete;
    bool poisoned;

    bool card_initialized;
    bool observed_identity_valid;
    bsp_sdcard_identity_t observed_identity;

    bool authorization_consumed;
    bool authorization_identity_matched;
    bool format_attempted;   /**< Destructive sequence entered; partial writes are possible on failure. */
    bool partition_written;  /**< f_fdisk completed successfully. */
    bool formatted;          /**< f_mkfs completed successfully, even if the following mount failed. */
    bool mounted;
} bsp_sdcard_mount_result_t;

typedef struct {
    esp_err_t error;
    bsp_sdcard_cleanup_stage_t stage;
    FRESULT fatfs_result;
    bool fatfs_result_valid;
    bool complete;
    bool poisoned;
} bsp_sdcard_unmount_result_t;

/**
 * @brief Probe and mount the removable microSD card.
 *
 * Formatting is considered only when the immediate FAT probe returns exactly
 * FR_NO_FILESYSTEM and the single-use authorization identity matches the
 * initialized card field by field. An authorization is consumed by this call
 * even when the card is absent, its identity differs, initialization fails, or
 * formatting is not required.
 *
 * @param request Mount request and optional identity-bound format permission.
 * @param[out] out_result Exact stage, ESP-IDF error, FatFs result, formatting
 *                        progress, and cleanup state.
 * @return ESP_OK only when the card is mounted; otherwise the primary
 *         ESP-IDF error or the specific ESP-IDF mapping of the exact FRESULT
 *         retained in out_result.
 */
esp_err_t bsp_sdcard_mount(
    const bsp_sdcard_mount_request_t *request,
    bsp_sdcard_mount_result_t *out_result
);

/**
 * @brief Unmount the microSD card and release all BSP-owned resources.
 *
 * A failed cleanup poisons the BSP storage state. Further mount, status, and
 * unmount calls then fail with ESP_ERR_INVALID_STATE instead of attempting an
 * ambiguous recovery.
 *
 * @param[out] out_result Exact cleanup stage, FatFs result, and ownership
 *                        state. The result is populated on every return path.
 */
esp_err_t bsp_sdcard_unmount(bsp_sdcard_unmount_result_t *out_result);

/**
 * @brief Check the currently mounted card through the SDMMC protocol.
 *
 * @return ESP_OK when the mounted card responds, the exact sdmmc_get_status()
 *         error, or ESP_ERR_INVALID_STATE when no usable mount exists.
 */
esp_err_t bsp_sdcard_get_status(void);
#endif

/**
 * @brief Initialize or get the IO expander chip TCA9554
 *
 * @param[out] out_handle Initialized expander handle
 * @return ESP_OK or the exact I2C/driver error
 */
esp_err_t bsp_io_expander_init(
    esp_io_expander_handle_t *out_handle
);


/**************************************************************************************************
 *
 * LCD interface
 *
 * The UI feature owns LVGL and its adapter. With UI disabled, bsp/display.h
 * exposes the same panel hardware through standalone transactions.
 * Take bsp_display_lock() before shared display operations and release it with
 * bsp_display_unlock(). The lock follows the selected display owner.
 **************************************************************************************************/
#define BSP_LCD_SPI_NUM            (SPI2_HOST)

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI

#define BSP_DISPLAY_LVGL_PAUSE_TIMEOUT_MS (1000U)

typedef void (*bsp_touch_activity_callback_t)(void *user_data);

/**
 * @brief BSP display configuration structure
 */
typedef struct {
    esp_lv_adapter_config_t          lv_adapter_cfg;
    esp_lv_adapter_rotation_t        rotation;
    esp_lv_adapter_tear_avoid_mode_t tear_avoid_mode;
    struct {
        unsigned int swap_xy;  /*!< Swap X and Y after read coordinates */
        unsigned int mirror_x; /*!< Mirror X after read coordinates */
        unsigned int mirror_y; /*!< Mirror Y after read coordinates */
    } touch_flags;
} bsp_display_cfg_t;

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 *
 * @param[out] out_display Initialized LVGL display
 * @return ESP_OK or the exact startup error
 */
esp_err_t bsp_display_start(lv_display_t **out_display);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 * @param[out] out_display Initialized LVGL display
 *
 * @return ESP_OK or the exact startup error
 */
esp_err_t bsp_display_start_with_config(
    const bsp_display_cfg_t *cfg,
    lv_display_t **out_display
);

/**
 * @brief Get pointer to input device (touch, buttons, ...)
 *
 * @note The LVGL input device is initialized in bsp_display_start() function.
 *
 * @return Pointer to LVGL input device or NULL when not initialized
 */
lv_indev_t *bsp_display_get_input_dev(void);

/**
 * @brief Register the product-level observer for physical touch contacts.
 *
 * The callback runs from the LVGL input event context and must not block.
 * Passing NULL removes the observer without disabling the touch device.
 */
esp_err_t bsp_display_set_touch_activity_callback(
    bsp_touch_activity_callback_t callback,
    void *user_data
);

/**
 * @brief Pause the LVGL worker and its periodic tick for display standby.
 *
 * The pinned adapter is patched to clear its acknowledgement before publishing
 * the pause request, roll the worker back on timeout, and propagate tick-timer
 * errors. Completion is bounded by BSP_DISPLAY_LVGL_PAUSE_TIMEOUT_MS. Repeated
 * calls are idempotent. This API must be paired with bsp_display_lvgl_resume()
 * and must not be mixed with sleep_prepare().
 *
 * @return ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE, or the exact
 *         esp_lv_adapter_pause() error
 */
esp_err_t bsp_display_lvgl_pause(void);

/**
 * @brief Resume the LVGL worker and periodic tick after display standby.
 *
 * Repeated calls are idempotent. A failed resume retains either the verified
 * paused state or a faulted-paused state; only resume can retry the latter, so
 * another pause cannot convert an ambiguous timer state into success.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE, or the exact
 *         esp_lv_adapter_resume() error
 */
esp_err_t bsp_display_lvgl_resume(void);

#endif // CONFIG_LUNASHELL_BUILD_ENABLE_UI

#ifdef __cplusplus
}
#endif
