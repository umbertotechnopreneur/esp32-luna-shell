#include "sdkconfig.h"
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#include "esp_lcd_panel_io_additions.h"
#include "driver/spi_master.h"
#endif

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_system.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "sdmmc_cmd.h"
#endif
#include "esp_spiffs.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#include "esp_lcd_co5300.h"
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
#include "esp_lcd_touch_cst9217.h"
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#include "esp_codec_dev_defaults.h"
#endif
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "bsp/display.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
#include "bsp/display_qspi_io.h"
#endif
#include "bsp/runtime_cores.h"
#include "bsp/touch.h"
#include "bsp_display_dma.h"

static const char *TAG = "ESP32-S3-Touch-AMOLED-1.75";

static i2c_master_bus_handle_t i2c_handle = NULL; // I2C Handle
static bool i2c_initialized = false;
static esp_io_expander_handle_t io_expander = NULL; // IO expander tca9554 handle
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
static bool display_initialized;
static uint32_t display_content_revision;
static bsp_display_rotation_t current_rotation = BSP_DISPLAY_ROTATE_0;
static bool display_rotation_consistent = true;
static bool display_panel_output_enabled = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
static lv_display_t *display_handle = NULL;
static lv_indev_t *disp_indev = NULL;
static bsp_touch_activity_callback_t touch_activity_callback = NULL;
static void *touch_activity_user_data = NULL;
static uint32_t touch_interrupt_sequence;
static uint8_t *display_frame_cache = NULL;
static bool display_frame_cache_rows[BSP_LCD_V_RES];
static size_t display_frame_cache_full_row_count = 0;
static bool display_frame_cache_valid = false;
typedef enum {
    BSP_DISPLAY_LVGL_ACTIVE = 0,
    BSP_DISPLAY_LVGL_PAUSING,
    BSP_DISPLAY_LVGL_PAUSED,
    BSP_DISPLAY_LVGL_FAULTED_PAUSED,
    BSP_DISPLAY_LVGL_RESUMING,
} bsp_display_lvgl_state_t;
static bsp_display_lvgl_state_t display_lvgl_state = BSP_DISPLAY_LVGL_ACTIVE;
static portMUX_TYPE display_lvgl_state_lock = portMUX_INITIALIZER_UNLOCKED;
#else
static StaticSemaphore_t display_mutex_storage;
static SemaphoreHandle_t display_mutex;
static StaticSemaphore_t display_completion_storage;
static SemaphoreHandle_t display_completion;
static volatile bool display_completion_fault;
static bool display_dma_faulted;
#endif
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
sdmmc_card_t *bsp_sdcard = NULL; // Global uSD card handler
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// touch_handle: Adapter-owned hardware controller; this observer never reads it.
// user_context: Unused observer context; no allocation or task work runs in the ISR.
static void IRAM_ATTR bsp_touch_wake_interrupt(
    esp_lcd_touch_handle_t touch_handle,
    void *user_context
)
{
    (void)touch_handle;
    (void)user_context;
    // The GPIO ISR remains live while LVGL is paused. This is wake intent,
    // not a measured contact; only the normal input owner may publish a touch.
    __atomic_add_fetch(&touch_interrupt_sequence, 1U, __ATOMIC_RELEASE);
}

// No parameters: expose raw IRQ generation without consuming the input owner's report.
uint32_t bsp_touch_interrupt_sequence(void)
{
    return __atomic_load_n(&touch_interrupt_sequence, __ATOMIC_ACQUIRE);
}

// event: Existing LVGL input event used to publish physical activity intent.
static void bsp_touch_activity_event_cb(lv_event_t *event)
{
    (void)event;
    bsp_touch_activity_callback_t callback = touch_activity_callback;
    if (callback != NULL) {
        callback(touch_activity_user_data);
    }
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
#define BSP_SDCARD_MAX_FILES               5
#define BSP_SDCARD_ALLOCATION_UNIT_SIZE    (16U * 1024U)
#define BSP_SDCARD_FORMAT_WORK_BUFFER_SIZE 4096U
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI && CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH < BSP_DISPLAY_DMA_PIPELINE_DEPTH
#error "LCD transaction queue must hold the display DMA pipeline"
#endif

_Static_assert(
    (BSP_DISPLAY_DMA_ALIGNMENT & (BSP_DISPLAY_DMA_ALIGNMENT - 1U)) == 0U,
    "Display DMA alignment must be a power of two"
);
_Static_assert(
    BSP_DISPLAY_DMA_EMERGENCY_BYTES >= BSP_DISPLAY_DMA_ROW_BYTES,
    "Display DMA emergency slot must hold one full row"
);



#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
// A failed completion must retain memory that the SPI peripheral may still own.
static bsp_display_dma_lease_t display_dma_fault_lease;
static uint32_t display_dma_retained_bytes;
typedef struct {
    TaskHandle_t owner;
    bsp_display_dma_lease_t lease;
    int64_t started_at_us;
    int64_t deadline_us;
    uint32_t rectangles;
    uint32_t conversion_us;
    uint32_t submission_us;
    uint32_t completion_wait_us;
    size_t render_slot;
    size_t pending_slot;
    bool transfer_pending;
    uint16_t buffer_count;
    uint16_t transfer_rows;
    esp_err_t completion_error;
    esp_err_t error;
} bsp_display_frame_t;
static bsp_display_frame_t display_frame;
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
static uint32_t display_dma_transfer_failure_count;
#endif
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
typedef struct {
    bool host_initialized;
    bool slot_initialized;
    bool diskio_registered;
    bool vfs_registered;
    bool fatfs_bound;
    bool mounted;
    bool poisoned;
    BYTE pdrv;
    char drive[3];
    FATFS *fs;
    sdmmc_card_t *card;
    sdmmc_host_t host;
} bsp_sdcard_context_t;

typedef struct {
    esp_err_t error;
    bsp_sdcard_cleanup_stage_t stage;
    FRESULT fatfs_result;
    bool fatfs_result_valid;
    bool complete;
} bsp_sdcard_cleanup_report_t;

static bsp_sdcard_context_t sdcard_context = {
    .pdrv = FF_DRV_NOT_USED,
};
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
static esp_lcd_touch_handle_t tp = NULL;
static esp_lcd_panel_io_handle_t touch_io_handle = NULL;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
static esp_lcd_panel_io_handle_t io_handle = NULL;
static bool display_spi_bus_initialized = false;
uint8_t brightness;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
#define BSP_AUDIO_HAS_OUTPUT 1
#else
#define BSP_AUDIO_HAS_OUTPUT 0
#endif
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
#define BSP_AUDIO_HAS_INPUT 0
#else
#define BSP_AUDIO_HAS_INPUT 1
#endif
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t *i2s_data_if = NULL; /* Codec data interface */
static bool i2s_tx_enabled;
static bool i2s_rx_enabled;
static bool audio_transport_standby;
static bool audio_resources_poisoned;
static esp_codec_dev_handle_t speaker_codec_device;
static esp_codec_dev_handle_t microphone_codec_device;
static const audio_codec_gpio_if_t *speaker_gpio_interface;
static const audio_codec_ctrl_if_t *speaker_control_interface;
static const audio_codec_if_t *speaker_codec_interface;
static const audio_codec_ctrl_if_t *microphone_control_interface;
static const audio_codec_if_t *microphone_codec_interface;
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#define BSP_ES7210_CODEC_ADDR ES7210_CODEC_DEFAULT_ADDR
#define BSP_I2S_GPIO_CFG       \
    {                          \
        .mclk = BSP_I2S_MCLK,  \
        .bclk = BSP_I2S_SCLK,  \
        .ws = BSP_I2S_LCLK,    \
        .dout = BSP_I2S_DOUT,  \
        .din = BSP_I2S_DSIN,   \
        .invert_flags = {      \
            .mclk_inv = false, \
            .bclk_inv = false, \
            .ws_inv = false,   \
        },                     \
    }
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
// Set the command's brightness before init to avoid exposing old panel RAM on wake.
static uint8_t initial_panel_brightness = 0xFF;
static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, &initial_panel_brightness, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    {0x29, NULL, 0, 0},
#endif
};
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
#define BSP_I2S_DUPLEX_STEREO_CFG(_sample_rate)                                                         \
    {                                                                                                   \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                            \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG,                                                                   \
    }
#endif

/**************************************************************************************************
 *
 * I2C Function
 *
 **************************************************************************************************/
esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
    };
    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&i2c_bus_conf, &i2c_handle),
        TAG,
        "I2C bus initialization failed"
    );

    i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (!i2c_initialized || i2c_handle == NULL) {
        ESP_LOGE(TAG, "I2C deinitialization rejected: bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(
        i2c_del_master_bus(i2c_handle),
        TAG,
        "I2C bus deinitialization failed"
    );
    i2c_handle = NULL;
    i2c_initialized = false;
    return ESP_OK;
}

esp_err_t bsp_i2c_get_handle(i2c_master_bus_handle_t *out_handle)
{
    if (out_handle == NULL) {
        ESP_LOGE(TAG, "I2C handle output is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    const esp_err_t init_result = bsp_i2c_init();
    if (init_result != ESP_OK) {
        return init_result;
    }
    if (i2c_handle == NULL) {
        ESP_LOGE(TAG, "I2C initialized without a bus handle");
        return ESP_ERR_INVALID_STATE;
    }

    *out_handle = i2c_handle;
    return ESP_OK;
}

esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
        .format_if_mount_failed = false,
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    if (ret_val != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPIFFS mount failed: %s",
            esp_err_to_name(ret_val)
        );
        return ret_val;
    }

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
static void bsp_sdcard_reset_context(void)
{
    memset(&sdcard_context, 0, sizeof(sdcard_context));
    sdcard_context.pdrv = FF_DRV_NOT_USED;
    bsp_sdcard = NULL;
}

static bool bsp_sdcard_context_is_idle(void)
{
    return !sdcard_context.host_initialized &&
           !sdcard_context.slot_initialized &&
           !sdcard_context.diskio_registered &&
           !sdcard_context.vfs_registered &&
           !sdcard_context.fatfs_bound &&
           !sdcard_context.mounted &&
           sdcard_context.card == NULL &&
           bsp_sdcard == NULL;
}

static esp_err_t bsp_sdcard_identity_from_card(
    const sdmmc_card_t *card,
    bsp_sdcard_identity_t *out_identity
)
{
    if (card == NULL || out_identity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (card->csd.capacity <= 0 || card->csd.sector_size <= 0) {
        ESP_LOGE(
            TAG,
            "SD card reported invalid geometry: sectors=%" PRId32
            " sector_size=%" PRId32,
            (int32_t)card->csd.capacity,
            (int32_t)card->csd.sector_size
        );
        return ESP_ERR_INVALID_SIZE;
    }

    memset(out_identity, 0, sizeof(*out_identity));
    out_identity->manufacturer_id = (int32_t)card->cid.mfg_id;
    out_identity->oem_id = (int32_t)card->cid.oem_id;
    memcpy(
        out_identity->product_name,
        card->cid.name,
        sizeof(out_identity->product_name)
    );
    out_identity->revision = (int32_t)card->cid.revision;
    out_identity->serial = (int32_t)card->cid.serial;
    out_identity->manufacturing_date = (int32_t)card->cid.date;
    out_identity->capacity_bytes =
        (uint64_t)card->csd.capacity * (uint64_t)card->csd.sector_size;
    return ESP_OK;
}

static bool bsp_sdcard_identity_matches(
    const bsp_sdcard_identity_t *expected,
    const bsp_sdcard_identity_t *observed
)
{
    return expected != NULL &&
           observed != NULL &&
           expected->manufacturer_id == observed->manufacturer_id &&
           expected->oem_id == observed->oem_id &&
           memcmp(
               expected->product_name,
               observed->product_name,
               sizeof(expected->product_name)
           ) == 0 &&
           expected->revision == observed->revision &&
           expected->serial == observed->serial &&
           expected->manufacturing_date == observed->manufacturing_date &&
           expected->capacity_bytes == observed->capacity_bytes;
}

static esp_err_t bsp_sdcard_error_from_fatfs(FRESULT result)
{
    switch (result) {
    case FR_OK:
        return ESP_OK;
    case FR_DISK_ERR:
        return ESP_FAIL;
    case FR_INT_ERR:
        return ESP_ERR_INVALID_STATE;
    case FR_NOT_READY:
        return ESP_ERR_INVALID_RESPONSE;
    case FR_NO_FILE:
    case FR_NO_PATH:
        return ESP_ERR_NOT_FOUND;
    case FR_INVALID_NAME:
    case FR_INVALID_OBJECT:
    case FR_INVALID_DRIVE:
    case FR_INVALID_PARAMETER:
        return ESP_ERR_INVALID_ARG;
    case FR_DENIED:
    case FR_EXIST:
    case FR_WRITE_PROTECTED:
        return ESP_ERR_NOT_ALLOWED;
    case FR_NOT_ENABLED:
    case FR_LOCKED:
        return ESP_ERR_INVALID_STATE;
    case FR_NO_FILESYSTEM:
    case FR_MKFS_ABORTED:
        return ESP_ERR_NOT_FINISHED;
    case FR_TIMEOUT:
        return ESP_ERR_TIMEOUT;
    case FR_NOT_ENOUGH_CORE:
    case FR_TOO_MANY_OPEN_FILES:
        return ESP_ERR_NO_MEM;
    default:
        ESP_LOGE(TAG, "Unknown FatFs result=%d", (int)result);
        abort();
    }
}

static esp_err_t bsp_sdcard_validate_format_geometry(
    const sdmmc_card_t *card
)
{
    if (card == NULL || card->csd.sector_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint64_t sector_size = card->csd.sector_size;
    const uint64_t allocation_unit = BSP_SDCARD_ALLOCATION_UNIT_SIZE;
    const bool allocation_unit_is_power_of_two =
        (allocation_unit & (allocation_unit - 1U)) == 0;
    if (!allocation_unit_is_power_of_two ||
        allocation_unit < sector_size ||
        allocation_unit > (128U * sector_size) ||
        (allocation_unit % sector_size) != 0 ||
        sector_size > BSP_SDCARD_FORMAT_WORK_BUFFER_SIZE) {
        ESP_LOGE(
            TAG,
            "SD format geometry rejected: allocation_unit=%" PRIu64
            " sector_size=%" PRIu64,
            allocation_unit,
            sector_size
        );
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static bsp_sdcard_cleanup_report_t bsp_sdcard_cleanup_owned_resources(void)
{
    bsp_sdcard_cleanup_report_t report = {
        .error = ESP_OK,
        .stage = BSP_SDCARD_CLEANUP_NONE,
        .fatfs_result = FR_OK,
        .fatfs_result_valid = false,
        .complete = true,
    };

    // Stop exposing the card as usable before releasing any backing resource.
    bsp_sdcard = NULL;

    if (sdcard_context.fatfs_bound) {
        const FRESULT result =
            f_mount(NULL, sdcard_context.drive, 0);
        if (result != FR_OK) {
            ESP_LOGE(
                TAG,
                "SD FatFs cleanup unmount failed: FRESULT=%d",
                (int)result
            );
            report.error = bsp_sdcard_error_from_fatfs(result);
            report.stage = BSP_SDCARD_CLEANUP_FAT_UNMOUNT;
            report.fatfs_result = result;
            report.fatfs_result_valid = true;
            report.complete = false;
            sdcard_context.poisoned = true;
            return report;
        }
        sdcard_context.fatfs_bound = false;
        sdcard_context.mounted = false;
    }

    if (sdcard_context.vfs_registered) {
        const esp_err_t result =
            esp_vfs_fat_unregister_path(BSP_SD_MOUNT_POINT);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "SD VFS cleanup unregister failed: %s",
                esp_err_to_name(result)
            );
            report.error = result;
            report.stage = BSP_SDCARD_CLEANUP_VFS_UNREGISTER;
            report.complete = false;
            sdcard_context.poisoned = true;
            return report;
        }
        sdcard_context.vfs_registered = false;
        sdcard_context.fs = NULL;
    }

    if (sdcard_context.diskio_registered) {
        ff_diskio_unregister(sdcard_context.pdrv);
        sdcard_context.diskio_registered = false;
        sdcard_context.pdrv = FF_DRV_NOT_USED;
        sdcard_context.drive[0] = '\0';
    }

    esp_err_t host_cleanup_result = ESP_OK;
    if (sdcard_context.slot_initialized) {
        host_cleanup_result =
            sdmmc_host_deinit_slot(sdcard_context.host.slot);
    } else if (sdcard_context.host_initialized) {
        host_cleanup_result = sdmmc_host_deinit();
    }
    if (host_cleanup_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD host cleanup failed: %s",
            esp_err_to_name(host_cleanup_result)
        );
        report.error = host_cleanup_result;
        report.stage = BSP_SDCARD_CLEANUP_HOST_DEINIT;
        report.complete = false;
        sdcard_context.poisoned = true;
        return report;
    }
    sdcard_context.slot_initialized = false;
    sdcard_context.host_initialized = false;

    free(sdcard_context.card);
    sdcard_context.card = NULL;
    bsp_sdcard_reset_context();
    return report;
}

static void bsp_sdcard_record_cleanup(
    bsp_sdcard_mount_result_t *mount_result,
    const bsp_sdcard_cleanup_report_t *cleanup
)
{
    mount_result->cleanup_error = cleanup->error;
    mount_result->cleanup_stage = cleanup->stage;
    mount_result->cleanup_fatfs_result = cleanup->fatfs_result;
    mount_result->cleanup_fatfs_result_valid =
        cleanup->fatfs_result_valid;
    mount_result->cleanup_complete = cleanup->complete;
    mount_result->poisoned = sdcard_context.poisoned;
}

static esp_err_t bsp_sdcard_finish_mount_failure(
    bsp_sdcard_mount_result_t *result
)
{
    const bsp_sdcard_cleanup_report_t cleanup =
        bsp_sdcard_cleanup_owned_resources();
    bsp_sdcard_record_cleanup(result, &cleanup);

    if (result->authorization_consumed && !result->format_attempted) {
        ESP_LOGW(
            TAG,
            "SD format authorization consumed without formatting:"
            " outcome=%d stage=%d error=%s",
            (int)result->outcome,
            (int)result->stage,
            esp_err_to_name(result->error)
        );
    } else if (result->format_attempted && !result->formatted) {
        ESP_LOGE(
            TAG,
            "SD format did not complete; media may have been modified"
        );
    }

    if (!cleanup.complete) {
        ESP_LOGE(
            TAG,
            "SD cleanup incomplete at stage=%d; BSP storage is poisoned",
            (int)cleanup.stage
        );
    }
    return result->error;
}

// host: Initialized SDMMC slot owned by this exact staged mount attempt.
// Exception: Returns original clock-selection or clock-source preparation error.
static esp_err_t bsp_sdcard_prepare_probing_clock(const sdmmc_host_t *host)
{
    // Resolving the probing frequency prevents IDF 6 from reprogramming clocks
    // on each OCR command and undoing its continuous-clock request for ACMD41.
    esp_err_t error = host->set_card_clk(host->slot, SDMMC_FREQ_PROBING);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC probing clock selection failed: %s", esp_err_to_name(error));
        return error;
    }

    // In this SDK the official frequency query prepares the selected clock
    // source through divider calculation. READY transactions need that source
    // before querying hardware dividers; this value is calculated, not measured.
    int computed_frequency_khz = 0;
    error = host->get_real_freq(host->slot, &computed_frequency_khz);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC probing clock source preparation failed: %s", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "SDMMC probing clock selected: slot=%d requested=%d kHz computed=%d kHz",
        host->slot, SDMMC_FREQ_PROBING, computed_frequency_khz);
    return ESP_OK;
}

// host: Prepared SDMMC host owned by this exact staged mount attempt.
// card: Allocated state for the card being initialized, not a fallback card.
// Exception: Returns the original IDF protocol error after restoring log policy.
static esp_err_t bsp_sdcard_initialize_card(const sdmmc_host_t *host, sdmmc_card_t *card)
{
#if CONFIG_LUNASHELL_STORAGE_DEBUG_PROBE
    // Trace the stock protocol only during initialization. These are owner
    // operation logs; no extra transactions, presence inference or repair occur.
    const esp_log_level_t command_level = esp_log_level_get("sdmmc_cmd");
    const esp_log_level_t common_level = esp_log_level_get("sdmmc_common");
    const esp_log_level_t host_level = esp_log_level_get("SD_HOST");
    esp_log_level_set("sdmmc_cmd", ESP_LOG_VERBOSE);
    esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
    esp_log_level_set("SD_HOST", ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "SD protocol trace begin: slot=%d runtime_max=%d kHz sdio_probe=%s",
        host->slot, host->max_freq_khz,
#if CONFIG_SD_ENABLE_SDIO_SUPPORT
        "enabled"
#else
        "disabled"
#endif
    );
#endif
    const esp_err_t error = sdmmc_card_init(host, card);
#if CONFIG_LUNASHELL_STORAGE_DEBUG_PROBE
    esp_log_level_set("sdmmc_cmd", command_level);
    esp_log_level_set("sdmmc_common", common_level);
    esp_log_level_set("SD_HOST", host_level);
    ESP_LOGI(TAG, "SD protocol trace end: result=%s", esp_err_to_name(error));
#endif
    return error;
}

// request: Exact staged mount and any explicit single-use format authorization.
// out_result: Receives the real failed stage, error and cleanup ownership.
// Exception: Returns original protocol, filesystem, policy or cleanup failures.
esp_err_t bsp_sdcard_mount(
    const bsp_sdcard_mount_request_t *request,
    bsp_sdcard_mount_result_t *out_result
)
{
    if (out_result == NULL) {
        ESP_LOGE(TAG, "SD mount result output is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
    out_result->stage = BSP_SDCARD_STAGE_REQUEST;
    out_result->error = ESP_ERR_INVALID_ARG;
    out_result->cleanup_error = ESP_OK;
    out_result->cleanup_complete = true;

    if (request == NULL) {
        ESP_LOGE(TAG, "SD mount request is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // The caller consumes this permission before entering the BSP. It is never
    // retained for a later probe, even when this request is rejected.
    out_result->authorization_consumed = request->format_authorized;
    if (request->format_authorized != request->expected_identity_valid) {
        ESP_LOGE(
            TAG,
            "SD format request rejected: authorized=%s identity_valid=%s%s",
            request->format_authorized ? "yes" : "no",
            request->expected_identity_valid ? "yes" : "no",
            request->format_authorized ? "; authorization consumed" : ""
        );
        return ESP_ERR_INVALID_ARG;
    }
    if (request->format_authorized &&
        request->expected_identity.capacity_bytes == 0) {
        ESP_LOGE(
            TAG,
            "SD format request has an invalid zero-capacity identity;"
            " authorization consumed"
        );
        return ESP_ERR_INVALID_ARG;
    }
    if (sdcard_context.poisoned || !bsp_sdcard_context_is_idle()) {
        out_result->error = ESP_ERR_INVALID_STATE;
        out_result->poisoned = sdcard_context.poisoned;
        ESP_LOGE(
            TAG,
            "SD mount rejected: BSP storage state is %s%s",
            sdcard_context.poisoned ? "poisoned" : "already owned",
            request->format_authorized ? "; authorization consumed" : ""
        );
        return out_result->error;
    }

    sdcard_context.card =
        (sdmmc_card_t *)calloc(1, sizeof(*sdcard_context.card));
    if (sdcard_context.card == NULL) {
        out_result->error = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "SD card state allocation failed");
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Use Waveshare's published host clock and the board's external pull-ups.
    // A reduced runtime clock cannot repair a card stuck during 400 kHz OCR.
    sdcard_context.host = host;

    const sdmmc_slot_config_t slot_config = {
        .clk = BSP_SD_CLK,
        .cmd = BSP_SD_CMD,
        .d0 = BSP_SD_D0,
        .d1 = GPIO_NUM_NC,
        .d2 = GPIO_NUM_NC,
        .d3 = GPIO_NUM_NC,
        .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC,
        .d6 = GPIO_NUM_NC,
        .d7 = GPIO_NUM_NC,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 1,
        .flags = 0,
    };

    out_result->stage = BSP_SDCARD_STAGE_HOST_INIT;
    esp_err_t result = sdcard_context.host.init();
    if (result != ESP_OK) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED;
        out_result->error = result;
        ESP_LOGE(TAG, "SDMMC host initialization failed: %s", esp_err_to_name(result));
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    sdcard_context.host_initialized = true;

    out_result->stage = BSP_SDCARD_STAGE_SLOT_INIT;
    result = sdmmc_host_init_slot(
        sdcard_context.host.slot,
        &slot_config
    );
    if (result != ESP_OK) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED;
        out_result->error = result;
        ESP_LOGE(TAG, "SDMMC slot initialization failed: %s", esp_err_to_name(result));
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    sdcard_context.slot_initialized = true;

    // Card initialization still negotiates the supported runtime speed later.
    result = bsp_sdcard_prepare_probing_clock(&sdcard_context.host);
    if (result != ESP_OK) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED;
        out_result->error = result;
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    out_result->stage = BSP_SDCARD_STAGE_CARD_INIT;
    result = bsp_sdcard_initialize_card(
        &sdcard_context.host,
        sdcard_context.card
    );
    if (result != ESP_OK) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED;
        out_result->error = result;
        ESP_LOGE(TAG, "SD card initialization failed: %s", esp_err_to_name(result));
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    out_result->card_initialized = true;

    out_result->stage = BSP_SDCARD_STAGE_CARD_IDENTITY;
    result = bsp_sdcard_identity_from_card(
        sdcard_context.card,
        &out_result->observed_identity
    );
    if (result != ESP_OK) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
        out_result->error = result;
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    out_result->observed_identity_valid = true;

    if (request->format_authorized) {
        out_result->authorization_identity_matched =
            bsp_sdcard_identity_matches(
                &request->expected_identity,
                &out_result->observed_identity
            );
        if (!out_result->authorization_identity_matched) {
            ESP_LOGW(
                TAG,
                "SD format authorization identity mismatch:"
                " expected_serial=%" PRId32
                " observed_serial=%" PRId32
                " expected_capacity=%" PRIu64
                " observed_capacity=%" PRIu64,
                request->expected_identity.serial,
                out_result->observed_identity.serial,
                request->expected_identity.capacity_bytes,
                out_result->observed_identity.capacity_bytes
            );
        }
    }

    out_result->stage = BSP_SDCARD_STAGE_DRIVE_ALLOC;
    result = ff_diskio_get_drive(&sdcard_context.pdrv);
    if (result != ESP_OK ||
        sdcard_context.pdrv == FF_DRV_NOT_USED ||
        sdcard_context.pdrv > 9) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
        out_result->error =
            result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
        ESP_LOGE(
            TAG,
            "Allocating an SD FatFs drive failed: %s",
            esp_err_to_name(out_result->error)
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    sdcard_context.drive[0] = (char)('0' + sdcard_context.pdrv);
    sdcard_context.drive[1] = ':';
    sdcard_context.drive[2] = '\0';

    out_result->stage = BSP_SDCARD_STAGE_DISKIO_REGISTER;
    ff_diskio_register_sdmmc(
        sdcard_context.pdrv,
        sdcard_context.card
    );
    sdcard_context.diskio_registered = true;
    ff_sdmmc_set_disk_status_check(sdcard_context.pdrv, true);

    const esp_vfs_fat_conf_t vfs_config = {
        .base_path = BSP_SD_MOUNT_POINT,
        .fat_drive = sdcard_context.drive,
        .max_files = BSP_SDCARD_MAX_FILES,
    };
    out_result->stage = BSP_SDCARD_STAGE_VFS_REGISTER;
    result = esp_vfs_fat_register(
        &vfs_config,
        &sdcard_context.fs
    );
    if (result != ESP_OK) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
        out_result->error = result;
        ESP_LOGE(TAG, "Registering SD VFS failed: %s", esp_err_to_name(result));
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    sdcard_context.vfs_registered = true;

    out_result->stage = BSP_SDCARD_STAGE_FAT_MOUNT;
    sdcard_context.fatfs_bound = true;
    FRESULT fatfs_result =
        f_mount(sdcard_context.fs, sdcard_context.drive, 1);
    out_result->initial_mount_result = fatfs_result;
    out_result->initial_mount_result_valid = true;
    out_result->fatfs_result = fatfs_result;
    out_result->fatfs_result_valid = true;

    if (fatfs_result == FR_OK) {
        sdcard_context.mounted = true;
        bsp_sdcard = sdcard_context.card;
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_MOUNTED;
        out_result->error = ESP_OK;
        out_result->mounted = true;
        if (out_result->authorization_consumed) {
            ESP_LOGW(
                TAG,
                "SD format authorization consumed; filesystem was already valid"
            );
        }
        return ESP_OK;
    }

    if (fatfs_result != FR_NO_FILESYSTEM) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        ESP_LOGE(
            TAG,
            "SD FAT mount failed without format eligibility: FRESULT=%d",
            (int)fatfs_result
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    if (!request->format_authorized ||
        !out_result->authorization_identity_matched) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_FORMAT_REQUIRED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        ESP_LOGW(
            TAG,
            "SD has no valid FAT filesystem; explicit matching authorization required"
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    result = bsp_sdcard_validate_format_geometry(sdcard_context.card);
    if (result != ESP_OK) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
        out_result->stage = BSP_SDCARD_STAGE_FORMAT;
        out_result->error = result;
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    void *format_work_buffer = heap_caps_aligned_alloc(
        sizeof(uint32_t),
        BSP_SDCARD_FORMAT_WORK_BUFFER_SIZE,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );
    if (format_work_buffer == NULL) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED;
        out_result->stage = BSP_SDCARD_STAGE_FORMAT;
        out_result->error = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Allocating SD format work buffer failed");
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    out_result->stage = BSP_SDCARD_STAGE_PRE_FORMAT_UNMOUNT;
    fatfs_result = f_mount(NULL, sdcard_context.drive, 0);
    out_result->fatfs_result = fatfs_result;
    if (fatfs_result != FR_OK) {
        heap_caps_free(format_work_buffer);
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        out_result->cleanup_error =
            bsp_sdcard_error_from_fatfs(fatfs_result);
        out_result->cleanup_stage =
            BSP_SDCARD_CLEANUP_FAT_UNMOUNT;
        out_result->cleanup_fatfs_result = fatfs_result;
        out_result->cleanup_fatfs_result_valid = true;
        out_result->cleanup_complete = false;
        sdcard_context.poisoned = true;
        out_result->poisoned = true;
        bsp_sdcard = NULL;
        ESP_LOGE(
            TAG,
            "SD pre-format unmount failed: FRESULT=%d;"
            " authorization consumed; storage state is poisoned",
            (int)fatfs_result
        );
        return out_result->error;
    }
    sdcard_context.fatfs_bound = false;
    sdcard_context.mounted = false;

    const LBA_t partitions[4] = {100, 0, 0, 0};
    out_result->stage = BSP_SDCARD_STAGE_PARTITION;
    out_result->format_attempted = true;
    ESP_LOGW(
        TAG,
        "Formatting explicitly authorized SD card:"
        " serial=%" PRId32 " capacity=%" PRIu64,
        out_result->observed_identity.serial,
        out_result->observed_identity.capacity_bytes
    );
    fatfs_result = f_fdisk(
        sdcard_context.pdrv,
        partitions,
        format_work_buffer
    );
    out_result->fatfs_result = fatfs_result;
    if (fatfs_result != FR_OK) {
        heap_caps_free(format_work_buffer);
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        ESP_LOGE(
            TAG,
            "SD partitioning failed: FRESULT=%d",
            (int)fatfs_result
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    out_result->partition_written = true;

    const MKFS_PARM format_options = {
        .fmt = FM_ANY,
        .n_fat = 2,
        .align = 0,
        .n_root = 0,
        .au_size = BSP_SDCARD_ALLOCATION_UNIT_SIZE,
    };
    out_result->stage = BSP_SDCARD_STAGE_FORMAT;
    fatfs_result = f_mkfs(
        sdcard_context.drive,
        &format_options,
        format_work_buffer,
        BSP_SDCARD_FORMAT_WORK_BUFFER_SIZE
    );
    heap_caps_free(format_work_buffer);
    out_result->fatfs_result = fatfs_result;
    if (fatfs_result != FR_OK) {
        out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        ESP_LOGE(
            TAG,
            "SD filesystem creation failed: FRESULT=%d",
            (int)fatfs_result
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }
    out_result->formatted = true;

    out_result->stage = BSP_SDCARD_STAGE_POST_FORMAT_MOUNT;
    sdcard_context.fatfs_bound = true;
    fatfs_result =
        f_mount(sdcard_context.fs, sdcard_context.drive, 1);
    out_result->fatfs_result = fatfs_result;
    if (fatfs_result != FR_OK) {
        out_result->outcome =
            BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED;
        out_result->error = bsp_sdcard_error_from_fatfs(fatfs_result);
        ESP_LOGE(
            TAG,
            "SD remount after completed format failed: FRESULT=%d",
            (int)fatfs_result
        );
        return bsp_sdcard_finish_mount_failure(out_result);
    }

    sdcard_context.mounted = true;
    bsp_sdcard = sdcard_context.card;
    out_result->outcome = BSP_SDCARD_MOUNT_OUTCOME_MOUNTED;
    out_result->error = ESP_OK;
    out_result->mounted = true;
    ESP_LOGI(TAG, "SD format completed and filesystem mounted");
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(bsp_sdcard_unmount_result_t *out_result)
{
    if (out_result == NULL) {
        ESP_LOGE(TAG, "SD unmount result output is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_result = (bsp_sdcard_unmount_result_t){
        .error = ESP_ERR_INVALID_STATE,
        .stage = BSP_SDCARD_CLEANUP_NONE,
        .fatfs_result = FR_OK,
        .fatfs_result_valid = false,
        .complete = false,
        .poisoned = sdcard_context.poisoned,
    };

    if (sdcard_context.poisoned) {
        ESP_LOGE(TAG, "SD unmount rejected: BSP storage state is poisoned");
        return out_result->error;
    }
    if (!sdcard_context.mounted ||
        !sdcard_context.fatfs_bound ||
        sdcard_context.card == NULL ||
        bsp_sdcard != sdcard_context.card) {
        ESP_LOGE(TAG, "SD unmount rejected: no coherent mounted card");
        return out_result->error;
    }

    const bsp_sdcard_cleanup_report_t cleanup =
        bsp_sdcard_cleanup_owned_resources();
    out_result->error = cleanup.error;
    out_result->stage = cleanup.stage;
    out_result->fatfs_result = cleanup.fatfs_result;
    out_result->fatfs_result_valid = cleanup.fatfs_result_valid;
    out_result->complete = cleanup.complete;
    out_result->poisoned = sdcard_context.poisoned;
    if (!cleanup.complete) {
        ESP_LOGE(
            TAG,
            "SD unmount cleanup failed at stage=%d: %s;"
            " BSP storage is poisoned",
            (int)cleanup.stage,
            esp_err_to_name(cleanup.error)
        );
        return cleanup.error;
    }
    out_result->error = ESP_OK;
    return ESP_OK;
}

esp_err_t bsp_sdcard_get_status(void)
{
    if (sdcard_context.poisoned) {
        ESP_LOGE(TAG, "SD status rejected: BSP storage state is poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    if (!sdcard_context.mounted ||
        !sdcard_context.fatfs_bound ||
        sdcard_context.card == NULL ||
        bsp_sdcard != sdcard_context.card) {
        ESP_LOGE(TAG, "SD status rejected: no coherent mounted card");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = sdmmc_get_status(sdcard_context.card);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "SD status command failed: %s", esp_err_to_name(result));
    }
    return result;
}
#endif

/**************************************************************************************************
 *
 * I2S Audio Function
 *
 **************************************************************************************************/
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO
static void record_audio_cleanup_error(
    const char *operation,
    int result,
    esp_err_t *aggregate
)
{
    if (result == ESP_CODEC_DEV_OK) {
        return;
    }
    ESP_LOGE(TAG, "Audio cleanup %s failed: codec_result=%d", operation, result);
    if (*aggregate == ESP_OK) {
        *aggregate = ESP_FAIL;
    }
}

// Stop one codec-managed I2S channel and confirm an already-stopped channel.
static esp_err_t bsp_audio_disable_channel(
    i2s_chan_handle_t channel,
    bool *enabled,
    const char *name
)
{
    if (channel == NULL || enabled == NULL || name == NULL) {
        ESP_LOGE(TAG, "Audio channel disable rejected: invalid channel state");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = i2s_channel_disable(channel);
    if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
        // esp_codec_dev_close() normally stops the channel before this BSP
        // ownership layer confirms and records the transport state.
        if (result == ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "Audio %s channel already stopped", name);
        }
        *enabled = false;
        return ESP_OK;
    }
    ESP_LOGE(
        TAG,
        "Audio %s disable failed: %s",
        name,
        esp_err_to_name(result)
    );
    return result;
}

// Start one allocated I2S channel while retaining the exact driver failure.
static esp_err_t bsp_audio_enable_channel(
    i2s_chan_handle_t channel,
    bool *enabled,
    const char *name
)
{
    if (channel == NULL || enabled == NULL || name == NULL) {
        ESP_LOGE(TAG, "Audio channel enable rejected: invalid channel state");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = i2s_channel_enable(channel);
    if (result == ESP_OK) {
        *enabled = true;
        return ESP_OK;
    }
    ESP_LOGE(
        TAG,
        "Audio %s enable failed: %s",
        name,
        esp_err_to_name(result)
    );
    return result;
}

// Release every BSP-owned I2S resource and poison partial cleanup state.
static esp_err_t bsp_audio_transport_cleanup(void)
{
    esp_err_t aggregate = ESP_OK;

    if (i2s_data_if != NULL) {
        const int result = audio_codec_delete_data_if(i2s_data_if);
        if (result == ESP_CODEC_DEV_OK) {
            i2s_data_if = NULL;
        }
        record_audio_cleanup_error("data interface", result, &aggregate);
    }

    if (i2s_rx_chan != NULL && i2s_rx_enabled) {
        const esp_err_t result =
            bsp_audio_disable_channel(i2s_rx_chan, &i2s_rx_enabled, "RX");
        if (aggregate == ESP_OK && result != ESP_OK) {
            aggregate = result;
        }
    }
    if (i2s_tx_chan != NULL && i2s_tx_enabled) {
        const esp_err_t result =
            bsp_audio_disable_channel(i2s_tx_chan, &i2s_tx_enabled, "TX");
        if (aggregate == ESP_OK && result != ESP_OK) {
            aggregate = result;
        }
    }

    if (i2s_rx_chan != NULL && !i2s_rx_enabled) {
        const esp_err_t result = i2s_del_channel(i2s_rx_chan);
        if (result == ESP_OK) {
            i2s_rx_chan = NULL;
        } else {
            ESP_LOGE(
                TAG,
                "Audio cleanup RX delete failed: %s",
                esp_err_to_name(result)
            );
            if (aggregate == ESP_OK) {
                aggregate = result;
            }
        }
    }
    if (i2s_tx_chan != NULL && !i2s_tx_enabled) {
        const esp_err_t result = i2s_del_channel(i2s_tx_chan);
        if (result == ESP_OK) {
            i2s_tx_chan = NULL;
        } else {
            ESP_LOGE(
                TAG,
                "Audio cleanup TX delete failed: %s",
                esp_err_to_name(result)
            );
            if (aggregate == ESP_OK) {
                aggregate = result;
            }
        }
    }

    if (aggregate == ESP_OK) {
        audio_transport_standby = false;
    } else {
        audio_resources_poisoned = true;
    }
    return aggregate;
}

static esp_err_t bsp_audio_codec_cleanup(void)
{
    esp_err_t aggregate = ESP_OK;

    if (microphone_codec_device != NULL) {
        esp_codec_dev_delete(microphone_codec_device);
        microphone_codec_device = NULL;
    }
    if (speaker_codec_device != NULL) {
        esp_codec_dev_delete(speaker_codec_device);
        speaker_codec_device = NULL;
    }
    if (microphone_codec_interface != NULL) {
        const int result =
            audio_codec_delete_codec_if(microphone_codec_interface);
        if (result == ESP_CODEC_DEV_OK) {
            microphone_codec_interface = NULL;
        }
        record_audio_cleanup_error(
            "microphone codec interface",
            result,
            &aggregate
        );
    }
    if (speaker_codec_interface != NULL) {
        const int result =
            audio_codec_delete_codec_if(speaker_codec_interface);
        if (result == ESP_CODEC_DEV_OK) {
            speaker_codec_interface = NULL;
        }
        record_audio_cleanup_error(
            "speaker codec interface",
            result,
            &aggregate
        );
    }
    if (microphone_control_interface != NULL) {
        const int result =
            audio_codec_delete_ctrl_if(microphone_control_interface);
        if (result == ESP_CODEC_DEV_OK) {
            microphone_control_interface = NULL;
        }
        record_audio_cleanup_error(
            "microphone control interface",
            result,
            &aggregate
        );
    }
    if (speaker_control_interface != NULL) {
        const int result =
            audio_codec_delete_ctrl_if(speaker_control_interface);
        if (result == ESP_CODEC_DEV_OK) {
            speaker_control_interface = NULL;
        }
        record_audio_cleanup_error(
            "speaker control interface",
            result,
            &aggregate
        );
    }
    if (speaker_gpio_interface != NULL) {
        const int result =
            audio_codec_delete_gpio_if(speaker_gpio_interface);
        if (result == ESP_CODEC_DEV_OK) {
            speaker_gpio_interface = NULL;
        }
        record_audio_cleanup_error(
            "speaker GPIO interface",
            result,
            &aggregate
        );
    }

    const esp_err_t transport_result = bsp_audio_transport_cleanup();
    if (aggregate == ESP_OK && transport_result != ESP_OK) {
        aggregate = transport_result;
    }
    if (aggregate != ESP_OK) {
        audio_resources_poisoned = true;
    }
    return aggregate;
}

// Allocate and initialize the shared duplex I2S transport once.
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (audio_resources_poisoned) {
        ESP_LOGE(TAG, "Audio initialization rejected: resource state is poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    if ((!BSP_AUDIO_HAS_OUTPUT ||
         (i2s_tx_chan != NULL && i2s_tx_enabled)) &&
        (!BSP_AUDIO_HAS_INPUT ||
         (i2s_rx_chan != NULL && i2s_rx_enabled)) &&
        i2s_data_if != NULL) {
        return ESP_OK;
    }
    if (i2s_tx_chan != NULL || i2s_rx_chan != NULL ||
        i2s_data_if != NULL || i2s_tx_enabled || i2s_rx_enabled) {
        ESP_LOGE(
            TAG,
            "Audio initialization rejected: I2S resource state is partial"
        );
        return ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Clear stale samples in the DMA buffer.
    esp_err_t primary_error = i2s_new_channel(
        &chan_cfg,
        BSP_AUDIO_HAS_OUTPUT ? &i2s_tx_chan : NULL,
        BSP_AUDIO_HAS_INPUT ? &i2s_rx_chan : NULL
    );
    if (primary_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S channel allocation failed: %s",
            esp_err_to_name(primary_error)
        );
        if (i2s_tx_chan != NULL || i2s_rx_chan != NULL) {
            ESP_LOGE(
                TAG,
                "I2S allocation returned partial channel ownership; "
                "starting explicit cleanup"
            );
            const esp_err_t cleanup_error = bsp_audio_transport_cleanup();
            if (cleanup_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "I2S cleanup after partial allocation failed: %s",
                    esp_err_to_name(cleanup_error)
                );
            }
        }
        return primary_error;
    }

    // ES7210 provides two PCM channels; mono slots alias adjacent time samples
    // as channels and create a false Nyquist peak in the spectrum.
    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_STEREO_CFG(22050);
    const i2s_std_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        p_i2s_cfg = i2s_config;
    }

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    primary_error = i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg);
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_enable(i2s_tx_chan);
        i2s_tx_enabled = primary_error == ESP_OK;
    }
#endif
#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg);
    }
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_enable(i2s_rx_chan);
        i2s_rx_enabled = primary_error == ESP_OK;
    }
#endif
    if (primary_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S initialization failed: %s",
            esp_err_to_name(primary_error)
        );
        const esp_err_t cleanup_error = bsp_audio_transport_cleanup();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "I2S cleanup after initialization failure failed: %s",
                esp_err_to_name(cleanup_error)
            );
        }
        return primary_error;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .rx_handle = i2s_rx_chan,
        .tx_handle = i2s_tx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (i2s_data_if == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Creating I2S codec data interface failed");
        const esp_err_t cleanup_error = bsp_audio_transport_cleanup();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "I2S cleanup after data-interface failure failed: %s",
                esp_err_to_name(cleanup_error)
            );
        }
        return primary_error;
    }
    audio_transport_standby = false;
    return ESP_OK;
}

// Transition both allocated I2S channels atomically for system standby.
esp_err_t bsp_audio_set_transport_standby(
    bool standby,
    bool resume_tx,
    bool resume_rx
)
{
    if (audio_resources_poisoned) {
        ESP_LOGE(TAG, "Audio transport transition rejected: resources are poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    if (standby == audio_transport_standby) {
        return ESP_OK;
    }
    if (i2s_data_if == NULL ||
        (BSP_AUDIO_HAS_OUTPUT && i2s_tx_chan == NULL) ||
        (BSP_AUDIO_HAS_INPUT && i2s_rx_chan == NULL)) {
        ESP_LOGE(TAG, "Audio transport transition rejected: route is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (standby) {
        esp_err_t rx_error = ESP_OK;
        if (i2s_rx_chan != NULL) {
            rx_error = bsp_audio_disable_channel(i2s_rx_chan, &i2s_rx_enabled, "RX");
            if (rx_error != ESP_OK) {
                return rx_error;
            }
        }
        const esp_err_t tx_error = i2s_tx_chan != NULL ?
            bsp_audio_disable_channel(i2s_tx_chan, &i2s_tx_enabled, "TX") : ESP_OK;
        if (tx_error != ESP_OK) {
            const esp_err_t rollback_error = i2s_rx_chan != NULL ?
                bsp_audio_enable_channel(i2s_rx_chan, &i2s_rx_enabled, "RX rollback") : ESP_OK;
            if (rollback_error != ESP_OK) {
                audio_resources_poisoned = true;
                ESP_LOGE(
                    TAG,
                    "Audio standby rollback failed after TX error: %s",
                    esp_err_to_name(rollback_error)
                );
            }
            return tx_error;
        }
        audio_transport_standby = true;
        ESP_LOGI(TAG, "Audio I2S transport suspended");
        return ESP_OK;
    }

    bool tx_resumed = false;
    if (resume_tx && i2s_tx_chan != NULL) {
        const esp_err_t tx_error =
            bsp_audio_enable_channel(i2s_tx_chan, &i2s_tx_enabled, "TX");
        if (tx_error != ESP_OK) {
            return tx_error;
        }
        tx_resumed = true;
    }
    if (resume_rx && i2s_rx_chan != NULL) {
        const esp_err_t rx_error =
            bsp_audio_enable_channel(i2s_rx_chan, &i2s_rx_enabled, "RX");
        if (rx_error != ESP_OK) {
            if (tx_resumed) {
                const esp_err_t rollback_error = bsp_audio_disable_channel(
                    i2s_tx_chan,
                    &i2s_tx_enabled,
                    "TX rollback"
                );
                if (rollback_error != ESP_OK) {
                    audio_resources_poisoned = true;
                    ESP_LOGE(
                        TAG,
                        "Audio resume rollback failed after RX error: %s",
                        esp_err_to_name(rollback_error)
                    );
                }
            }
            return rx_error;
        }
    }
    audio_transport_standby = false;
    ESP_LOGI(
        TAG,
        "Audio I2S transport resumed: TX=%s RX=%s",
        resume_tx ? "on" : "off",
        resume_rx ? "on" : "off"
    );
    return ESP_OK;
}

esp_err_t bsp_audio_codec_init(
    esp_codec_dev_handle_t *out_speaker,
    esp_codec_dev_handle_t *out_microphone
)
{
    if (out_speaker == NULL || out_microphone == NULL) {
        ESP_LOGE(TAG, "Audio codec output handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_speaker = NULL;
    *out_microphone = NULL;
    if (audio_resources_poisoned) {
        ESP_LOGE(TAG, "Audio codec initialization rejected: resources are poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    const bool route_complete =
        (!BSP_AUDIO_HAS_OUTPUT ||
         (speaker_codec_device != NULL && speaker_gpio_interface != NULL &&
          speaker_control_interface != NULL && speaker_codec_interface != NULL &&
          i2s_tx_chan != NULL && i2s_tx_enabled)) &&
        (!BSP_AUDIO_HAS_INPUT ||
         (microphone_codec_device != NULL && microphone_control_interface != NULL &&
          microphone_codec_interface != NULL && i2s_rx_chan != NULL &&
          i2s_rx_enabled)) &&
        i2s_data_if != NULL;
    if (route_complete) {
        *out_speaker = speaker_codec_device;
        *out_microphone = microphone_codec_device;
        return ESP_OK;
    }
    if (speaker_codec_device != NULL ||
        microphone_codec_device != NULL ||
        speaker_gpio_interface != NULL ||
        speaker_control_interface != NULL ||
        speaker_codec_interface != NULL ||
        microphone_control_interface != NULL ||
        microphone_codec_interface != NULL) {
        ESP_LOGE(TAG, "Audio codec initialization rejected: route is partial");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t primary_error = bsp_i2c_init();
    if (primary_error == ESP_OK) {
        primary_error = bsp_audio_init(NULL);
    }
    if (primary_error != ESP_OK) {
        return primary_error;
    }

#if CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    speaker_gpio_interface = audio_codec_new_gpio();
    if (speaker_gpio_interface == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_handle,
    };
    speaker_control_interface = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (speaker_control_interface == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = speaker_control_interface,
        .gpio_if = speaker_gpio_interface,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    speaker_codec_interface = es8311_codec_new(&es8311_cfg);
    if (speaker_codec_interface == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = speaker_codec_interface,
        .data_if = i2s_data_if,
    };
    speaker_codec_device = esp_codec_dev_new(&codec_dev_cfg);
    if (speaker_codec_device == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }
#endif

#if !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    audio_codec_i2c_cfg_t microphone_i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = BSP_ES7210_CODEC_ADDR,
        .bus_handle = i2c_handle,
    };
    microphone_control_interface =
        audio_codec_new_i2c_ctrl(&microphone_i2c_cfg);
    if (microphone_control_interface == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = microphone_control_interface,
    };
    microphone_codec_interface = es7210_codec_new(&es7210_cfg);
    if (microphone_codec_interface == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }

    esp_codec_dev_cfg_t codec_es7210_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = microphone_codec_interface,
        .data_if = i2s_data_if,
    };
    microphone_codec_device =
        esp_codec_dev_new(&codec_es7210_dev_cfg);
    if (microphone_codec_device == NULL) {
        primary_error = ESP_ERR_NO_MEM;
        goto fail;
    }
#endif

    *out_speaker = speaker_codec_device;
    *out_microphone = microphone_codec_device;
    return ESP_OK;

fail:
    ESP_LOGE(
        TAG,
        "Audio codec route allocation failed: %s",
        esp_err_to_name(primary_error)
    );
    {
        const esp_err_t cleanup_error = bsp_audio_codec_cleanup();
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Audio codec cleanup after allocation failure failed: %s",
                esp_err_to_name(cleanup_error)
            );
        }
    }
    return primary_error;
}

esp_err_t bsp_audio_codec_deinit(void)
{
    if (audio_resources_poisoned) {
        ESP_LOGE(TAG, "Audio codec cleanup rejected: resources are poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    const bool has_resources =
        speaker_codec_device != NULL ||
        microphone_codec_device != NULL ||
        speaker_gpio_interface != NULL ||
        speaker_control_interface != NULL ||
        speaker_codec_interface != NULL ||
        microphone_control_interface != NULL ||
        microphone_codec_interface != NULL ||
        i2s_data_if != NULL ||
        i2s_tx_chan != NULL ||
        i2s_rx_chan != NULL;
    if (!has_resources) {
        ESP_LOGE(TAG, "Audio codec cleanup rejected: route is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return bsp_audio_codec_cleanup();
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_LEDC_CH (CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH)
#define LVGL_TICK_PERIOD_MS (CONFIG_BSP_DISPLAY_LVGL_TICK)
#define LVGL_MAX_SLEEP_MS (CONFIG_BSP_DISPLAY_LVGL_MAX_SLEEP)

// No parameters: keep Deep-sleep wake dark until the first real frame is rendered.
esp_err_t bsp_display_brightness_init(void)
{
    return bsp_display_brightness_set(esp_reset_reason() == ESP_RST_DEEPSLEEP ? 0 : 100);
}

// brightness_percent: Controller brightness requested as a whole percentage.
esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness_percent < 0 || brightness_percent > 100)
    {
        ESP_LOGE(TAG, "Brightness percentage %d rejected: expected 0 through 100 (%s)", brightness_percent, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result =
        bsp_display_lock(BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS);
    if (lock_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display brightness lock failed: %s",
            esp_err_to_name(lock_result)
        );
        return lock_result;
    }

    const uint8_t requested_brightness =
        (uint8_t)(brightness_percent * 255 / 100);

    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    const uint8_t param = requested_brightness;
    const esp_err_t result =
        esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, &param, 1);
    if (result == ESP_OK) {
        brightness = requested_brightness;
    }
    bsp_display_unlock();
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display brightness command failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return -1;
    }

    // Round the controller's 8-bit value back to the nearest percentage.
    return (brightness * 100 + 127) / 255;
}

// enabled: Requested AMOLED output state, independent of brightness.
esp_err_t bsp_display_panel_set_enabled(bool enabled)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(
            TAG,
            "Display panel %s rejected: panel handle is not initialized",
            enabled ? "enable" : "disable"
        );
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        bsp_display_lock(BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS);
    if (lock_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display panel %s lock failed: %s",
            enabled ? "enable" : "disable",
            esp_err_to_name(lock_result)
        );
        return lock_result;
    }
    if (display_panel_output_enabled == enabled) {
        bsp_display_unlock();
        return ESP_OK;
    }

    const esp_err_t result =
        esp_lcd_panel_disp_on_off(panel_handle, enabled);
    if (result == ESP_OK) {
        display_panel_output_enabled = enabled;
        if (enabled) {
            __atomic_add_fetch(&display_content_revision, 1U, __ATOMIC_RELEASE);
        }
    }
    bsp_display_unlock();
    if (result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Display panel %s failed: %s",
            enabled ? "enable" : "disable",
            esp_err_to_name(result)
        );
        return result;
    }

    ESP_LOGI(TAG, "Display panel output %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG, "Backlight off");
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    ESP_LOGI(TAG, "Backlight on");
    return bsp_display_brightness_set(100);
}
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// e: LVGL dirty-area event rounded to the panel's paired-pixel boundaries.
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
static esp_err_t bsp_display_draw_bitmap_bounced(
    esp_lcd_panel_handle_t panel,
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *color_map,
    size_t source_stride_bytes,
    uint8_t *bounce_buffer,
    size_t bounce_slot_stride_bytes,
    size_t bounce_buffer_count,
    size_t transfer_height_rows
)
{
    if (panel == NULL || color_map == NULL || bounce_buffer == NULL ||
        bounce_slot_stride_bytes == 0U ||
        bounce_buffer_count == 0U ||
        bounce_buffer_count > CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH ||
        transfer_height_rows == 0U ||
        transfer_height_rows > BSP_DISPLAY_DMA_TRANSFER_HEIGHT ||
        x_start < 0 || y_start < 0 ||
        x_end > BSP_LCD_H_RES || y_end > BSP_LCD_V_RES ||
        x_start >= x_end || y_start >= y_end ||
        source_stride_bytes <
            (size_t)(x_end - x_start) *
                (BSP_LCD_BITS_PER_PIXEL / 8U)) {
        ESP_LOGE(
            TAG,
            "Bounced draw rejected: panel=%p pixels=%p buffer=%p "
            "area=[%d,%d,%d,%d]",
            panel,
            color_map,
            bounce_buffer,
            x_start,
            y_start,
            x_end,
            y_end
        );
        return ESP_ERR_INVALID_ARG;
    }

    const size_t bytes_per_pixel = BSP_LCD_BITS_PER_PIXEL / 8U;
    const size_t source_row_bytes =
        (size_t)(x_end - x_start) * bytes_per_pixel;
    const size_t maximum_chunk_rows =
        (size_t)(y_end - y_start) < transfer_height_rows
            ? (size_t)(y_end - y_start)
            : transfer_height_rows;
    const size_t bounce_slot_bytes =
        source_row_bytes * maximum_chunk_rows;
    if (bounce_slot_stride_bytes < bounce_slot_bytes) {
        ESP_LOGE(
            TAG,
            "Bounced draw rejected: slot=%lu required=%lu",
            (unsigned long)bounce_slot_stride_bytes,
            (unsigned long)bounce_slot_bytes
        );
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t *source = (const uint8_t *)color_map;
    size_t queued_transfers = 0;
    for (int chunk_y = y_start; chunk_y < y_end;
         chunk_y += (int)transfer_height_rows) {
        const int chunk_end =
            chunk_y + (int)transfer_height_rows < y_end
                ? chunk_y + (int)transfer_height_rows
                : y_end;
        const size_t chunk_rows = (size_t)(chunk_end - chunk_y);
        uint8_t *chunk_buffer =
            bounce_buffer + queued_transfers * bounce_slot_stride_bytes;
        for (size_t row = 0; row < chunk_rows; ++row) {
            memcpy(
                chunk_buffer + row * source_row_bytes,
                source +
                    ((size_t)(chunk_y - y_start) + row) *
                        source_stride_bytes,
                source_row_bytes
            );
        }
        const esp_err_t draw_result = esp_lcd_panel_draw_bitmap(
            panel,
            x_start,
            chunk_y,
            x_end,
            chunk_end,
            chunk_buffer
        );
        if (draw_result != ESP_OK) {
            ++display_dma_transfer_failure_count;
            ESP_LOGE(
                TAG,
                "Bounced draw failed at [%d,%d,%d,%d] count=%lu (%s)",
                x_start,
                chunk_y,
                x_end,
                chunk_end,
                (unsigned long)display_dma_transfer_failure_count,
                esp_err_to_name(draw_result)
            );
            if (queued_transfers > 0U) {
                const esp_err_t drain_result =
                    esp_lcd_panel_io_tx_param(io_handle, -1, NULL, 0);
                if (drain_result != ESP_OK) {
                    ++display_dma_transfer_failure_count;
                    ESP_LOGE(
                        TAG,
                        "Bounced draw error cleanup failed count=%lu (%s)",
                        (unsigned long)display_dma_transfer_failure_count,
                        esp_err_to_name(drain_result)
                    );
                }
            }
            return draw_result;
        }
        ++queued_transfers;
        if (queued_transfers == bounce_buffer_count ||
            chunk_end == y_end) {
            const esp_err_t drain_result =
                esp_lcd_panel_io_tx_param(io_handle, -1, NULL, 0);
            if (drain_result != ESP_OK) {
                ++display_dma_transfer_failure_count;
                ESP_LOGE(
                    TAG,
                    "Bounced draw queue drain failed at rows %d-%d "
                    "count=%lu (%s)",
                    chunk_y,
                    chunk_end - 1,
                    (unsigned long)display_dma_transfer_failure_count,
                    esp_err_to_name(drain_result)
                );
                return drain_result;
            }
            queued_transfers = 0;
        }
    }
    return ESP_OK;
}

static void bsp_display_invalidate_cached_rows(int y_start, int y_end)
{
    if (y_start < 0) {
        y_start = 0;
    }
    if (y_end > BSP_LCD_V_RES) {
        y_end = BSP_LCD_V_RES;
    }
    for (int y = y_start; y < y_end; ++y) {
        if (display_frame_cache_rows[y]) {
            display_frame_cache_rows[y] = false;
            if (display_frame_cache_full_row_count > 0U) {
                --display_frame_cache_full_row_count;
            }
        }
    }
    display_frame_cache_valid = false;
}

// Keep the last rendered RGB565 frame in PSRAM. Rotation can then replay the
// already-rasterized UI instead of asking LVGL to redraw every gradient/shadow.
static esp_err_t bsp_display_draw_bitmap_cached(
    lv_display_t *display,
    esp_lcd_panel_handle_t panel,
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *color_map,
    void *user_ctx
)
{
    (void)display;
    (void)user_ctx;
    if (panel == NULL || color_map == NULL || display_frame_cache == NULL ||
        x_start < 0 || y_start < 0 ||
        x_end > BSP_LCD_H_RES || y_end > BSP_LCD_V_RES ||
        x_start >= x_end || y_start >= y_end) {
        ESP_LOGE(
            TAG,
            "Cached draw rejected: panel=%p pixels=%p cache=%p area=[%d,%d,%d,%d]",
            panel,
            color_map,
            display_frame_cache,
            x_start,
            y_start,
            x_end,
            y_end
        );
        return ESP_ERR_INVALID_ARG;
    }

    const size_t bytes_per_pixel = BSP_LCD_BITS_PER_PIXEL / 8U;
    const size_t source_row_bytes =
        (size_t)(x_end - x_start) * bytes_per_pixel;
    const size_t bounce_rows =
        (size_t)(y_end - y_start) < BSP_DISPLAY_DMA_TRANSFER_HEIGHT
            ? (size_t)(y_end - y_start)
            : BSP_DISPLAY_DMA_TRANSFER_HEIGHT;
    const size_t chunk_count =
        ((size_t)(y_end - y_start) +
         BSP_DISPLAY_DMA_TRANSFER_HEIGHT - 1U) /
        BSP_DISPLAY_DMA_TRANSFER_HEIGHT;
    size_t bounce_buffer_count =
        chunk_count < BSP_DISPLAY_DMA_PIPELINE_DEPTH
            ? chunk_count
            : BSP_DISPLAY_DMA_PIPELINE_DEPTH;
    const uint8_t *source = (const uint8_t *)color_map;

    bsp_display_dma_lease_t dma_lease = {0};
    const esp_err_t lease_result = bsp_display_acquire_dma_pipeline(
        source_row_bytes,
        bounce_rows,
        bounce_buffer_count,
        &dma_lease
    );
    if (lease_result != ESP_OK) {
        bsp_display_invalidate_cached_rows(y_start, y_end);
        return lease_result;
    }
    const esp_err_t result = bsp_display_draw_bitmap_bounced(
        panel,
        x_start,
        y_start,
        x_end,
        y_end,
        color_map,
        source_row_bytes,
        dma_lease.data,
        dma_lease.slot_stride_bytes,
        dma_lease.buffer_count,
        dma_lease.transfer_rows
    );
    bsp_display_release_dma_pipeline(&dma_lease);
    if (result != ESP_OK) {
        bsp_display_invalidate_cached_rows(y_start, y_end);
        return result;
    }
    for (int y = y_start; y < y_end; ++y) {
        uint8_t *destination =
            display_frame_cache +
            (((size_t)y * BSP_LCD_H_RES + (size_t)x_start) * bytes_per_pixel);
        memcpy(
            destination,
            source + ((size_t)(y - y_start) * source_row_bytes),
            source_row_bytes
        );
    }
    if (x_start == 0 && x_end == BSP_LCD_H_RES) {
        for (int y = y_start; y < y_end; ++y) {
            if (!display_frame_cache_rows[y]) {
                display_frame_cache_rows[y] = true;
                ++display_frame_cache_full_row_count;
            }
        }
        display_frame_cache_valid =
            display_frame_cache_full_row_count == BSP_LCD_V_RES;
    }
    return result;
}

static esp_err_t bsp_display_replay_cached_frame(void)
{
    if (!display_frame_cache_valid || display_frame_cache == NULL ||
        io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t row_bytes =
        (size_t)BSP_LCD_H_RES * BSP_LCD_BITS_PER_PIXEL / 8U;
    const size_t bytes_per_pixel = BSP_LCD_BITS_PER_PIXEL / 8U;
    bsp_display_dma_lease_t dma_lease = {0};
    const esp_err_t lease_result = bsp_display_acquire_dma_pipeline(
        row_bytes,
        BSP_DISPLAY_DMA_TRANSFER_HEIGHT,
        1U,
        &dma_lease
    );
    if (lease_result != ESP_OK) {
        return lease_result;
    }

    size_t replayed_pixels = 0;
    for (int y = 0; y < BSP_LCD_V_RES;
         y += (int)BSP_DISPLAY_REPLAY_TRANSFER_HEIGHT) {
        const int y_end =
            y + (int)BSP_DISPLAY_REPLAY_TRANSFER_HEIGHT < BSP_LCD_V_RES
            ? y + (int)BSP_DISPLAY_REPLAY_TRANSFER_HEIGHT
            : BSP_LCD_V_RES;
        const int center_twice = BSP_LCD_V_RES - 1;
        const int radius_twice = BSP_LCD_V_RES;
        const int first_distance = abs(y * 2 - center_twice);
        const int last_distance = abs((y_end - 1) * 2 - center_twice);
        const int nearest_distance =
            first_distance < last_distance ? first_distance : last_distance;
        const uint32_t squared_half_width =
            (uint32_t)(radius_twice * radius_twice -
                       nearest_distance * nearest_distance);
        uint32_t half_width_twice = 0;
        uint32_t test_bit = 1UL << 30;
        uint32_t remainder = squared_half_width;
        while (test_bit > remainder) {
            test_bit >>= 2;
        }
        while (test_bit != 0) {
            if (remainder >= half_width_twice + test_bit) {
                remainder -= half_width_twice + test_bit;
                half_width_twice = (half_width_twice >> 1) + test_bit;
            } else {
                half_width_twice >>= 1;
            }
            test_bit >>= 2;
        }
        int x_start =
            (center_twice - (int)half_width_twice) / 2 -
            BSP_DISPLAY_CIRCLE_EDGE_PADDING;
        int x_end =
            (center_twice + (int)half_width_twice + 1) / 2 + 1 +
            BSP_DISPLAY_CIRCLE_EDGE_PADDING;
        if (x_start < 0) {
            x_start = 0;
        }
        if (x_end > BSP_LCD_H_RES) {
            x_end = BSP_LCD_H_RES;
        }

        const esp_err_t result = bsp_display_draw_bitmap_bounced(
            panel_handle,
            x_start,
            y,
            x_end,
            y_end,
            display_frame_cache +
                (size_t)y * row_bytes +
                (size_t)x_start * bytes_per_pixel,
            row_bytes,
            dma_lease.data,
            dma_lease.slot_stride_bytes,
            dma_lease.buffer_count,
            dma_lease.transfer_rows
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Cached rotation replay failed at [%d,%d,%d,%d] (%s)",
                x_start,
                y,
                x_end,
                y_end,
                esp_err_to_name(result)
            );
            bsp_display_release_dma_pipeline(&dma_lease);
            bsp_display_invalidate_cached_rows(0, BSP_LCD_V_RES);
            return result;
        }
        replayed_pixels +=
            (size_t)(x_end - x_start) * (size_t)(y_end - y);
    }
    ESP_LOGD(
        TAG,
        "Cached round-frame replay transferred %u/%u pixels",
        (unsigned int)replayed_pixels,
        (unsigned int)((size_t)BSP_LCD_H_RES * BSP_LCD_V_RES)
    );
    bsp_display_release_dma_pipeline(&dma_lease);
    return ESP_OK;
}

#endif // CONFIG_LUNASHELL_BUILD_ENABLE_UI

#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
// panel_io: SPI panel transport reporting a finished pixel transfer.
// event_data: Driver completion metadata, not needed for the one-strip transaction.
// user_context: Static semaphore retained for the lifetime of the panel transport.
static bool IRAM_ATTR bsp_display_color_transfer_done(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *event_data,
    void *user_context
)
{
    (void)panel_io;
    (void)event_data;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xSemaphoreGiveFromISR((SemaphoreHandle_t)user_context, &higher_priority_task_woken) != pdTRUE) {
        // Report from task context; an IRQ must not allocate or log.
        display_completion_fault = true;
    }
    return higher_priority_task_woken == pdTRUE;
}
#endif

// config: Maximum pixel payload admitted to one SPI transfer.
// ret_panel: Optional output for the initialized panel handle.
// ret_io: Optional output for the initialized panel transport handle.
// Exception: Returns real transport/panel initialization and cleanup failures.
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    bool spi_bus_initialized = false;
    if (ret_panel != NULL && ret_panel != &panel_handle) {
        *ret_panel = NULL;
    }
    if (ret_io != NULL && ret_io != &io_handle) {
        *ret_io = NULL;
    }
    if (config == NULL || config->max_transfer_sz <= 0) {
        ESP_LOGE(TAG, "Display creation rejected: invalid transfer configuration (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (panel_handle != NULL || io_handle != NULL || display_spi_bus_initialized) {
        ESP_LOGE(TAG, "Display creation rejected: resources already initialized (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (display_mutex == NULL) {
        display_mutex = xSemaphoreCreateRecursiveMutexStatic(&display_mutex_storage);
    }
    if (display_completion == NULL) {
        display_completion = xSemaphoreCreateBinaryStatic(&display_completion_storage);
    }
    if (display_mutex == NULL || display_completion == NULL) {
        ESP_LOGE(TAG, "Display synchronization creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
#endif

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK,
                                                                 BSP_LCD_DATA0,
                                                                 BSP_LCD_DATA1,
                                                                 BSP_LCD_DATA2,
                                                                 BSP_LCD_DATA3,
                                                                 config->max_transfer_sz);
    ESP_GOTO_ON_ERROR(
        spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO),
        fail,
        TAG,
        "LCD SPI bus initialization failed"
    );
    spi_bus_initialized = true;
    display_spi_bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH;
    co5300_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    io_config.on_color_trans_done = bsp_display_color_transfer_done;
    io_config.user_ctx = display_completion;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io_handle);
#else
    ret = bsp_display_new_qspi_io(BSP_LCD_SPI_NUM, &io_config, &io_handle);
#endif
    ESP_GOTO_ON_ERROR(
        ret,
        fail,
        TAG,
        "LCD panel IO initialization failed"
    );
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_co5300(io_handle, &panel_config, &panel_handle),
        fail,
        TAG,
        "LCD panel allocation failed"
    );
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_set_gap(panel_handle, 0x06, 0),
        fail,
        TAG,
        "LCD gap configuration failed"
    );
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_reset(panel_handle),
        fail,
        TAG,
        "LCD reset failed"
    );
    initial_panel_brightness = esp_reset_reason() == ESP_RST_DEEPSLEEP ? 0 : 0xFF;
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_init(panel_handle),
        fail,
        TAG,
        "LCD initialization failed"
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const bool enable_output = true;
#else
    const bool enable_output = false;
#endif
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_disp_on_off(panel_handle, enable_output),
        fail,
        TAG,
        "LCD output initialization failed"
    );
    display_panel_output_enabled = enable_output;
    __atomic_store_n(&display_initialized, true, __ATOMIC_RELEASE);
    __atomic_store_n(&display_content_revision, 1U, __ATOMIC_RELEASE);

    if (ret_panel)
    {
        *ret_panel = panel_handle;
    }
    if (ret_io)
    {
        *ret_io = io_handle;
    }
    return ESP_OK;

fail:
    if (panel_handle != NULL) {
        const esp_err_t cleanup_result = esp_lcd_panel_del(panel_handle);
        if (cleanup_result == ESP_OK) {
            panel_handle = NULL;
            display_panel_output_enabled = false;
            __atomic_store_n(&display_initialized, false, __ATOMIC_RELEASE);
        } else {
            ESP_LOGE(
                TAG,
                "LCD panel cleanup failed: %s",
                esp_err_to_name(cleanup_result)
            );
            return ret;
        }
    }
    if (io_handle != NULL) {
        const esp_err_t cleanup_result = esp_lcd_panel_io_del(io_handle);
        if (cleanup_result == ESP_OK) {
            io_handle = NULL;
        } else {
            ESP_LOGE(
                TAG,
                "LCD panel IO cleanup failed: %s",
                esp_err_to_name(cleanup_result)
            );
            return ret;
        }
    }
    if (spi_bus_initialized) {
        const esp_err_t cleanup_result = spi_bus_free(BSP_LCD_SPI_NUM);
        if (cleanup_result == ESP_OK) {
            display_spi_bus_initialized = false;
        } else {
            ESP_LOGE(
                TAG,
                "LCD SPI bus cleanup failed: %s",
                esp_err_to_name(cleanup_result)
            );
            return ret;
        }
    }
    return ret;
}

#endif // CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY

#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
// The CST9217 reports on IRQ edges; polling its data registers while idle is
// not a current measurement. The ISR only latches work for the owning task.
static uint32_t touch_report_pending;
static uint32_t touch_report_irq_at_us;
static StaticSemaphore_t touch_report_signal_storage;
static SemaphoreHandle_t touch_report_signal;
// Input gesture latches guard rotation; these are never published as new samples.
static bool touch_contact_pressed;
static esp_err_t touch_last_report_error;

// touch_handle: Controller that raised a report-ready interrupt; no I2C is done here.
static void IRAM_ATTR bsp_touch_report_interrupt(esp_lcd_touch_handle_t touch_handle)
{
    (void)touch_handle;
    __atomic_store_n(&touch_report_irq_at_us, (uint32_t)esp_timer_get_time(), __ATOMIC_RELAXED);
    __atomic_store_n(&touch_report_pending, 1U, __ATOMIC_RELEASE);
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(touch_report_signal, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
#endif

// config: Raw controller coordinate transformation, independent of a renderer.
// ret_touch: Output handle, cleared before the initialization attempt.
esp_err_t bsp_touch_new_raw(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    if (config == NULL || ret_touch == NULL) {
        ESP_LOGE(TAG, "Touch creation rejected: configuration or output handle is NULL (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (ret_touch != &tp) {
        *ret_touch = NULL;
    }
    if (tp != NULL || touch_io_handle != NULL) {
        ESP_LOGE(TAG, "Touch creation rejected: controller already initialized (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }

#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    __atomic_store_n(&touch_report_pending, 0U, __ATOMIC_RELEASE);
    // Same binary IRQ signal as Luna's input adapter, without a GUI dependency.
    touch_report_signal = xSemaphoreCreateBinaryStatic(&touch_report_signal_storage);
    if (touch_report_signal == NULL) {
        ESP_LOGE(TAG, "Touch IRQ signal creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
#endif

    ESP_RETURN_ON_ERROR(
        bsp_i2c_init(),
        TAG,
        "Touch I2C initialization failed"
    );

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
#else
        .x_max = BSP_LCD_H_RES - 1U,
        .y_max = BSP_LCD_V_RES - 1U,
#endif
        .rst_gpio_num = BSP_LCD_TOUCH_RST, // Dedicated touch reset GPIO40; the panel uses GPIO39.
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = config->swap_xy,
            .mirror_x = config->mirror_x,
            .mirror_y = config->mirror_y,
        },
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
        .interrupt_callback = bsp_touch_report_interrupt,
#endif
    };
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(
            i2c_handle,
            &tp_io_config,
            &touch_io_handle
        ),
        TAG,
        "Touch panel IO initialization failed"
    );
    const esp_err_t touch_result =
        esp_lcd_touch_new_i2c_cst9217(touch_io_handle, &tp_cfg, ret_touch);
    if (touch_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Touch controller initialization failed: %s",
            esp_err_to_name(touch_result)
        );
        const esp_err_t cleanup_result =
            esp_lcd_panel_io_del(touch_io_handle);
        if (cleanup_result == ESP_OK) {
            touch_io_handle = NULL;
        } else {
            ESP_LOGE(
                TAG,
                "Touch panel IO cleanup failed: %s",
                esp_err_to_name(cleanup_result)
            );
        }
        return touch_result;
    }
    tp = *ret_touch;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    // Include a report already asserted between configuration and ISR registration.
    if (gpio_get_level(BSP_LCD_TOUCH_INT) == tp->config.levels.interrupt) {
        __atomic_store_n(&touch_report_irq_at_us, (uint32_t)esp_timer_get_time(), __ATOMIC_RELAXED);
        __atomic_store_n(&touch_report_pending, 1U, __ATOMIC_RELEASE);
        xSemaphoreGive(touch_report_signal);
    }
    ESP_LOGI(TAG, "Standalone touch input ready: IRQ GPIO%d, binary report signal", BSP_LCD_TOUCH_INT);
#endif
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// cfg: Existing Luna adapter settings, including the wired touch-axis transform.
// ret_touch: Output handle for the current legacy UI initialization sequence.
esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch)
{
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Legacy touch configuration rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const bsp_touch_config_t touch_config = {
        .swap_xy = cfg->touch_flags.swap_xy,
        .mirror_x = cfg->touch_flags.mirror_x,
        .mirror_y = cfg->touch_flags.mirror_y,
    };
    return bsp_touch_new_raw(&touch_config, ret_touch);
}
#endif
#endif // CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH

/**************************************************************************************************
 *
 * IO Expander Function
 *
 **************************************************************************************************/
esp_err_t bsp_io_expander_init(
    esp_io_expander_handle_t *out_handle
)
{
    if (out_handle == NULL) {
        ESP_LOGE(TAG, "IO expander output handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;
    ESP_RETURN_ON_ERROR(
        bsp_i2c_init(),
        TAG,
        "IO expander I2C initialization failed"
    );
    if (io_expander == NULL) {
        ESP_RETURN_ON_ERROR(
            esp_io_expander_new_i2c_tca9554(
                i2c_handle,
                BSP_IO_EXPANDER_I2C_ADDRESS,
                &io_expander
            ),
            TAG,
            "IO expander initialization failed"
        );
    }
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "IO expander initialization returned no handle");
        return ESP_ERR_INVALID_STATE;
    }
    *out_handle = io_expander;
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// cfg: Selected panel and LVGL adapter configuration.
// out_display: Receives the adapter display only after initialization succeeds.
static esp_err_t bsp_display_lcd_init(
    const bsp_display_cfg_t *cfg,
    lv_display_t **out_display
)
{
    if (cfg == NULL || out_display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;
    ESP_RETURN_ON_ERROR(
        bsp_display_validate_emergency_buffer(),
        TAG, "Display emergency DMA row validation failed"
    );

    // Rasterize into tall PSRAM tiles, then feed CO5300 QSPI through the
    // transient internal bounce buffer owned by each cached draw callback.
    const bsp_display_config_t disp_config = {
        .max_transfer_sz =
            BSP_LCD_H_RES * BSP_DISPLAY_DMA_TRANSFER_HEIGHT *
            BSP_LCD_BITS_PER_PIXEL / 8,
    };

    ESP_RETURN_ON_ERROR(
        bsp_display_new(&disp_config, &panel_handle, &io_handle),
        TAG,
        "LCD hardware initialization failed"
    );

    ESP_LOGD(TAG, "Add LCD screen");
    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel_handle,
        .panel_io = io_handle,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = cfg->rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = BSP_DISPLAY_DRAW_BUFFER_HEIGHT,
            .use_psram = true,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = cfg->tear_avoid_mode,
    };

    display_handle = esp_lv_adapter_register_display(&disp_cfg);
    if (display_handle == NULL) {
        ESP_LOGE(TAG, "Registering LCD with the LVGL adapter failed");
        return ESP_FAIL;
    }

    display_frame_cache = heap_caps_aligned_alloc(
        64,
        BSP_DISPLAY_FRAME_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (display_frame_cache == NULL ||
        !esp_ptr_external_ram(display_frame_cache)) {
        if (display_frame_cache != NULL) {
            heap_caps_free(display_frame_cache);
            display_frame_cache = NULL;
        }
        ESP_LOGE(
            TAG,
            "Display frame cache requires %lu bytes of PSRAM",
            (unsigned long)BSP_DISPLAY_FRAME_BYTES
        );
        return ESP_ERR_NO_MEM;
    }
    memset(display_frame_cache_rows, 0, sizeof(display_frame_cache_rows));
    display_frame_cache_full_row_count = 0;
    display_frame_cache_valid = false;

    const esp_lv_adapter_draw_bitmap_callbacks_t draw_callbacks = {
        .custom_draw_bitmap = bsp_display_draw_bitmap_cached,
        .waits_until_complete = true,
    };
    const esp_err_t callback_result =
        esp_lv_adapter_set_draw_bitmap_callbacks(
            display_handle,
            &draw_callbacks,
            NULL
        );
    if (callback_result != ESP_OK) {
        heap_caps_free(display_frame_cache);
        display_frame_cache = NULL;
        ESP_LOGE(
            TAG,
            "Registering the display frame cache failed (%s)",
            esp_err_to_name(callback_result)
        );
        return callback_result;
    }
    ESP_LOGI(
        TAG,
        "PSRAM render pipeline ready: draw_rows=%u cache=%lu bytes "
        "transient_DMA=%lu bytes emergency_DMA=%lu bytes headroom=%u",
        (unsigned int)BSP_DISPLAY_DRAW_BUFFER_HEIGHT,
        (unsigned long)BSP_DISPLAY_FRAME_BYTES,
        (unsigned long)BSP_DISPLAY_DMA_PIPELINE_BYTES,
        (unsigned long)BSP_DISPLAY_DMA_EMERGENCY_BYTES,
        (unsigned int)BSP_DISPLAY_DMA_HEADROOM_BYTES
    );

    lv_display_add_event_cb(
        display_handle,
        rounder_event_cb,
        LV_EVENT_INVALIDATE_AREA,
        NULL
    );

    *out_display = display_handle;
    return ESP_OK;
}

// cfg: Selected display adapter and touch coordinate mapping.
// disp: Initialized display to which the single input owner is registered.
static esp_err_t bsp_display_indev_init(
    const bsp_display_cfg_t *cfg,
    lv_display_t *disp
)
{
    if (cfg == NULL || disp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(
        bsp_touch_new(cfg, &tp),
        TAG,
        "Touch hardware initialization failed"
    );

    esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);
    // Keep the adapter as the only report reader. Its ISR observer can wake
    // the power owner even while the renderer and LVGL input loop are parked.
    touch_cfg.callbacks.on_interrupt = bsp_touch_wake_interrupt;

    disp_indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (disp_indev == NULL) {
        ESP_LOGE(TAG, "Registering touch with the LVGL adapter failed");
        return ESP_FAIL;
    }
    lv_indev_add_event_cb(
        disp_indev,
        bsp_touch_activity_event_cb,
        LV_EVENT_PRESSED,
        NULL
    );
    return ESP_OK;
}

static esp_err_t bsp_display_cleanup_failed_start(bool adapter_initialized)
{
    if (adapter_initialized) {
        const esp_err_t result = esp_lv_adapter_deinit();
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "LVGL adapter cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        display_handle = NULL;
        disp_indev = NULL;
    }
    if (display_frame_cache != NULL) {
        heap_caps_free(display_frame_cache);
        display_frame_cache = NULL;
        display_frame_cache_full_row_count = 0;
        display_frame_cache_valid = false;
        memset(display_frame_cache_rows, 0, sizeof(display_frame_cache_rows));
    }
    if (tp != NULL) {
        const esp_err_t result = esp_lcd_touch_del(tp);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Touch controller cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        tp = NULL;
    }
    if (touch_io_handle != NULL) {
        const esp_err_t result = esp_lcd_panel_io_del(touch_io_handle);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Touch panel IO cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        touch_io_handle = NULL;
    }
    if (panel_handle != NULL) {
        const esp_err_t result = esp_lcd_panel_del(panel_handle);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "LCD panel cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        panel_handle = NULL;
        display_panel_output_enabled = false;
    }
    if (io_handle != NULL) {
        const esp_err_t result = esp_lcd_panel_io_del(io_handle);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "LCD panel IO cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        io_handle = NULL;
    }
    if (display_spi_bus_initialized) {
        const esp_err_t result = spi_bus_free(BSP_LCD_SPI_NUM);
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "LCD SPI bus cleanup failed: %s",
                esp_err_to_name(result)
            );
            return result;
        }
        display_spi_bus_initialized = false;
    }
    return ESP_OK;
}
/**********************************************************************************************************
 *
 * Display Function
 *
 **********************************************************************************************************/
esp_err_t bsp_display_start(lv_display_t **out_display)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1}};
    return bsp_display_start_with_config(&cfg, out_display);
}

// cfg: Borrowed physical display and official LVGL adapter configuration.
// out_display: Receives the initialized display only on successful startup.
// Exception: Returns original adapter/display failures; no alternate renderer is started.
esp_err_t bsp_display_start_with_config(
    const bsp_display_cfg_t *cfg,
    lv_display_t **out_display
)
{
    if (cfg == NULL || out_display == NULL) {
        ESP_LOGE(TAG, "Display configuration or output handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;
    if (display_handle != NULL || panel_handle != NULL || io_handle != NULL) {
        ESP_LOGE(TAG, "Display is already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_lv_adapter_config_t adapter_config = cfg->lv_adapter_cfg;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    // The adapter default is unpinned. Optional speech requires an exclusive
    // renderer core even for callers supplying their own display configuration.
    adapter_config.task_core_id = LUNASHELL_RENDER_CORE_ID;
    ESP_LOGI(TAG, "LVGL renderer pinned to core %d; speech reserved for core %d",
             LUNASHELL_RENDER_CORE_ID, LUNASHELL_TTS_CORE_ID);
#endif
    esp_err_t result = esp_lv_adapter_init(&adapter_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter initialization failed: %s", esp_err_to_name(result));
        return result;
    }
    const bool adapter_initialized = true;

    result = bsp_display_lcd_init(cfg, out_display);
    if (result == ESP_OK) {
        result = bsp_display_indev_init(cfg, *out_display);
    }
    if (result == ESP_OK) {
        result = bsp_display_brightness_init();
    }
    if (result == ESP_OK) {
        result = esp_lv_adapter_start();
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Display startup failed: %s", esp_err_to_name(result));
        const esp_err_t cleanup_result =
            bsp_display_cleanup_failed_start(adapter_initialized);
        if (cleanup_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Display startup cleanup incomplete: %s",
                esp_err_to_name(cleanup_result)
            );
        }
        *out_display = NULL;
        return result;
    }

    // A successful full hardware initialization is the only implicit recovery
    // from an indeterminate runtime rotation transaction.
    current_rotation = BSP_DISPLAY_ROTATE_0;
    display_rotation_consistent = true;
    return ESP_OK;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

esp_err_t bsp_display_set_touch_activity_callback(
    bsp_touch_activity_callback_t callback,
    void *user_data
)
{
    if (disp_indev == NULL) {
        ESP_LOGE(TAG, "Touch activity observer rejected: input is unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    touch_activity_user_data = user_data;
    touch_activity_callback = callback;
    return ESP_OK;
}
#endif // CONFIG_LUNASHELL_BUILD_ENABLE_UI

typedef struct {
    bool panel_swap_xy;
    bool panel_mirror_x;
    bool panel_mirror_y;
    int panel_x_gap;
    int panel_y_gap;
    bool touch_swap_xy;
    bool touch_mirror_x;
    bool touch_mirror_y;
} bsp_display_rotation_config_t;

typedef enum {
    BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY = (1U << 0),
    BSP_DISPLAY_ROTATION_STEP_PANEL_MIRROR = (1U << 1),
    BSP_DISPLAY_ROTATION_STEP_PANEL_GAP = (1U << 2),
    BSP_DISPLAY_ROTATION_STEP_TOUCH_SWAP_XY = (1U << 3),
    BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_X = (1U << 4),
    BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_Y = (1U << 5),
} bsp_display_rotation_step_t;

typedef struct {
    esp_err_t error;
    uint32_t attempted_steps;
    bsp_display_rotation_step_t failed_step;
} bsp_display_rotation_apply_result_t;

// Keep CO5300 scan direction and CST9217 coordinates in the same cardinal frame.
// rotation: Requested cardinal orientation of the logical display surface.
// config: Matching panel and touch scan-direction transaction.
static bool bsp_display_rotation_config(
    bsp_display_rotation_t rotation,
    bsp_display_rotation_config_t *config
)
{
    if (config == NULL) {
        return false;
    }

    switch (rotation) {
    case BSP_DISPLAY_ROTATE_0:
        *config = (bsp_display_rotation_config_t){
            .panel_swap_xy = false, .panel_mirror_x = false, .panel_mirror_y = false,
            .panel_x_gap = 6, .panel_y_gap = 0,
            .touch_swap_xy = false, .touch_mirror_x = true, .touch_mirror_y = true,
        };
        return true;
    case BSP_DISPLAY_ROTATE_90:
        *config = (bsp_display_rotation_config_t){
            .panel_swap_xy = true, .panel_mirror_x = true, .panel_mirror_y = false,
            .panel_x_gap = 0, .panel_y_gap = 6,
            .touch_swap_xy = true, .touch_mirror_x = false, .touch_mirror_y = true,
        };
        return true;
    case BSP_DISPLAY_ROTATE_180:
        *config = (bsp_display_rotation_config_t){
            .panel_swap_xy = false, .panel_mirror_x = true, .panel_mirror_y = true,
            .panel_x_gap = 6, .panel_y_gap = 0,
            .touch_swap_xy = false, .touch_mirror_x = false, .touch_mirror_y = false,
        };
        return true;
    case BSP_DISPLAY_ROTATE_270:
        *config = (bsp_display_rotation_config_t){
            .panel_swap_xy = true, .panel_mirror_x = false, .panel_mirror_y = true,
            .panel_x_gap = 0, .panel_y_gap = 6,
            .touch_swap_xy = true, .touch_mirror_x = true, .touch_mirror_y = false,
        };
        return true;
    default:
        return false;
    }
}

static const char *bsp_display_rotation_step_name(
    bsp_display_rotation_step_t step
)
{
    switch (step) {
    case BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY:
        return "panel swap-xy";
    case BSP_DISPLAY_ROTATION_STEP_PANEL_MIRROR:
        return "panel mirror";
    case BSP_DISPLAY_ROTATION_STEP_PANEL_GAP:
        return "panel gap";
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_SWAP_XY:
        return "touch swap-xy";
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_X:
        return "touch mirror-x";
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_Y:
        return "touch mirror-y";
    default:
        return "unknown";
    }
}

// config: Desired coherent panel and touch coordinate transform.
// step: Single controller mutation to apply or roll back.
static esp_err_t bsp_display_apply_rotation_step(
    const bsp_display_rotation_config_t *config,
    bsp_display_rotation_step_t step
)
{
    switch (step) {
    case BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY:
        return esp_lcd_panel_swap_xy(panel_handle, config->panel_swap_xy);
    case BSP_DISPLAY_ROTATION_STEP_PANEL_MIRROR:
        return esp_lcd_panel_mirror(
            panel_handle,
            config->panel_mirror_x,
            config->panel_mirror_y
        );
    case BSP_DISPLAY_ROTATION_STEP_PANEL_GAP:
        return esp_lcd_panel_set_gap(
            panel_handle,
            config->panel_x_gap,
            config->panel_y_gap
        );
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_SWAP_XY:
        return esp_lcd_touch_set_swap_xy(tp, config->touch_swap_xy);
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_X:
        return esp_lcd_touch_set_mirror_x(tp, config->touch_mirror_x);
    case BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_Y:
        return esp_lcd_touch_set_mirror_y(tp, config->touch_mirror_y);
#endif
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

// Apply each controller mutation explicitly so an interrupted transaction can
// restore every operation that may have changed hardware state.
// config: Complete desired mapping of panel scan direction and touch axes.
static bsp_display_rotation_apply_result_t bsp_display_apply_rotation_config(
    const bsp_display_rotation_config_t *config
)
{
    static const bsp_display_rotation_step_t steps[] = {
        BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY,
        BSP_DISPLAY_ROTATION_STEP_PANEL_MIRROR,
        BSP_DISPLAY_ROTATION_STEP_PANEL_GAP,
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
        BSP_DISPLAY_ROTATION_STEP_TOUCH_SWAP_XY,
        BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_X,
        BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_Y,
#endif
    };
    bsp_display_rotation_apply_result_t result = {
        .error = ESP_OK,
        .attempted_steps = 0,
        .failed_step = BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY,
    };

    for (size_t index = 0; index < sizeof(steps) / sizeof(steps[0]); ++index) {
        const bsp_display_rotation_step_t step = steps[index];
        result.attempted_steps |= (uint32_t)step;
        result.error = bsp_display_apply_rotation_step(config, step);
        if (result.error != ESP_OK) {
            result.failed_step = step;
            ESP_LOGE(
                TAG,
                "Display rotation apply failed at %s (%s)",
                bsp_display_rotation_step_name(step),
                esp_err_to_name(result.error)
            );
            return result;
        }
    }

    return result;
}

static esp_err_t bsp_display_rollback_rotation_config(
    const bsp_display_rotation_config_t *config,
    uint32_t attempted_steps
)
{
    static const bsp_display_rotation_step_t rollback_steps[] = {
        BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_Y,
        BSP_DISPLAY_ROTATION_STEP_TOUCH_MIRROR_X,
        BSP_DISPLAY_ROTATION_STEP_TOUCH_SWAP_XY,
        BSP_DISPLAY_ROTATION_STEP_PANEL_GAP,
        BSP_DISPLAY_ROTATION_STEP_PANEL_MIRROR,
        BSP_DISPLAY_ROTATION_STEP_PANEL_SWAP_XY,
    };
    esp_err_t first_error = ESP_OK;

    for (size_t index = 0;
         index < sizeof(rollback_steps) / sizeof(rollback_steps[0]);
         ++index) {
        const bsp_display_rotation_step_t step = rollback_steps[index];
        if ((attempted_steps & (uint32_t)step) == 0) {
            continue;
        }
        const esp_err_t result = bsp_display_apply_rotation_step(config, step);
        if (result != ESP_OK) {
            if (first_error == ESP_OK) {
                first_error = result;
            }
            ESP_LOGE(
                TAG,
                "Display rotation rollback failed at %s (%s)",
                bsp_display_rotation_step_name(step),
                esp_err_to_name(result)
            );
        }
    }

    return first_error;
}

// rotation: Cardinal orientation; rotate coordinates without reflecting the artwork.
esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation)
{
    if (panel_handle == NULL
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
        || tp == NULL
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
        || display_handle == NULL
#else
        || __atomic_load_n(&display_dma_faulted, __ATOMIC_ACQUIRE)
#endif
    ) {
        ESP_LOGE(TAG, "Display or touch handle is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    bsp_display_rotation_config_t requested_config;
    if (!bsp_display_rotation_config(rotation, &requested_config)) {
        ESP_LOGE(TAG, "Invalid rotation value: %d", rotation);
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t lock_result = bsp_display_lock(100);
    if (lock_result != ESP_OK) {
        ESP_LOGE(TAG, "Display rotation lock failed (%s)", esp_err_to_name(lock_result));
        return lock_result;
    }
    if (!display_rotation_consistent) {
        ESP_LOGE(
            TAG,
            "Display rotation %d rejected: panel/touch state is inconsistent",
            rotation
        );
        bsp_display_unlock();
        return BSP_DISPLAY_ERR_ROTATION_INCONSISTENT;
    }
    if (!display_panel_output_enabled) {
        ESP_LOGE(TAG, "Display rotation rejected: panel output disabled (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (rotation == current_rotation) {
        bsp_display_unlock();
        return ESP_OK;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (disp_indev != NULL && lv_indev_get_state(disp_indev) == LV_INDEV_STATE_PRESSED) {
        ESP_LOGE(TAG, "Display rotation rejected: touch pressed (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }
#elif CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    // Do not consume the UI's next event just to check rotation. Pending input,
    // an unfinished gesture or a real acquisition failure defers the change.
    const bool report_pending = __atomic_load_n(&touch_report_pending, __ATOMIC_ACQUIRE) != 0U;
    if (report_pending || touch_contact_pressed || touch_last_report_error != ESP_OK) {
        const esp_err_t touch_result = touch_last_report_error != ESP_OK
                                          ? touch_last_report_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "Display rotation deferred by touch: pending=%d pressed=%d (%s)",
                 report_pending, touch_contact_pressed, esp_err_to_name(touch_result));
        bsp_display_unlock();
        return touch_result;
    }
#endif

    bsp_display_rotation_config_t previous_config;
    if (!bsp_display_rotation_config(current_rotation, &previous_config)) {
        display_rotation_consistent = false;
        ESP_LOGE(
            TAG,
            "Display rotation rejected: committed rotation %d is invalid",
            current_rotation
        );
        bsp_display_unlock();
        return BSP_DISPLAY_ERR_ROTATION_INCONSISTENT;
    }
    const int64_t started_at_us = esp_timer_get_time();
    display_rotation_consistent = false;
    const bsp_display_rotation_apply_result_t apply_result =
        bsp_display_apply_rotation_config(&requested_config);
    if (apply_result.error != ESP_OK) {
        const esp_err_t rollback_result =
            bsp_display_rollback_rotation_config(
                &previous_config,
                apply_result.attempted_steps
            );
        display_rotation_consistent = rollback_result == ESP_OK;
        if (rollback_result == ESP_OK) {
            ESP_LOGE(
                TAG,
                "Display rotation %d failed at %s (%s); previous state restored",
                rotation,
                bsp_display_rotation_step_name(apply_result.failed_step),
                esp_err_to_name(apply_result.error)
            );
        } else {
            ESP_LOGE(
                TAG,
                "Display rotation %d failed at %s (%s); rollback failed (%s), "
                "panel/touch state is inconsistent",
                rotation,
                bsp_display_rotation_step_name(apply_result.failed_step),
                esp_err_to_name(apply_result.error),
                esp_err_to_name(rollback_result)
            );
        }
        bsp_display_unlock();
        return rollback_result == ESP_OK
                   ? apply_result.error
                   : BSP_DISPLAY_ERR_ROTATION_INCONSISTENT;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const bool replay_ready = display_frame_cache_valid;
    if (replay_ready) {
        const esp_err_t replay_result = bsp_display_replay_cached_frame();
        if (replay_result != ESP_OK) {
            const esp_err_t rollback_result =
                bsp_display_rollback_rotation_config(
                    &previous_config,
                    apply_result.attempted_steps
                );
            display_rotation_consistent = rollback_result == ESP_OK;
            if (rollback_result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Display rotation replay failed (%s); rollback also failed "
                    "(%s), panel/touch state is inconsistent",
                    esp_err_to_name(replay_result),
                    esp_err_to_name(rollback_result)
                );
            } else {
                ESP_LOGE(
                    TAG,
                    "Display rotation replay failed (%s); previous state restored",
                    esp_err_to_name(replay_result)
                );
                lv_obj_t *screen = lv_display_get_screen_active(display_handle);
                if (screen != NULL) {
                    lv_obj_invalidate(screen);
                }
            }
            bsp_display_unlock();
            return rollback_result == ESP_OK
                       ? replay_result
                       : BSP_DISPLAY_ERR_ROTATION_INCONSISTENT;
        }
    }

    current_rotation = rotation;
    display_rotation_consistent = true;
    if (!replay_ready) {
        lv_obj_t *screen = lv_display_get_screen_active(display_handle);
        if (screen != NULL) {
            lv_obj_invalidate(screen);
        }
    }
#else
    current_rotation = rotation;
    display_rotation_consistent = true;
#endif
    __atomic_add_fetch(&display_content_revision, 1U, __ATOMIC_RELEASE);
    bsp_display_unlock();
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const char *render_path = replay_ready ? "PSRAM-cache" : "LVGL-redraw";
#else
    const char *render_path = "direct-redraw-required";
#endif
    ESP_LOGI(
        TAG,
        "CO5300 + CST9217 rotation: %d degrees, path=%s, queued=%lld us",
        rotation * 90,
        render_path,
        (long long)(esp_timer_get_time() - started_at_us)
    );
    return ESP_OK;
}

bsp_display_rotation_t bsp_display_rotation_get(void)
{
    return current_rotation;
}

bool bsp_display_rotation_is_consistent(void)
{
    return display_rotation_consistent;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// Pause the LVGL worker through the patched, bounded adapter transaction.
esp_err_t bsp_display_lvgl_pause(void)
{
    if (display_handle == NULL) {
        ESP_LOGE(TAG, "LVGL pause rejected: display is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&display_lvgl_state_lock);
    const bsp_display_lvgl_state_t previous_state = display_lvgl_state;
    if (previous_state == BSP_DISPLAY_LVGL_PAUSED) {
        portEXIT_CRITICAL(&display_lvgl_state_lock);
        return ESP_OK;
    }
    if (previous_state != BSP_DISPLAY_LVGL_ACTIVE) {
        portEXIT_CRITICAL(&display_lvgl_state_lock);
        ESP_LOGE(TAG, "LVGL pause rejected: transition state=%d", previous_state);
        return ESP_ERR_INVALID_STATE;
    }
    display_lvgl_state = BSP_DISPLAY_LVGL_PAUSING;
    portEXIT_CRITICAL(&display_lvgl_state_lock);

    const esp_err_t result = esp_lv_adapter_pause(
        (int32_t)BSP_DISPLAY_LVGL_PAUSE_TIMEOUT_MS
    );

    esp_err_t recovery_result = ESP_OK;
    if (result != ESP_OK) {
        // Resume is idempotent and reconciles any ambiguous tick-stop failure.
        recovery_result = esp_lv_adapter_resume();
        if (recovery_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "LVGL pause recovery failed after %s: %s",
                esp_err_to_name(result),
                esp_err_to_name(recovery_result)
            );
        }
    }

    portENTER_CRITICAL(&display_lvgl_state_lock);
    display_lvgl_state =
        result == ESP_OK
        ? BSP_DISPLAY_LVGL_PAUSED
        : (recovery_result == ESP_OK
            ? BSP_DISPLAY_LVGL_ACTIVE
            : BSP_DISPLAY_LVGL_FAULTED_PAUSED);
    portEXIT_CRITICAL(&display_lvgl_state_lock);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LVGL pause failed: %s", esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "LVGL worker and tick paused");
    return ESP_OK;
}

// Resume the LVGL worker while retaining paused state on adapter failure.
esp_err_t bsp_display_lvgl_resume(void)
{
    if (display_handle == NULL) {
        ESP_LOGE(TAG, "LVGL resume rejected: display is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&display_lvgl_state_lock);
    const bsp_display_lvgl_state_t previous_state = display_lvgl_state;
    if (previous_state == BSP_DISPLAY_LVGL_ACTIVE) {
        portEXIT_CRITICAL(&display_lvgl_state_lock);
        return ESP_OK;
    }
    if (previous_state != BSP_DISPLAY_LVGL_PAUSED &&
        previous_state != BSP_DISPLAY_LVGL_FAULTED_PAUSED) {
        portEXIT_CRITICAL(&display_lvgl_state_lock);
        ESP_LOGE(TAG, "LVGL resume rejected: transition state=%d", previous_state);
        return ESP_ERR_INVALID_STATE;
    }
    display_lvgl_state = BSP_DISPLAY_LVGL_RESUMING;
    portEXIT_CRITICAL(&display_lvgl_state_lock);

    const esp_err_t result = esp_lv_adapter_resume();

    portENTER_CRITICAL(&display_lvgl_state_lock);
    display_lvgl_state =
        result == ESP_OK ? BSP_DISPLAY_LVGL_ACTIVE : previous_state;
    portEXIT_CRITICAL(&display_lvgl_state_lock);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LVGL resume failed: %s", esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "LVGL worker and tick resumed");
    return ESP_OK;
}
#endif // CONFIG_LUNASHELL_BUILD_ENABLE_UI

// timeout_ms: Maximum wait for the renderer's shared display transaction mutex.
esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    return esp_lv_adapter_lock(timeout_ms);
#else
    if (timeout_ms > BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Display lock wait rejected: %lu ms exceeds %u ms (%s)", (unsigned long)timeout_ms, BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (display_mutex == NULL) {
        ESP_LOGE(TAG, "Display lock unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTakeRecursive(display_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Display lock timed out after %lu ms (%s)", (unsigned long)timeout_ms, esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }
    if (__atomic_load_n(&display_dma_faulted, __ATOMIC_ACQUIRE)) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "Display transaction rejected: DMA state requires explicit reset, retained=%p (%s)", display_dma_fault_lease.data, esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#endif
}

void bsp_display_unlock(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    esp_lv_adapter_unlock();
#else
    if (display_mutex == NULL || xSemaphoreGiveRecursive(display_mutex) != pdTRUE) {
        ESP_LOGE(TAG, "Display unlock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
    }
#endif
}
#endif // CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY

#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
// operation: Disabled display operation included in the component error log.
static esp_err_t bsp_display_disabled(const char *operation)
{
    ESP_LOGE(TAG, "%s unavailable: display excluded by build profile (%s)", operation, esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// config: Unused creation settings when display hardware is excluded.
// ret_panel: Optional output cleared to indicate unavailable hardware.
// ret_io: Optional output cleared to indicate unavailable hardware.
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    (void)config;
    if (ret_panel != NULL) {
        *ret_panel = NULL;
    }
    if (ret_io != NULL) {
        *ret_io = NULL;
    }
    return bsp_display_disabled("Display creation");
}

esp_err_t bsp_display_brightness_init(void)
{
    return bsp_display_disabled("Brightness initialization");
}

// brightness_percent: Requested value is rejected when no panel is configured.
esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    ESP_LOGE(TAG, "Brightness %d rejected: display excluded (%s)", brightness_percent, esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

int bsp_display_brightness_get(void)
{
    ESP_LOGE(TAG, "Brightness read unavailable: display excluded (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return -1;
}

// enabled: Requested output state is rejected when no panel is configured.
esp_err_t bsp_display_panel_set_enabled(bool enabled)
{
    ESP_LOGE(TAG, "Panel state %d rejected: display excluded (%s)", enabled, esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_disabled("Backlight enable");
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_disabled("Backlight disable");
}

// rotation: Requested orientation is rejected when no panel is configured.
esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation)
{
    ESP_LOGE(TAG, "Rotation %d rejected: display excluded (%s)", rotation, esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

bsp_display_rotation_t bsp_display_rotation_get(void)
{
    ESP_LOGE(TAG, "Rotation read unavailable: display excluded (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return BSP_DISPLAY_ROTATE_UNAVAILABLE;
}

bool bsp_display_rotation_is_consistent(void)
{
    return false;
}

// timeout_ms: Unused wait limit because this profile has no display owner.
esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return bsp_display_disabled("Display lock");
}

void bsp_display_unlock(void)
{
    ESP_LOGE(TAG, "Display unlock unavailable: display excluded (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
}
#endif

bool bsp_display_is_ready(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    return __atomic_load_n(&display_initialized, __ATOMIC_ACQUIRE) && display_handle != NULL;
#else
    return __atomic_load_n(&display_initialized, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&display_dma_faulted, __ATOMIC_ACQUIRE);
#endif
#else
    return false;
#endif
}

bool bsp_touch_is_ready(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    return tp != NULL;
#else
    return false;
#endif
}

uint32_t bsp_display_content_revision(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    return __atomic_load_n(&display_content_revision, __ATOMIC_ACQUIRE);
#else
    return 0;
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
static esp_err_t bsp_display_write_rgb565_internal(
    int x_start, int y_start, int x_end, int y_end, const void *pixels,
    bsp_display_rgb565_byte_order_t byte_order, bool clear_black
);
#endif

esp_err_t bsp_display_base_start(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const bsp_display_config_t display_config = {
        .max_transfer_sz = BSP_DISPLAY_DMA_ROW_BYTES * BSP_DISPLAY_STANDALONE_DMA_TRANSFER_HEIGHT,
    };
    ESP_RETURN_ON_ERROR(
        bsp_display_new(&display_config, &panel_handle, &io_handle),
        TAG, "Base display initialization failed"
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    const bsp_touch_config_t touch_config = {
        .swap_xy = false,
        .mirror_x = true,
        .mirror_y = true,
    };
    ESP_RETURN_ON_ERROR(
        bsp_touch_new_raw(&touch_config, &tp),
        TAG, "Base touch initialization failed"
    );
#endif
    ESP_RETURN_ON_ERROR(
        bsp_display_brightness_init(),
        TAG, "Base display brightness initialization failed"
    );
    ESP_RETURN_ON_ERROR(
        bsp_display_write_rgb565_internal(
            0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, NULL,
            BSP_DISPLAY_RGB565_BYTE_ORDER_PANEL, true
        ),
        TAG, "Base display black-frame initialization failed"
    );
    ESP_RETURN_ON_ERROR(
        bsp_display_panel_set_enabled(true),
        TAG, "Base display output enable failed"
    );
    current_rotation = BSP_DISPLAY_ROTATE_0;
    display_rotation_consistent = true;
    ESP_LOGI(TAG, "Base display and enabled touch initialized without a GUI worker");
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Standalone display startup unavailable in this profile (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// timeout_ms: Maximum idle wait; an IRQ wakes the consumer before its next frame deadline.
esp_err_t bsp_touch_wait_for_report(uint32_t timeout_ms)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (tp == NULL || touch_report_signal == NULL) {
        ESP_LOGE(TAG, "Touch IRQ wait rejected: not initialized (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (__atomic_load_n(&touch_report_pending, __ATOMIC_ACQUIRE) != 0U) {
        return ESP_OK;
    }
    const TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(touch_report_signal, wait_ticks) == pdTRUE
               ? ESP_OK : ESP_ERR_NOT_FOUND;
#else
    (void)timeout_ms;
    ESP_LOGE(TAG, "Standalone touch IRQ wait unavailable (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// point: Current report, or invalid state with the real error; no report has timestamp zero.
esp_err_t bsp_touch_read_point(bsp_touch_point_t *point)
{
    if (point == NULL) {
        ESP_LOGE(TAG, "Touch sample output rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    *point = (bsp_touch_point_t){
        .valid = false,
        .error = ESP_ERR_NOT_SUPPORTED,
        .sample_timestamp_us = 0,
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (tp == NULL) {
        point->error = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "Touch read rejected: controller not initialized (%s)", esp_err_to_name(point->error));
        return point->error;
    }
    const esp_err_t lock_result = bsp_display_lock(BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS);
    if (lock_result != ESP_OK) {
        point->error = lock_result;
        return lock_result;
    }
    if (__atomic_exchange_n(&touch_report_pending, 0U, __ATOMIC_ACQ_REL) == 0U) {
        // No IRQ means no new hardware sample, rather than a fabricated release.
        point->error = ESP_ERR_NOT_FOUND;
        bsp_display_unlock();
        return point->error;
    }
    const uint32_t irq_at_us = __atomic_load_n(&touch_report_irq_at_us, __ATOMIC_RELAXED);
    // Consume the wake signal together with this report. A newer IRQ keeps pending set.
    xSemaphoreTake(touch_report_signal, 0);
    const uint32_t read_started_us = (uint32_t)esp_timer_get_time();
    esp_err_t result = esp_lcd_touch_read_data(tp);
    esp_lcd_touch_point_data_t data = {0};
    uint8_t contact_count = 0;
    if (result == ESP_OK) {
        result = esp_lcd_touch_get_data(tp, &data, &contact_count, 1U);
    }
    point->sample_timestamp_us = esp_timer_get_time();
    if (result == ESP_OK && contact_count > 0U &&
        (data.x >= BSP_LCD_H_RES || data.y >= BSP_LCD_V_RES)) {
        result = ESP_ERR_INVALID_RESPONSE;
        ESP_LOGE(TAG, "Touch sample coordinate rejected: x=%u y=%u (%s)", data.x, data.y, esp_err_to_name(result));
    }
    point->error = result;
    touch_last_report_error = result;
    if (result == ESP_OK) {
        point->valid = true;
        point->pressed = contact_count > 0U;
        touch_contact_pressed = point->pressed;
        if (point->pressed) {
            point->x = data.x;
            point->y = data.y;
        }
    } else {
        ESP_LOGE(TAG, "Current touch sample failed: IRQ age=%luus read=%luus level=%d (%s)",
                 (unsigned long)(read_started_us - irq_at_us),
                 (unsigned long)((uint32_t)esp_timer_get_time() - read_started_us),
                 gpio_get_level(BSP_LCD_TOUCH_INT), esp_err_to_name(result));
    }
    bsp_display_unlock();
    return result;
#else
    ESP_LOGE(TAG, "Standalone touch sampling unavailable in this profile (%s)", esp_err_to_name(point->error));
    return point->error;
#endif
}

// budget_ms: Complete-frame limit, independent of the number of strips.
// Exception: Returns real lock/readiness/allocation errors without admitting a partial lease.
esp_err_t bsp_display_frame_begin(uint32_t budget_ms)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (budget_ms == 0 || budget_ms > BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Frame budget %lu rejected (%s)", (unsigned long)budget_ms,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t lock_error = bsp_display_lock(BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    if (__atomic_load_n(&display_frame.owner, __ATOMIC_ACQUIRE) != NULL ||
        !bsp_display_is_ready() || !display_panel_output_enabled) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "Frame begin rejected: nested frame or unavailable panel (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    display_frame.started_at_us = esp_timer_get_time();
    display_frame.deadline_us = display_frame.started_at_us + (int64_t)budget_ms * 1000;
    const esp_err_t lease_error = bsp_display_acquire_dma_pipeline(
        BSP_DISPLAY_DMA_ROW_BYTES, BSP_DISPLAY_STANDALONE_FRAME_SLOT_HEIGHT,
        BSP_DISPLAY_DMA_PIPELINE_DEPTH,
        &display_frame.lease
    );
    if (lease_error != ESP_OK) {
        bsp_display_unlock();
        return lease_error;
    }
    display_frame.rectangles = 0;
    display_frame.conversion_us = 0;
    display_frame.submission_us = 0;
    display_frame.completion_wait_us = 0;
    display_frame.render_slot = 0;
    display_frame.pending_slot = 0;
    display_frame.transfer_pending = false;
    display_frame.buffer_count = (uint16_t)display_frame.lease.buffer_count;
    display_frame.transfer_rows = (uint16_t)display_frame.lease.transfer_rows;
    display_frame.completion_error = ESP_OK;
    display_frame.error = ESP_OK;
    __atomic_store_n(&display_frame.owner, xTaskGetCurrentTaskHandle(), __ATOMIC_RELEASE);
    // Keep this lock level until end: panel and touch stay in the same orientation.
    return ESP_OK;
#else
    (void)budget_ms;
    ESP_LOGE(TAG, "Standalone frame begin unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// Only the owner may inspect or advance the frame while holding its inherited lock.
esp_err_t bsp_display_frame_check_deadline(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (__atomic_load_n(&display_frame.owner, __ATOMIC_ACQUIRE) != xTaskGetCurrentTaskHandle()) {
        ESP_LOGE(TAG, "Frame deadline check rejected: caller is not the owner (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (display_frame.error == ESP_OK && esp_timer_get_time() >= display_frame.deadline_us) {
        display_frame.error = ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "Complete frame exceeded its deadline after %lu rectangles (%s)",
                 (unsigned long)display_frame.rectangles, esp_err_to_name(display_frame.error));
    }
    return display_frame.error;
#else
    ESP_LOGE(TAG, "Standalone frame deadline unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
// error: Actual submission/completion failure; uncertain transfers retain their entire lease.
// operation: Owner operation included in the failure log.
static void bsp_display_frame_quarantine(esp_err_t error, const char *operation)
{
    if (display_frame.lease.data != NULL) {
        display_dma_fault_lease = display_frame.lease;
        __atomic_store_n(&display_dma_retained_bytes,
            (uint32_t)(display_frame.lease.slot_stride_bytes * display_frame.lease.buffer_count),
            __ATOMIC_RELEASE);
        display_frame.lease = (bsp_display_dma_lease_t){0};
    }
    // Pending ownership moved to quarantine; this frame can never borrow it again.
    display_frame.transfer_pending = false;
    display_frame.completion_error = error;
    __atomic_store_n(&display_dma_faulted, true, __ATOMIC_RELEASE);
    if (display_frame.error == ESP_OK) {
        display_frame.error = error;
    }
    ESP_LOGE(TAG, "%s failed; DMA retained until reset: bytes=%lu (%s)", operation,
        (unsigned long)__atomic_load_n(&display_dma_retained_bytes, __ATOMIC_ACQUIRE),
        esp_err_to_name(error));
}

// No parameters: retire the one queued transfer before reusing its slot or ending the frame.
// Exception: Returns the real timeout/completion error and quarantines ambiguous ownership.
static esp_err_t bsp_display_frame_retire_transfer(void)
{
    if (!display_frame.transfer_pending) {
        return ESP_OK;
    }
    const int64_t completion_started_us = esp_timer_get_time();
    const int64_t remaining_us = display_frame.deadline_us - completion_started_us;
    TickType_t wait_ticks = remaining_us > 0
        ? pdMS_TO_TICKS((uint32_t)((remaining_us + 999) / 1000)) : 0;
    if (remaining_us > 0 && wait_ticks == 0) {
        wait_ticks = 1;
    }
    esp_err_t error = ESP_OK;
    if (xSemaphoreTake(display_completion, wait_ticks) != pdTRUE) {
        error = ESP_ERR_TIMEOUT;
    } else if (display_completion_fault) {
        error = ESP_ERR_INVALID_STATE;
    }
    display_frame.completion_wait_us += (uint32_t)(esp_timer_get_time() - completion_started_us);
    if (error != ESP_OK) {
        display_frame.completion_error = error;
        bsp_display_frame_quarantine(error, "Frame DMA completion");
        return error;
    }
    display_frame.transfer_pending = false;
    return ESP_OK;
}
#endif

// pixels: Receives a proven-idle internal slot owned by the current frame.
// capacity_bytes: Usable row bytes, excluding alignment padding and other slots.
// Exception: Returns owner/deadline/completion errors without lending an active DMA slot.
esp_err_t bsp_display_frame_buffer(void **pixels, size_t *capacity_bytes)
{
    if (pixels == NULL || capacity_bytes == NULL) {
        ESP_LOGE(TAG, "Frame buffer outputs rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    *pixels = NULL;
    *capacity_bytes = 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const esp_err_t error = bsp_display_frame_check_deadline();
    if (error != ESP_OK) {
        return error;
    }
    if (display_frame.transfer_pending && display_frame.render_slot == display_frame.pending_slot) {
        const esp_err_t completion_error = bsp_display_frame_retire_transfer();
        if (completion_error != ESP_OK) {
            return completion_error;
        }
        const esp_err_t after_wait_error = bsp_display_frame_check_deadline();
        if (after_wait_error != ESP_OK) {
            return after_wait_error;
        }
    }
    // A second slot lets the caller rasterize while the prior slot is on the wire.
    // Low-memory one-slot leases retire first; neither mode overwrites active DMA.
    if (display_frame.lease.data == NULL || display_frame.lease.transfer_rows == 0) {
        ESP_LOGE(TAG, "Frame buffer unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    *pixels = bsp_display_dma_slot(&display_frame.lease, display_frame.render_slot);
    *capacity_bytes = BSP_DISPLAY_DMA_ROW_BYTES * display_frame.lease.transfer_rows;
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Standalone frame buffer unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// x_start: First included physical column.
// y_start: First included physical row.
// x_end: Exclusive column, bounded by the panel.
// y_end: Exclusive row, fitting one borrowed slot.
// pixels: Current frame-borrowed slot, already encoded in panel RGB565 byte order.
// Exception: Returns geometry/owner/submission/completion errors; failed DMA retains its lease.
esp_err_t bsp_display_frame_submit_rgb565(
    int x_start, int y_start, int x_end, int y_end, const void *pixels)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const esp_err_t deadline_error = bsp_display_frame_check_deadline();
    if (deadline_error != ESP_OK) {
        return deadline_error;
    }
    const uint8_t *expected = bsp_display_dma_slot(&display_frame.lease, display_frame.render_slot);
    if (pixels == NULL || pixels != expected || x_start < 0 || y_start < 0 ||
        x_end > BSP_LCD_H_RES || y_end > BSP_LCD_V_RES ||
        x_start >= x_end || y_start >= y_end ||
        (size_t)(y_end - y_start) > display_frame.lease.transfer_rows ||
        (display_frame.transfer_pending && display_frame.render_slot == display_frame.pending_slot)) {
        ESP_LOGE(TAG, "Frame RGB565 submission rejected: [%d,%d,%d,%d] pixels=%p expected=%p (%s)",
            x_start, y_start, x_end, y_end, pixels, expected, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // Keep at most one color transfer outstanding. Rasterization of this slot
    // already overlapped the preceding DMA; the existing binary IRQ signal is exact.
    const esp_err_t completion_error = bsp_display_frame_retire_transfer();
    if (completion_error != ESP_OK) {
        return completion_error;
    }
    const esp_err_t after_wait_error = bsp_display_frame_check_deadline();
    if (after_wait_error != ESP_OK) {
        return after_wait_error;
    }
    const int64_t submission_started_us = esp_timer_get_time();
    const esp_err_t error = esp_lcd_panel_draw_bitmap(
        panel_handle, x_start, y_start, x_end, y_end, pixels);
    display_frame.submission_us += (uint32_t)(esp_timer_get_time() - submission_started_us);
    if (error != ESP_OK) {
        bsp_display_frame_quarantine(error, "Frame RGB565 submission");
        return error;
    }
    display_frame.pending_slot = display_frame.render_slot;
    display_frame.transfer_pending = true;
    display_frame.render_slot = (display_frame.render_slot + 1U) % display_frame.lease.buffer_count;
    ++display_frame.rectangles;
    return ESP_OK;
#else
    (void)x_start;
    (void)y_start;
    (void)x_end;
    (void)y_end;
    (void)pixels;
    ESP_LOGE(TAG, "Frame RGB565 submission unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// frame_error: First failure from geometry, raster or a completed transfer.
// result: Nonnull output; a failed completion never becomes a successful frame.
// Exception: Preserves render/deadline/DMA errors while releasing only a proven-idle lease.
esp_err_t bsp_display_frame_end(esp_err_t frame_error, bsp_display_frame_result_t *result)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (result == NULL ||
        __atomic_load_n(&display_frame.owner, __ATOMIC_ACQUIRE) != xTaskGetCurrentTaskHandle()) {
        ESP_LOGE(TAG, "Frame end rejected: output=%p or wrong owner (%s)", result,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // Even a renderer error must retire previously queued pixels before release.
    const esp_err_t completion_error = bsp_display_frame_retire_transfer();
    const esp_err_t deadline_error = bsp_display_frame_check_deadline();
    const esp_err_t error = frame_error != ESP_OK ? frame_error :
        (completion_error != ESP_OK ? completion_error : deadline_error);
    *result = (bsp_display_frame_result_t){
        .duration_us = (uint32_t)(esp_timer_get_time() - display_frame.started_at_us),
        .rectangles = display_frame.rectangles,
        .conversion_us = display_frame.conversion_us,
        .submission_us = display_frame.submission_us,
        .completion_wait_us = display_frame.completion_wait_us,
        .retained_dma_bytes = __atomic_load_n(&display_dma_retained_bytes, __ATOMIC_ACQUIRE),
        .buffer_count = display_frame.buffer_count,
        .transfer_rows = display_frame.transfer_rows,
        .completion_error = display_frame.completion_error,
        .error = error,
    };
    // Uncertain DMA was moved to quarantine. Only a proven-idle lease reaches free.
    bsp_display_release_dma_pipeline(&display_frame.lease);
    __atomic_store_n(&display_frame.owner, NULL, __ATOMIC_RELEASE);
    bsp_display_unlock();
    return error;
#else
    (void)frame_error;
    (void)result;
    ESP_LOGE(TAG, "Standalone frame end unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
typedef uint32_t bsp_display_rgb565_pair_t __attribute__((__may_alias__));

// destination: Writable DMA bytes; may be the same address as source.
// source: Native little-endian RGB565 pixels readable for byte_count bytes.
// byte_count: Even payload size; every adjacent byte pair belongs to one pixel.
static inline void bsp_display_copy_rgb565_to_panel_order(
    uint8_t *destination, const uint8_t *source, size_t byte_count
)
{
    size_t byte = 0;
    if ((((uintptr_t)destination | (uintptr_t)source) & (sizeof(uint32_t) - 1U)) == 0U) {
        // The alias type makes the intended aligned word access explicit to
        // Xtensa GCC without violating caller-owned uint16_t surface storage.
        const bsp_display_rgb565_pair_t *source_pairs =
            (const bsp_display_rgb565_pair_t *)source;
        bsp_display_rgb565_pair_t *destination_pairs =
            (bsp_display_rgb565_pair_t *)destination;
        const size_t pair_count = byte_count / sizeof(uint32_t);
        for (size_t pair = 0; pair < pair_count; ++pair) {
            const uint32_t native_pair = source_pairs[pair];
            const uint32_t panel_pair =
                ((native_pair & UINT32_C(0x00FF00FF)) << 8U) |
                ((native_pair & UINT32_C(0xFF00FF00)) >> 8U);
            destination_pairs[pair] = panel_pair;
        }
        byte = pair_count * sizeof(uint32_t);
    }
    for (; byte < byte_count; byte += 2U) {
        // Load both bytes before writing because conversion is normally in place.
        const uint8_t low = source[byte];
        const uint8_t high = source[byte + 1U];
        destination[byte] = high;
        destination[byte + 1U] = low;
    }
}

// x_start: First column included in the dirty rectangle.
// y_start: First row included in the dirty rectangle.
// x_end: Exclusive last column; must remain inside the physical display.
// y_end: Exclusive last row; must remain inside the physical display.
// pixels: Packed RGB565 data, owned by the caller throughout the call.
// byte_order: Explicit source representation; invalid enum values are rejected.
// clear_black: Initialize the dark panel using bounded zeroed strips without a framebuffer.
// Exception: Drains prior frame DMA before synchronous reuse and retains uncertain transfers.
static esp_err_t bsp_display_write_rgb565_internal(
    int x_start, int y_start, int x_end, int y_end, const void *pixels,
    bsp_display_rgb565_byte_order_t byte_order, bool clear_black
)
{
    const bool valid_byte_order =
        byte_order == BSP_DISPLAY_RGB565_BYTE_ORDER_NATIVE ||
        byte_order == BSP_DISPLAY_RGB565_BYTE_ORDER_PANEL;
    if ((!clear_black && pixels == NULL) || x_start < 0 || y_start < 0 ||
        x_end > BSP_LCD_H_RES || y_end > BSP_LCD_V_RES ||
        x_start >= x_end || y_start >= y_end || !valid_byte_order) {
        ESP_LOGE(TAG, "RGB565 rectangle rejected: [%d,%d,%d,%d] pixels=%p order=%d (%s)",
                 x_start, y_start, x_end, y_end, pixels, (int)byte_order,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t lock_error = bsp_display_lock(BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    if (!bsp_display_is_ready() || (!clear_black && !display_panel_output_enabled) ||
        !display_rotation_consistent) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "RGB565 write rejected: panel unavailable (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const bool in_frame =
        __atomic_load_n(&display_frame.owner, __ATOMIC_ACQUIRE) == xTaskGetCurrentTaskHandle();
    const size_t source_row_bytes = (size_t)(x_end - x_start) * 2U;
    bsp_display_dma_lease_t rectangle_lease = {0};
    bsp_display_dma_lease_t *lease = in_frame ? &display_frame.lease : &rectangle_lease;
    esp_err_t error = ESP_OK;
    if (in_frame) {
        // A synchronous caller may reuse the first slot only after queued DMA retires.
        error = bsp_display_frame_retire_transfer();
        if (error == ESP_OK) {
            error = bsp_display_frame_check_deadline();
        }
    } else {
        const size_t rectangle_rows = (size_t)(y_end - y_start);
        const size_t transfer_rows = rectangle_rows < BSP_DISPLAY_STANDALONE_DMA_TRANSFER_HEIGHT
            ? rectangle_rows : BSP_DISPLAY_STANDALONE_DMA_TRANSFER_HEIGHT;
        error = bsp_display_acquire_dma_pipeline(source_row_bytes, transfer_rows, 1U, lease);
    }
    if (error != ESP_OK) {
        bsp_display_unlock();
        return error;
    }
    const int64_t deadline_us = in_frame ? display_frame.deadline_us
        : esp_timer_get_time() + (int64_t)BSP_DISPLAY_DIRECT_TRANSFER_TIMEOUT_MS * 1000;
    const uint8_t *source = (const uint8_t *)pixels;
    const bool source_in_first_slot = !clear_black &&
        (uintptr_t)source >= (uintptr_t)lease->data &&
        (uintptr_t)source < (uintptr_t)lease->data +
            lease->slot_stride_bytes;
    const uint8_t *second_slot = bsp_display_dma_slot(lease, 1U);
    const bool source_in_second_slot = !clear_black && second_slot != NULL &&
        (uintptr_t)source >= (uintptr_t)second_slot &&
        (uintptr_t)source < (uintptr_t)second_slot + lease->slot_stride_bytes;
    if ((source_in_first_slot || source_in_second_slot) && (!in_frame ||
        (source != lease->data && source != second_slot) ||
        (size_t)(y_end - y_start) > lease->transfer_rows)) {
        if (!in_frame) {
            bsp_display_release_dma_pipeline(lease);
        }
        bsp_display_unlock();
        ESP_LOGE(TAG, "Borrowed DMA raster exceeds one owned slot (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    for (int strip_y = y_start; strip_y < y_end; strip_y += (int)lease->transfer_rows) {
        const int next_row = strip_y + (int)lease->transfer_rows;
        const int strip_end = next_row < y_end ? next_row : y_end;
        const size_t strip_bytes = (size_t)(strip_end - strip_y) * source_row_bytes;
        const uint8_t *strip_source = clear_black ? NULL :
            source + (size_t)(strip_y - y_start) * source_row_bytes;
        const bool panel_order_in_place =
            byte_order == BSP_DISPLAY_RGB565_BYTE_ORDER_PANEL &&
            strip_source == lease->data;
        const bool has_conversion_work = clear_black || !panel_order_in_place;
        const int64_t conversion_started_us = in_frame && has_conversion_work
            ? esp_timer_get_time() : 0;
        if (clear_black) {
            memset(lease->data, 0, strip_bytes);
        } else if (byte_order == BSP_DISPLAY_RGB565_BYTE_ORDER_NATIVE) {
            bsp_display_copy_rgb565_to_panel_order(lease->data, strip_source, strip_bytes);
        } else if (!panel_order_in_place) {
            memcpy(lease->data, strip_source, strip_bytes);
        } else {
            // Cody rendered directly into the owned panel-order slot. Touching
            // it again would only burn CPU before the same bytes are submitted.
        }
        if (in_frame && has_conversion_work) {
            display_frame.conversion_us += (uint32_t)(esp_timer_get_time() - conversion_started_us);
        }
        if (esp_timer_get_time() >= deadline_us) {
            error = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "RGB565 deadline exhausted before row %d (%s)",
                     strip_y, esp_err_to_name(error));
            break;
        }
        const int64_t submission_started_us = in_frame ? esp_timer_get_time() : 0;
        error = esp_lcd_panel_draw_bitmap(panel_handle, x_start, strip_y, x_end, strip_end, lease->data);
        if (in_frame) {
            display_frame.submission_us += (uint32_t)(esp_timer_get_time() - submission_started_us);
        }
        if (error == ESP_OK) {
            const int64_t completion_started_us = in_frame ? esp_timer_get_time() : 0;
            const int64_t wait_us = deadline_us - esp_timer_get_time();
            const TickType_t wait_ticks = wait_us > 0
                ? pdMS_TO_TICKS((uint32_t)(wait_us / 1000)) : 0;
            if (xSemaphoreTake(display_completion, wait_ticks) != pdTRUE) {
                error = ESP_ERR_TIMEOUT;
            } else if (display_completion_fault) {
                error = ESP_ERR_INVALID_STATE;
            }
            if (in_frame) {
                // Elapsed semaphore wait is not CPU utilization: this stage
                // lets the task sleep while the queued color DMA completes.
                display_frame.completion_wait_us += (uint32_t)(esp_timer_get_time() - completion_started_us);
            }
        }
        if (error != ESP_OK) {
            // The pixels may still be on the road. Keep their buffer until reset.
            if (in_frame) {
                display_frame.completion_error = error;
            }
            display_dma_fault_lease = *lease;
            __atomic_store_n(&display_dma_retained_bytes,
                (uint32_t)(lease->slot_stride_bytes * lease->buffer_count), __ATOMIC_RELEASE);
            *lease = (bsp_display_dma_lease_t){0};
            __atomic_store_n(&display_dma_faulted, true, __ATOMIC_RELEASE);
            ESP_LOGE(TAG, "RGB565 transfer rows %d-%d failed; DMA retained until reset (%s)",
                     strip_y, strip_end - 1, esp_err_to_name(error));
            break;
        }
    }
    if (in_frame) {
        ++display_frame.rectangles;
        if (display_frame.error == ESP_OK) {
            display_frame.error = error;
        }
    } else {
        bsp_display_release_dma_pipeline(&rectangle_lease);
    }
    bsp_display_unlock();
    return error;
}
#endif

// x_start: First column included in the dirty rectangle.
// y_start: First row included in the dirty rectangle.
// x_end: Exclusive last column, bounded by the physical display.
// y_end: Exclusive last row, bounded by the physical display.
// pixels: Packed RGB565 rectangle, reusable after return.
// byte_order: Explicit source representation; invalid enum values remain errors.
esp_err_t bsp_display_write_rgb565(
    int x_start, int y_start, int x_end, int y_end, const void *pixels,
    bsp_display_rgb565_byte_order_t byte_order
)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    return bsp_display_write_rgb565_internal(
        x_start, y_start, x_end, y_end, pixels, byte_order, false
    );
#else
    (void)x_start;
    (void)y_start;
    (void)x_end;
    (void)y_end;
    (void)pixels;
    (void)byte_order;
    ESP_LOGE(TAG, "Standalone RGB565 output unavailable in this profile (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
