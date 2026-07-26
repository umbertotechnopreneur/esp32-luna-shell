#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"

#include "esp_codec_dev_defaults.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "bsp/display.h"
#include "bsp/touch.h"

static const char *TAG = "ESP32-S3-Touch-AMOLED-1.75";

static i2c_master_bus_handle_t i2c_handle = NULL; // I2C Handle
static bool i2c_initialized = false;
static esp_io_expander_handle_t io_expander = NULL; // IO expander tca9554 handle
static lv_display_t *display_handle = NULL;
static lv_indev_t *disp_indev = NULL;
static bsp_display_rotation_t current_rotation = BSP_DISPLAY_ROTATE_0;
sdmmc_card_t *bsp_sdcard = NULL; // Global uSD card handler

#define BSP_SDCARD_MAX_FILES               5
#define BSP_SDCARD_ALLOCATION_UNIT_SIZE    (16U * 1024U)
#define BSP_SDCARD_FORMAT_WORK_BUFFER_SIZE 4096U

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

static esp_lcd_touch_handle_t tp = NULL;
static esp_lcd_panel_io_handle_t touch_io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
static esp_lcd_panel_io_handle_t io_handle = NULL;
static bool display_spi_bus_initialized = false;
uint8_t brightness;
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t *i2s_data_if = NULL; /* Codec data interface */
static bool i2s_tx_enabled;
static bool i2s_rx_enabled;
static bool audio_resources_poisoned;
static esp_codec_dev_handle_t speaker_codec_device;
static esp_codec_dev_handle_t microphone_codec_device;
static const audio_codec_gpio_if_t *speaker_gpio_interface;
static const audio_codec_ctrl_if_t *speaker_control_interface;
static const audio_codec_if_t *speaker_codec_interface;
static const audio_codec_ctrl_if_t *microphone_control_interface;
static const audio_codec_if_t *microphone_codec_interface;

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

static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG,                                                                 \
    }

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
    // The removable 1-bit bus uses conservative runtime clocking.
    host.max_freq_khz = 10000;
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
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
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

    out_result->stage = BSP_SDCARD_STAGE_CARD_INIT;
    result = sdmmc_card_init(
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

/**************************************************************************************************
 *
 * I2S Audio Function
 *
 **************************************************************************************************/
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
        const esp_err_t result = i2s_channel_disable(i2s_rx_chan);
        if (result == ESP_OK) {
            i2s_rx_enabled = false;
        } else {
            ESP_LOGE(
                TAG,
                "Audio cleanup RX disable failed: %s",
                esp_err_to_name(result)
            );
            aggregate = result;
        }
    }
    if (i2s_tx_chan != NULL && i2s_tx_enabled) {
        const esp_err_t result = i2s_channel_disable(i2s_tx_chan);
        if (result == ESP_OK) {
            i2s_tx_enabled = false;
        } else {
            ESP_LOGE(
                TAG,
                "Audio cleanup TX disable failed: %s",
                esp_err_to_name(result)
            );
            if (aggregate == ESP_OK) {
                aggregate = result;
            }
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

    if (aggregate != ESP_OK) {
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

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (audio_resources_poisoned) {
        ESP_LOGE(TAG, "Audio initialization rejected: resource state is poisoned");
        return ESP_ERR_INVALID_STATE;
    }
    if (i2s_tx_chan != NULL && i2s_rx_chan != NULL &&
        i2s_data_if != NULL && i2s_tx_enabled && i2s_rx_enabled) {
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
    esp_err_t primary_error =
        i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan);
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

    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(22050);
    const i2s_std_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        p_i2s_cfg = i2s_config;
    }

    primary_error = i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg);
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_enable(i2s_tx_chan);
        i2s_tx_enabled = primary_error == ESP_OK;
    }
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg);
    }
    if (primary_error == ESP_OK) {
        primary_error = i2s_channel_enable(i2s_rx_chan);
        i2s_rx_enabled = primary_error == ESP_OK;
    }
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
        speaker_codec_device != NULL &&
        microphone_codec_device != NULL &&
        speaker_gpio_interface != NULL &&
        speaker_control_interface != NULL &&
        speaker_codec_interface != NULL &&
        microphone_control_interface != NULL &&
        microphone_codec_interface != NULL &&
        i2s_data_if != NULL &&
        i2s_tx_chan != NULL &&
        i2s_rx_chan != NULL &&
        i2s_tx_enabled &&
        i2s_rx_enabled;
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

#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_LEDC_CH (CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH)
#define LVGL_TICK_PERIOD_MS (CONFIG_BSP_DISPLAY_LVGL_TICK)
#define LVGL_MAX_SLEEP_MS (CONFIG_BSP_DISPLAY_LVGL_MAX_SLEEP)

esp_err_t bsp_display_brightness_init(void)
{
    return bsp_display_brightness_set(100);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness_percent < 0 || brightness_percent > 100)
    {
        ESP_LOGE(TAG, "Invalid brightness percentage. Should be between 0 and 100.");
        return ESP_ERR_INVALID_ARG;
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
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Display brightness command failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }

    brightness = requested_brightness;
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    if (panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel handle is not initialized");
        return -1;
    }

    return brightness * 100 / 255;
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
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    bool spi_bus_initialized = false;
    assert(config != NULL && config->max_transfer_sz > 0);

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
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM,
            &io_config,
            &io_handle
        ),
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
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_init(panel_handle),
        fail,
        TAG,
        "LCD initialization failed"
    );
    ESP_GOTO_ON_ERROR(
        esp_lcd_panel_disp_on_off(panel_handle, true),
        fail,
        TAG,
        "LCD enable failed"
    );

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

esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch)
{
    if (cfg == NULL || ret_touch == NULL) {
        ESP_LOGE(TAG, "Touch configuration or output handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    *ret_touch = NULL;
    if (tp != NULL || touch_io_handle != NULL) {
        ESP_LOGE(TAG, "Touch controller is already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        bsp_i2c_init(),
        TAG,
        "Touch I2C initialization failed"
    );

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = cfg->touch_flags.swap_xy,
            .mirror_x = cfg->touch_flags.mirror_x,
            .mirror_y = cfg->touch_flags.mirror_y,
        },
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
    return ESP_OK;
}

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

static esp_err_t bsp_display_lcd_init(
    const bsp_display_cfg_t *cfg,
    lv_display_t **out_display
)
{
    if (cfg == NULL || out_display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;

    // Feed QSPI from two small internal buffers so LVGL can render the next
    // strip while SPI transfers the current one. Keeping both buffers internal
    // avoids the per-transfer PSRAM bounce allocation that exhausted DMA RAM.
    const size_t transfer_height = 10;

    const bsp_display_config_t disp_config = {
        .max_transfer_sz = BSP_LCD_H_RES * transfer_height * BSP_LCD_BITS_PER_PIXEL / 8,
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
            // The adapter and panel use the same bounded DMA transfer height.
            .buffer_height = transfer_height,
            .use_psram = false,
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

    lv_display_add_event_cb(
        display_handle,
        rounder_event_cb,
        LV_EVENT_INVALIDATE_AREA,
        NULL
    );

    *out_display = display_handle;
    return ESP_OK;
}

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

    const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, tp);

    disp_indev = esp_lv_adapter_register_touch(&touch_cfg);
    if (disp_indev == NULL) {
        ESP_LOGE(TAG, "Registering touch with the LVGL adapter failed");
        return ESP_FAIL;
    }
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

    esp_err_t result = esp_lv_adapter_init(&cfg->lv_adapter_cfg);
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

    return ESP_OK;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

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

// Keep CO5300 scan direction and CST9217 coordinates in the same cardinal frame.
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

// Apply a complete rotation atomically enough that visible pixels and touches agree.
static esp_err_t bsp_display_apply_rotation_config(
    const bsp_display_rotation_config_t *config
)
{
    esp_err_t result = esp_lcd_panel_swap_xy(panel_handle, config->panel_swap_xy);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_lcd_panel_mirror(
        panel_handle, config->panel_mirror_x, config->panel_mirror_y
    );
    if (result != ESP_OK) {
        return result;
    }
    result = esp_lcd_panel_set_gap(
        panel_handle, config->panel_x_gap, config->panel_y_gap
    );
    if (result != ESP_OK) {
        return result;
    }
    result = esp_lcd_touch_set_swap_xy(tp, config->touch_swap_xy);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_lcd_touch_set_mirror_x(tp, config->touch_mirror_x);
    if (result != ESP_OK) {
        return result;
    }
    return esp_lcd_touch_set_mirror_y(tp, config->touch_mirror_y);
}

// Rotate only between touch releases, preserving the logical LVGL coordinate system.
esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation)
{
    if (panel_handle == NULL || tp == NULL || display_handle == NULL) {
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
        return lock_result;
    }
    if (rotation == current_rotation) {
        bsp_display_unlock();
        return ESP_OK;
    }
    if (disp_indev != NULL && lv_indev_get_state(disp_indev) == LV_INDEV_STATE_PRESSED) {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    bsp_display_rotation_config_t previous_config;
    (void)bsp_display_rotation_config(current_rotation, &previous_config);
    const esp_err_t apply_result = bsp_display_apply_rotation_config(&requested_config);
    if (apply_result != ESP_OK) {
        ESP_LOGE(TAG, "Display rotation %d failed (%s)", rotation, esp_err_to_name(apply_result));
        (void)bsp_display_apply_rotation_config(&previous_config);
        bsp_display_unlock();
        return apply_result;
    }

    current_rotation = rotation;
    lv_obj_t *screen = lv_display_get_screen_active(display_handle);
    if (screen != NULL) {
        lv_obj_invalidate(screen);
    }
    bsp_display_unlock();
    ESP_LOGI(TAG, "CO5300 + CST9217 rotation: %d degrees", rotation * 90);
    return ESP_OK;
}

bsp_display_rotation_t bsp_display_rotation_get(void)
{
    return current_rotation;
}

esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    esp_lv_adapter_unlock();
}
