/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_radio_memory.hpp"

#include <cstdlib>
#include <mutex>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

static const char *TAG = "LUNASHELL_radios";
static std::recursive_mutex s_radios_init_mutex;
#if CONFIG_BT_CONTROLLER_ENABLED
// One lifetime owner retains the chunked DMA budget whenever it has not been
// handed to the controller. Standby and failed admission must not free it.
static LUNASHELL::radio_memory::BleControllerDmaReservation s_deferred_ble_dma;
#endif

static void radio_operation_progress(const char *stage)
{
    const esp_err_t result = LUNASHELL_liveness_watch_progress(
        LUNASHELL_LIVENESS_RADIOS
    );
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Radio watchdog progress failed after %s (%s)",
            stage,
            esp_err_to_name(result)
        );
        std::abort();
    }
}

static void log_radio_heap_checkpoint(const char *stage)
{
    constexpr uint32_t internal_caps =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t dma_caps =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    multi_heap_info_t internal = {};
    multi_heap_info_t dma = {};
    multi_heap_info_t psram = {};
    heap_caps_get_info(&internal, internal_caps);
    heap_caps_get_info(&dma, dma_caps);
    heap_caps_get_info(&psram, MALLOC_CAP_SPIRAM);
    ESP_LOGI(
        TAG,
        "Radio heap %s: internal free=%lu min=%lu largest=%lu; "
        "DMA free=%lu min=%lu largest=%lu; PSRAM free=%lu min=%lu largest=%lu",
        stage,
        static_cast<unsigned long>(internal.total_free_bytes),
        static_cast<unsigned long>(internal.minimum_free_bytes),
        static_cast<unsigned long>(internal.largest_free_block),
        static_cast<unsigned long>(dma.total_free_bytes),
        static_cast<unsigned long>(dma.minimum_free_bytes),
        static_cast<unsigned long>(dma.largest_free_block),
        static_cast<unsigned long>(psram.total_free_bytes),
        static_cast<unsigned long>(psram.minimum_free_bytes),
        static_cast<unsigned long>(psram.largest_free_block)
    );
}

#if CONFIG_BT_NIMBLE_ENABLED
static std::mutex s_ble_lifecycle_mutex;
static std::mutex s_ble_state_mutex;
static uint8_t s_ble_address_type;
static bool s_ble_initialization_attempted;
static bool s_ble_initializing;
static bool s_ble_cleanup_poisoned;
static esp_err_t s_ble_initialization_result = ESP_ERR_INVALID_STATE;
static bool s_ble_initialized;
static bool s_ble_host_synced;
static esp_err_t s_ble_runtime_error = ESP_ERR_INVALID_STATE;
static bool s_ble_advertising;
static bool s_ble_connected;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_connection_count;
static uint32_t s_disconnection_count;
static char s_device_name[20];
static TaskHandle_t s_refresh_task_handle;
static TaskHandle_t s_ble_host_task_handle;
static StaticSemaphore_t s_refresh_stop_ack_storage;
static SemaphoreHandle_t s_refresh_stop_ack;
static bool s_refresh_stop_requested;
static bool s_ble_stop_requested;
static bool s_ble_stop_call_in_progress;
static bool s_ble_host_exiting;

static bool has_fresh_data(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}

static bool format_json_string(
    const char *value,
    char *output,
    size_t output_size
)
{
    if (value == nullptr || output == nullptr || output_size < 3) {
        return false;
    }

    size_t output_index = 0;
    output[output_index++] = '"';
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(value);
         *cursor != '\0';
         ++cursor) {
        const unsigned char byte = *cursor;
        const char *escape = nullptr;
        switch (byte) {
        case '"':
            escape = "\\\"";
            break;
        case '\\':
            escape = "\\\\";
            break;
        case '\b':
            escape = "\\b";
            break;
        case '\f':
            escape = "\\f";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\t':
            escape = "\\t";
            break;
        default:
            break;
        }

        if (escape != nullptr) {
            if (output_index + 2 >= output_size) {
                return false;
            }
            output[output_index++] = escape[0];
            output[output_index++] = escape[1];
        } else if (byte < 0x20) {
            if (output_index + 6 >= output_size) {
                return false;
            }
            const int written = snprintf(
                output + output_index,
                output_size - output_index,
                "\\u%04x",
                static_cast<unsigned>(byte)
            );
            if (written != 6) {
                return false;
            }
            output_index += 6;
        } else {
            if (output_index + 1 >= output_size) {
                return false;
            }
            output[output_index++] = static_cast<char>(byte);
        }
    }
    if (output_index + 1 >= output_size) {
        return false;
    }
    output[output_index++] = '"';
    output[output_index] = '\0';
    return true;
}

/* d3f4a100-6b9a-4e51-9f71-515345452e41 */
static const ble_uuid128_t s_LUNASHELL_service_uuid =
    BLE_UUID128_INIT(
        0x41, 0x2e, 0x45, 0x45, 0x53, 0x51, 0x71, 0x9f,
        0x51, 0x4e, 0x9a, 0x6b, 0x00, 0xa1, 0xf4, 0xd3
    );
/* d3f4a101-6b9a-4e51-9f71-515345452e41 */
static const ble_uuid128_t s_telemetry_uuid =
    BLE_UUID128_INIT(
        0x41, 0x2e, 0x45, 0x45, 0x53, 0x51, 0x71, 0x9f,
        0x51, 0x4e, 0x9a, 0x6b, 0x01, 0xa1, 0xf4, 0xd3
    );
/* d3f4a102-6b9a-4e51-9f71-515345452e41 */
static const ble_uuid128_t s_command_uuid =
    BLE_UUID128_INIT(
        0x41, 0x2e, 0x45, 0x45, 0x53, 0x51, 0x71, 0x9f,
        0x51, 0x4e, 0x9a, 0x6b, 0x02, 0xa1, 0xf4, 0xd3
    );

static void publish_ble_snapshot(esp_err_t error = ESP_OK)
{
    bool ready = false;
    bool advertising = false;
    bool connected = false;
    uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
    uint32_t connection_count = 0;
    uint32_t disconnection_count = 0;
    char device_name[sizeof(s_device_name)]{};
    esp_err_t reported_error = error;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        const esp_err_t effective_error =
            error == ESP_OK ? s_ble_runtime_error : error;
        ready = s_ble_initialized &&
                s_ble_host_synced &&
                effective_error == ESP_OK;
        reported_error =
            !ready && effective_error == ESP_OK
                ? ESP_ERR_INVALID_STATE
                : effective_error;
        advertising = s_ble_advertising;
        connected = s_ble_connected;
        connection_handle = s_connection_handle;
        connection_count = s_connection_count;
        disconnection_count = s_disconnection_count;
        snprintf(
            device_name,
            sizeof(device_name),
            "%s",
            s_device_name
        );
    }

    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        ready,
        reported_error,
        advertising,
        connected,
        connection_handle,
        connection_count,
        disconnection_count,
        device_name,
        false
    );
}

static void refresh_snapshot_task(void *argument)
{
    (void)argument;
    while (true) {
        // Clearing the notification count coalesces bursts into one refresh.
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            if (s_refresh_stop_requested) {
                break;
            }
        }
        const esp_err_t error = LUNASHELL_hardware_refresh("ble-command");
        if (error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "BLE-requested hardware refresh failed (%s)",
                esp_err_to_name(error)
            );
        }
    }

    SemaphoreHandle_t stop_ack = nullptr;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_refresh_task_handle != xTaskGetCurrentTaskHandle()) {
            ESP_LOGE(
                TAG,
                "BLE refresh worker ownership changed before cooperative stop"
            );
            std::abort();
        }
        s_refresh_task_handle = nullptr;
        s_refresh_stop_requested = false;
        stop_ack = s_refresh_stop_ack;
    }
    if (stop_ack == nullptr || xSemaphoreGive(stop_ack) != pdTRUE) {
        ESP_LOGE(TAG, "BLE refresh worker stop acknowledgement failed");
        std::abort();
    }
    vTaskDelete(nullptr);
}

static void stop_refresh_snapshot_task()
{
    TaskHandle_t refresh_task = nullptr;
    SemaphoreHandle_t stop_ack = nullptr;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        refresh_task = s_refresh_task_handle;
        stop_ack = s_refresh_stop_ack;
        if (refresh_task == nullptr) {
            return;
        }
        if (stop_ack == nullptr || s_refresh_stop_requested) {
            ESP_LOGE(
                TAG,
                "BLE refresh worker stop contract is already invalid"
            );
            std::abort();
        }
    }
    if (xSemaphoreTake(stop_ack, 0) == pdTRUE) {
        ESP_LOGE(TAG, "BLE refresh worker stop acknowledgement was stale");
        std::abort();
    }
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_refresh_task_handle != refresh_task ||
            s_refresh_stop_ack != stop_ack ||
            s_refresh_stop_requested) {
            ESP_LOGE(
                TAG,
                "BLE refresh worker ownership changed before stop request"
            );
            std::abort();
        }
        s_refresh_stop_requested = true;
    }
    if (xTaskNotifyGive(refresh_task) != pdPASS) {
        ESP_LOGE(TAG, "BLE refresh worker stop notification failed");
        std::abort();
    }
    if (xSemaphoreTake(stop_ack, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "BLE refresh worker did not stop within 10000 ms"
        );
        std::abort();
    }
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_refresh_task_handle != nullptr ||
            s_refresh_stop_requested) {
            ESP_LOGE(
                TAG,
                "BLE refresh worker acknowledged stop with residual ownership"
            );
            std::abort();
        }
    }
}

// connection_handle: Existing NimBLE connection used to bound the response by negotiated MTU.
// attribute_handle: SDK attribute identity; UUID selects the existing telemetry or command route.
// context: SDK-owned GATT operation, offset and output mbuf.
// argument: Unused registered callback context.
// Exception: Logs the original snapshot/serialization error and returns the matching ATT rejection.
static int LUNASHELL_gatt_access(
    uint16_t connection_handle,
    uint16_t attribute_handle,
    struct ble_gatt_access_ctxt *context,
    void *argument
)
{
    (void)attribute_handle;
    (void)argument;
    if (ble_uuid_cmp(context->chr->uuid, &s_telemetry_uuid.u) == 0) {
        if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
        LUNASHELL::BleTelemetryObservation snapshot;
        const esp_err_t snapshot_error = LUNASHELL::DeviceSnapshotService::instance()
            .readBleTelemetryObservation(snapshot);
        if (snapshot_error != ESP_OK) {
            ESP_LOGE(TAG, "BLE telemetry canonical read failed (%s)", esp_err_to_name(snapshot_error));
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        const bool battery_valid =
            has_fresh_data(snapshot.battery) &&
            snapshot.battery_level_percent_valid &&
            snapshot.battery_level_percent >= 0 &&
            snapshot.battery_level_percent <= 100;
        const bool gps_valid = has_fresh_data(snapshot.gps);
        const bool imu_valid = has_fresh_data(snapshot.imu);
        const bool storage_valid = has_fresh_data(snapshot.storage);
        const bool wifi_valid = has_fresh_data(snapshot.wifi);
        const bool memory_valid = has_fresh_data(snapshot.memory);
        char battery[12] = "null";
        char satellites[12] = "null";
        char heap[16] = "null";
        char psram_free[16] = "null";
        char wifi_ssid[200] = "null";
        char wifi_ip[64] = "null";
        if (battery_valid) {
            snprintf(
                battery,
                sizeof(battery),
                "%d",
                snapshot.battery_level_percent
            );
        }
        if (gps_valid && snapshot.gps_has_fix) {
            snprintf(
                satellites,
                sizeof(satellites),
                "%u",
                static_cast<unsigned>(snapshot.gps_satellites)
            );
        }
        if (memory_valid) {
            snprintf(
                heap,
                sizeof(heap),
                "%lu",
                static_cast<unsigned long>(snapshot.heap_free_bytes)
            );
            snprintf(
                psram_free,
                sizeof(psram_free),
                "%lu",
                static_cast<unsigned long>(snapshot.psram_free_bytes)
            );
        }
        if (wifi_valid &&
            (!format_json_string(
                 snapshot.wifi_ssid,
                 wifi_ssid,
                 sizeof(wifi_ssid)
             ) ||
             !format_json_string(
                 snapshot.wifi_ip_address,
                 wifi_ip,
                 sizeof(wifi_ip)
             ))) {
            ESP_LOGE(
                TAG,
                "BLE telemetry rejected: valid Wi-Fi strings could not be "
                "serialized without loss"
            );
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        char json[640];
        const int length = snprintf(
            json,
            sizeof(json),
            "{\"seq\":%lu,\"battery\":%s,\"usb\":%s,\"wifi\":%s,"
            "\"ip\":%s,\"gps_fix\":%s,\"sat\":%s,\"imu\":%s,"
            "\"sd\":%s,\"heap\":%s,\"psram_free\":%s}",
            static_cast<unsigned long>(snapshot.sequence),
            battery,
            battery_valid
                ? (snapshot.usb_present ? "true" : "false")
                : "null",
            wifi_ssid,
            wifi_ip,
            gps_valid
                ? (snapshot.gps_has_fix ? "true" : "false")
                : "null",
            satellites,
            imu_valid
                ? (snapshot.imu_calibrated ? "true" : "false")
                : "null",
            storage_valid
                ? (snapshot.storage_card_response_verified ? "true" : "false")
                : "null",
            heap,
            psram_free
        );
        if (length < 0 || static_cast<size_t>(length) >= sizeof(json)) {
            ESP_LOGE(TAG, "BLE telemetry serialization overflow");
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (context->offset > static_cast<uint16_t>(length)) {
            return BLE_ATT_ERR_INVALID_OFFSET;
        }
        const size_t remaining =
            static_cast<size_t>(length) - context->offset;
        const uint16_t mtu = ble_att_mtu(connection_handle);
        const size_t chunk =
            remaining < static_cast<size_t>(mtu - 1) ? remaining : mtu - 1;
        const int result =
            os_mbuf_append(context->om, json + context->offset, chunk);
        return result == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ble_uuid_cmp(context->chr->uuid, &s_command_uuid.u) == 0) {
        if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        char command[17] = {};
        uint16_t length = 0;
        const int result = ble_hs_mbuf_to_flat(
            context->om,
            command,
            sizeof(command) - 1,
            &length
        );
        if (result != 0 || length == 0 || length >= sizeof(command)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        command[length] = '\0';
        if (strcmp(command, "refresh") != 0) {
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            if (s_refresh_task_handle == nullptr ||
                s_refresh_stop_requested ||
                xTaskNotifyGive(s_refresh_task_handle) != pdPASS) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
        }
        ESP_LOGI(TAG, "BLE GATT: canonical snapshot refresh requested");
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def s_LUNASHELL_characteristics[] = {
    {&s_telemetry_uuid.u, LUNASHELL_gatt_access, nullptr, nullptr,
     BLE_GATT_CHR_F_READ, 0, nullptr, nullptr},
    {&s_command_uuid.u, LUNASHELL_gatt_access, nullptr, nullptr,
     BLE_GATT_CHR_F_WRITE, 0, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};

static const struct ble_gatt_svc_def s_LUNASHELL_services[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &s_LUNASHELL_service_uuid.u, nullptr,
     s_LUNASHELL_characteristics},
    {0, nullptr, nullptr, nullptr},
};

static esp_err_t start_ble_advertising(void);

// event: NimBLE GAP event that may request an advertising restart.
// arg: Unused callback context.
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE: connection %s", event->connect.status == 0 ? "established" : "failed");
        if (event->connect.status == 0) {
            {
                std::lock_guard<std::mutex> guard(s_ble_state_mutex);
                s_ble_connected = true;
                s_ble_advertising = false;
                s_connection_handle = event->connect.conn_handle;
                ++s_connection_count;
            }
            publish_ble_snapshot();
        } else {
            bool restart_advertising = false;
            {
                std::lock_guard<std::mutex> guard(s_ble_state_mutex);
                s_ble_advertising = false;
                restart_advertising = !s_ble_stop_requested;
            }
            if (restart_advertising) {
                const esp_err_t advertising_error = start_ble_advertising();
                if (advertising_error != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "BLE: advertising recovery after failed connection "
                        "failed (%s)",
                        esp_err_to_name(advertising_error)
                    );
                }
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        {
            bool restart_advertising = false;
            {
                std::lock_guard<std::mutex> guard(s_ble_state_mutex);
                s_ble_connected = false;
                s_ble_advertising = false;
                s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
                ++s_disconnection_count;
                restart_advertising = !s_ble_stop_requested;
            }
            publish_ble_snapshot();
            if (!restart_advertising) {
                break;
            }
            ESP_LOGI(TAG, "BLE: disconnected, restarting advertising");
            const esp_err_t advertising_error = start_ble_advertising();
            if (advertising_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "BLE: advertising recovery after disconnect "
                    "failed (%s)",
                    esp_err_to_name(advertising_error)
                );
            }
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        {
            bool restart_advertising = false;
            {
                std::lock_guard<std::mutex> guard(s_ble_state_mutex);
                s_ble_advertising = false;
                restart_advertising = !s_ble_stop_requested;
            }
            if (restart_advertising) {
                const esp_err_t advertising_error = start_ble_advertising();
                if (advertising_error != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "BLE: advertising restart failed (%s)",
                        esp_err_to_name(advertising_error)
                    );
                }
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

// No parameters: begin advertising only while the BLE host is active.
// Exception: Returns an explicit stop rejection or the NimBLE setup/start error.
static esp_err_t start_ble_advertising(void)
{
    uint8_t address_type = 0;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_ble_stop_requested || s_ble_host_exiting) {
            return ESP_ERR_INVALID_STATE;
        }
        address_type = s_ble_address_type;
    }

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    const char *name = ble_svc_gap_device_name();
    if (name == nullptr) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_advertising = false;
            s_ble_runtime_error = ESP_ERR_INVALID_STATE;
        }
        publish_ble_snapshot(ESP_ERR_INVALID_STATE);
        ESP_LOGE(TAG, "BLE: advertising rejected without a device name");
        return ESP_ERR_INVALID_STATE;
    }
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_advertising = false;
            s_ble_runtime_error = ESP_FAIL;
        }
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGE(TAG, "BLE: advertising data failed (%d)", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(
        address_type,
        nullptr,
        BLE_HS_FOREVER,
        &params,
        ble_gap_event,
        nullptr
    );
    if (rc == 0) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_advertising = true;
            s_ble_runtime_error = ESP_OK;
        }
        publish_ble_snapshot();
        ESP_LOGI(TAG, "BLE: advertising as %s", name);
        return ESP_OK;
    } else {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_advertising = false;
            s_ble_runtime_error = ESP_FAIL;
        }
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGE(TAG, "BLE: advertising start failed (%d)", rc);
        return ESP_FAIL;
    }
}

static void ble_on_reset(int reason)
{
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_host_synced = false;
        s_ble_advertising = false;
        s_ble_connected = false;
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble_runtime_error = ESP_FAIL;
    }
    publish_ble_snapshot(ESP_FAIL);
    ESP_LOGE(TAG, "BLE: host reset (%d)", reason);
}

static void ble_on_sync(void)
{
    uint8_t address_type = 0;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &address_type);
    }
    if (rc != 0) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_host_synced = false;
            s_ble_runtime_error = ESP_FAIL;
        }
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGE(TAG, "BLE: no usable address (%d)", rc);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_address_type = address_type;
        s_ble_host_synced = true;
        s_ble_runtime_error = ESP_ERR_INVALID_STATE;
    }
    const esp_err_t advertising_error = start_ble_advertising();
    if (advertising_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "BLE: host synchronized but advertising failed (%s)",
            esp_err_to_name(advertising_error)
        );
    }
}

// arg: Unused FreeRTOS task context.
static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();

    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_host_exiting = true;
    }
    stop_refresh_snapshot_task();
    // NimBLE's dynamic stop event belongs to nimble_port_stop until it returns.
    // Deinitializing here earlier frees that event while the caller still uses it.
    while (true) {
        bool stop_call_in_progress = false;
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            stop_call_in_progress = s_ble_stop_call_in_progress;
        }
        if (!stop_call_in_progress) {
            break;
        }
        vTaskDelay(1);
    }
    const esp_err_t cleanup_error = nimble_port_deinit();
    bool stop_requested = false;
    esp_err_t runtime_error = cleanup_error;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        stop_requested = s_ble_stop_requested;
        runtime_error = cleanup_error == ESP_OK
                            ? ESP_ERR_INVALID_STATE
                            : cleanup_error;
        s_ble_initializing = false;
        s_ble_initialized = false;
        s_ble_host_synced = false;
        s_ble_advertising = false;
        s_ble_connected = false;
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble_initialization_result = runtime_error;
        s_ble_runtime_error = runtime_error;
        s_ble_cleanup_poisoned = cleanup_error != ESP_OK;
        if (stop_requested && cleanup_error == ESP_OK) {
            s_ble_initialization_attempted = false;
        }
        s_ble_stop_requested = false;
    }
    publish_ble_snapshot(runtime_error);
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        // The stop waiter must not admit a new host until the retired host has
        // published its final state; otherwise this can overwrite wake's Ready.
        s_ble_host_task_handle = nullptr;
        s_ble_host_exiting = false;
    }
    if (stop_requested && cleanup_error == ESP_OK) {
        ESP_LOGI(TAG, "BLE telemetry lifecycle stopped by explicit user action");
    } else {
        ESP_LOGE(
            TAG,
            "BLE host task stopped %s; NimBLE cleanup %s (%s)",
            stop_requested ? "with a cleanup failure" : "unexpectedly",
            cleanup_error == ESP_OK ? "completed" : "failed",
            esp_err_to_name(cleanup_error)
        );
    }
    vTaskDelete(nullptr);
}

static bool ble_telemetry_owns_radio()
{
    std::lock_guard<std::mutex> guard(s_ble_state_mutex);
    return s_ble_initializing ||
           s_ble_initialized ||
           s_ble_host_task_handle != nullptr ||
           s_ble_cleanup_poisoned;
}

static esp_err_t fail_ble_initialization_after_port(
    const char *stage,
    esp_err_t primary_error
)
{
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_host_task_handle = nullptr;
    }
    stop_refresh_snapshot_task();

    const esp_err_t cleanup_error = nimble_port_deinit();
    const esp_err_t returned_error =
        cleanup_error == ESP_OK ? primary_error : cleanup_error;
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_initializing = false;
        s_ble_cleanup_poisoned = cleanup_error != ESP_OK;
        s_ble_initialization_result = returned_error;
        s_ble_initialized = false;
        s_ble_host_synced = false;
        s_ble_advertising = false;
        s_ble_connected = false;
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble_runtime_error = returned_error;
    }
    publish_ble_snapshot(returned_error);

    if (cleanup_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "BLE: %s failed (%s), then NimBLE cleanup failed (%s); "
            "BLE ownership is poisoned for this boot",
            stage,
            esp_err_to_name(primary_error),
            esp_err_to_name(cleanup_error)
        );
    } else {
        ESP_LOGE(
            TAG,
            "BLE: %s failed (%s); NimBLE cleanup completed",
            stage,
            esp_err_to_name(primary_error)
        );
    }
    return returned_error;
}

// No parameters: stop the NimBLE host before allowing its dynamic context to be freed.
// Exception: Returns the original stop, cleanup, or timeout error.
static esp_err_t stop_ble_telemetry()
{
    std::lock_guard<std::mutex> lifecycle_guard(s_ble_lifecycle_mutex);
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_ble_cleanup_poisoned) {
            return s_ble_runtime_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : s_ble_runtime_error;
        }
        if (s_ble_initializing) {
            ESP_LOGE(TAG, "BLE stop rejected while initialization is active");
            return ESP_ERR_INVALID_STATE;
        }
        if (s_ble_host_exiting) {
            ESP_LOGE(TAG, "BLE stop rejected while host cleanup is active");
            return s_ble_runtime_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : s_ble_runtime_error;
        }
        if (!s_ble_initialized && s_ble_host_task_handle == nullptr) {
            return ESP_OK;
        }
        s_ble_stop_requested = true;
        s_ble_stop_call_in_progress = true;
    }

    const int stop_result = nimble_port_stop();
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_stop_call_in_progress = false;
    }
    if (stop_result != 0) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_stop_requested = false;
            s_ble_runtime_error = ESP_FAIL;
        }
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGE(
            TAG,
            "BLE telemetry stop request failed (NimBLE rc=%d)",
            stop_result
        );
        return ESP_FAIL;
    }

    constexpr uint32_t kStopWaitMs = 3000U;
    constexpr uint32_t kStopPollMs = 10U;
    for (uint32_t waited_ms = 0;
         waited_ms < kStopWaitMs;
         waited_ms += kStopPollMs) {
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            if (s_ble_host_task_handle == nullptr) {
                return s_ble_cleanup_poisoned
                           ? s_ble_runtime_error
                           : ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kStopPollMs));
    }

    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_runtime_error = ESP_ERR_TIMEOUT;
    }
    publish_ble_snapshot(ESP_ERR_TIMEOUT);
    ESP_LOGE(TAG, "BLE telemetry stop timed out after %lu ms",
             static_cast<unsigned long>(kStopWaitMs));
    return ESP_ERR_TIMEOUT;
}

// No parameters: initialize NimBLE telemetry for the current radio lifecycle.
// Exception: Returns the original initialization or cleanup error.
extern "C" esp_err_t LUNASHELL_ble_init(void)
{
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
    std::lock_guard<std::mutex> lifecycle_guard(s_ble_lifecycle_mutex);

    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        if (s_ble_initialization_attempted) {
            return s_ble_initialized
                       ? ESP_OK
                       : s_ble_initialization_result;
        }
    }

    // The provisioning manager and telemetry host must never share ownership.
    if (LUNASHELL_provisioning_initialized()) {
        ESP_LOGE(
            TAG,
            "BLE telemetry init rejected while provisioning owns BLE; "
            "run the explicit provisioning deinit handoff first"
        );
        return ESP_ERR_INVALID_STATE;
    }

    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_initialization_attempted = true;
        s_ble_initializing = true;
        s_ble_cleanup_poisoned = false;
        s_ble_host_exiting = false;
        s_ble_stop_call_in_progress = false;
        s_ble_initialization_result = ESP_ERR_INVALID_STATE;
    }

    uint8_t mac[6] = {};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BT);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "BLE: reading controller MAC failed (%s)",
            esp_err_to_name(err)
        );
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_initializing = false;
            s_ble_initialization_result = err;
            s_ble_runtime_error = err;
        }
        LUNASHELL::DeviceSnapshotService::instance().reportBle(false, err);
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BLE: NimBLE init failed (%s)", esp_err_to_name(err));
        {
            std::lock_guard<std::mutex> guard(s_ble_state_mutex);
            s_ble_initializing = false;
            s_ble_initialization_result = err;
            s_ble_runtime_error = err;
        }
        LUNASHELL::DeviceSnapshotService::instance().reportBle(false, err);
        return err;
    }

    char device_name[sizeof(s_device_name)]{};
    snprintf(
        device_name,
        sizeof(device_name),
        "CODY-%02X%02X",
        mac[4],
        mac[5]
    );

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_dis_init();
    int rc = ble_svc_dis_manufacturer_name_set("Device firmware");
    if (rc == 0) {
        rc = ble_svc_dis_model_number_set(
            "Waveshare ESP32-S3 AMOLED 1.75"
        );
    }
    if (rc == 0) {
        rc = ble_svc_dis_hardware_revision_set("ESP32-S3 AMOLED GPS");
    }
    if (rc == 0) {
        rc = ble_svc_dis_firmware_revision_set(
            esp_app_get_description()->version
        );
    }
    if (rc != 0) {
        ESP_LOGE(
            TAG,
            "BLE: device information setup failed (%d)",
            rc
        );
        return fail_ble_initialization_after_port(
            "device information setup",
            ESP_FAIL
        );
    }

    rc = ble_gatts_count_cfg(s_LUNASHELL_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_LUNASHELL_services);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE: GATT service registration failed (%d)", rc);
        return fail_ble_initialization_after_port(
            "GATT service registration",
            ESP_FAIL
        );
    }
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE: device name rejected (%d)", rc);
        return fail_ble_initialization_after_port(
            "device name setup",
            ESP_FAIL
        );
    }

    if (s_refresh_stop_ack == nullptr) {
        s_refresh_stop_ack =
            xSemaphoreCreateBinaryStatic(&s_refresh_stop_ack_storage);
    }
    if (s_refresh_stop_ack == nullptr) {
        return fail_ble_initialization_after_port(
            "refresh worker stop acknowledgement creation",
            ESP_ERR_NO_MEM
        );
    }
    if (xSemaphoreTake(s_refresh_stop_ack, 0) == pdTRUE) {
        return fail_ble_initialization_after_port(
            "refresh worker stop acknowledgement initialization",
            ESP_ERR_INVALID_STATE
        );
    }
    TaskHandle_t refresh_task = nullptr;
    if (xTaskCreate(
            refresh_snapshot_task,
            "LUNASHELL_ble_refresh",
            4096,
            nullptr,
            4,
            &refresh_task
        ) != pdPASS) {
        return fail_ble_initialization_after_port(
            "refresh worker creation",
            ESP_ERR_NO_MEM
        );
    }

    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_initializing = false;
        s_ble_initialized = true;
        s_ble_initialization_result = ESP_OK;
        s_ble_host_synced = false;
        s_ble_runtime_error = ESP_ERR_INVALID_STATE;
        s_refresh_task_handle = refresh_task;
        snprintf(
            s_device_name,
            sizeof(s_device_name),
            "%s",
            device_name
        );
    }
    publish_ble_snapshot(ESP_ERR_INVALID_STATE);
    TaskHandle_t host_task = nullptr;
    if (xTaskCreatePinnedToCore(
            ble_host_task,
            "nimble_host",
            NIMBLE_HS_STACK_SIZE,
            nullptr,
            configMAX_PRIORITIES - 4,
            &host_task,
            NIMBLE_CORE
        ) != pdPASS) {
        return fail_ble_initialization_after_port(
            "host worker creation",
            ESP_ERR_NO_MEM
        );
    }
    {
        std::lock_guard<std::mutex> guard(s_ble_state_mutex);
        s_ble_host_task_handle = host_task;
    }
    ESP_LOGI(TAG, "BLE: NimBLE host started; waiting for controller sync");
    return ESP_OK;
}
#else
extern "C" esp_err_t LUNASHELL_ble_init(void)
{
    ESP_LOGW(TAG, "BLE: disabled by sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

// No parameters: initialize radios while retaining unused DMA across every exit.
// Exception: Returns the original Wi-Fi, BLE admission, or lifecycle error.
static esp_err_t radios_init_impl(void)
{
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);

#if CONFIG_BT_CONTROLLER_ENABLED
    auto &ble_controller_dma = s_deferred_ble_dma;
    const esp_err_t reservation_error =
        ble_controller_dma ? ESP_OK :
            LUNASHELL::radio_memory::reserveBleControllerDma(ble_controller_dma);
    if (reservation_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            false,
            reservation_error,
            false,
            false,
            0xFFFF,
            0,
            0,
            nullptr,
            true
        );
        return reservation_error;
    }
#endif

    log_radio_heap_checkpoint("before-Wi-Fi");
    const esp_err_t wifi = LUNASHELL_wifi_init();
    radio_operation_progress("Wi-Fi initialization");
    log_radio_heap_checkpoint(
        wifi == ESP_OK ? "after-Wi-Fi" : "after-Wi-Fi-failure"
    );
    LUNASHELL_wifi_status_t wifi_status = {};
    LUNASHELL_wifi_get_status(&wifi_status);

    if (wifi != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            false,
            ESP_ERR_INVALID_STATE,
            false,
            false,
            0xFFFF,
            0,
            0,
            nullptr,
            true
        );
        ESP_LOGE(
            TAG,
            "Radio startup stopped after Wi-Fi initialization failed (%s)",
            esp_err_to_name(wifi)
        );
        return wifi;
    }

    esp_err_t ble = ESP_OK;
#if !CONFIG_LUNASHELL_PROVISIONING_START_ON_BOOT
    if (!wifi_status.credentials_configured) {
        ESP_LOGI(TAG, "Wi-Fi ready; BLE pairing awaits an explicit user action");
        return ESP_OK;
    }
#endif
    if (wifi_status.credentials_configured) {
        if (LUNASHELL_provisioning_initialized()) {
            ESP_LOGI(
                TAG,
                "Radio handoff: deinitializing provisioning before "
                "starting BLE telemetry"
            );
            ble = LUNASHELL_provisioning_deinit_for_ble();
            radio_operation_progress("provisioning handoff");
            if (ble != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Radio handoff failed; NimBLE telemetry was not started "
                    "(%s)",
                    esp_err_to_name(ble)
                );
                return ble;
            }
        }
#if CONFIG_BT_CONTROLLER_ENABLED
        LUNASHELL::radio_memory::releaseBleControllerDma(
            ble_controller_dma,
            "BLE telemetry"
        );
        ble = LUNASHELL::radio_memory::validateBleControllerDma("BLE telemetry");
#endif
        if (ble == ESP_OK) {
            ble = LUNASHELL_ble_init();
            radio_operation_progress("BLE telemetry initialization");
        }
    } else {
#if CONFIG_BT_NIMBLE_ENABLED
        if (ble_telemetry_owns_radio()) {
            ESP_LOGE(
                TAG,
                "BLE provisioning rejected because telemetry already owns "
                "BLE; automatic reverse handoff is unsupported"
            );
            return ESP_ERR_INVALID_STATE;
        }
#endif
        ble = LUNASHELL_provisioning_init();
        radio_operation_progress("provisioning initialization");
        if (ble == ESP_OK) {
#if CONFIG_BT_CONTROLLER_ENABLED
            LUNASHELL::radio_memory::releaseBleControllerDma(
                ble_controller_dma,
                "BLE provisioning"
            );
#endif
        }
        if (ble == ESP_OK) {
            ble = LUNASHELL_provisioning_start();
            radio_operation_progress("provisioning start");
        }
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            ble == ESP_OK,
            ble,
            ble == ESP_OK,
            false,
            0xFFFF,
            0,
            0,
            LUNASHELL_provisioning_service_name(),
            true
        );
    }
    log_radio_heap_checkpoint(
        ble == ESP_OK ? "after-BLE" : "after-BLE-failure"
    );

    if (ble != ESP_OK) {
        if (wifi_status.credentials_configured) {
            LUNASHELL::DeviceSnapshotService::instance().reportBle(false, ble);
        }
        ESP_LOGE(
            TAG,
            "BLE %s unavailable: %s",
            wifi_status.credentials_configured ? "telemetry" : "provisioning",
            esp_err_to_name(ble)
        );
    }
    if (ble != ESP_OK) {
        return ble;
    }
    if (!wifi_status.credentials_configured) {
        ESP_LOGI(
            TAG,
            "Radio inventory: Wi-Fi ready; secure BLE provisioning active as %s",
            LUNASHELL_provisioning_service_name()
        );
    } else {
        ESP_LOGI(
            TAG,
            "Radio inventory: Wi-Fi ready; BLE telemetry host started, "
            "awaiting controller sync"
        );
    }
    return ESP_OK;
}

// No parameters: stop radio hardware without releasing an unused controller lease.
// Exception: Returns the actual BLE or Wi-Fi teardown error.
static esp_err_t radios_stop_impl(void)
{
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
    esp_err_t ble_result = ESP_OK;
    if (LUNASHELL_provisioning_initialized()) {
        if (LUNASHELL_provisioning_active()) {
            ble_result = LUNASHELL_provisioning_stop();
            radio_operation_progress("provisioning stop");
        }
        if (ble_result == ESP_OK) {
            ble_result = LUNASHELL_provisioning_deinit_for_ble();
            radio_operation_progress("provisioning deinitialize");
        }
    } else {
#if CONFIG_BT_NIMBLE_ENABLED
        ble_result = stop_ble_telemetry();
        radio_operation_progress("BLE telemetry stop");
#endif
    }
    if (ble_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Radio shutdown stopped after BLE cleanup failed (%s)",
            esp_err_to_name(ble_result)
        );
        return ble_result;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    const esp_err_t wifi_result = LUNASHELL_wifi_stop();
    radio_operation_progress("Wi-Fi stop");
    if (wifi_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Radio shutdown stopped after Wi-Fi cleanup failed (%s)",
            esp_err_to_name(wifi_result)
        );
        return wifi_result;
    }
#endif

    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_ERR_INVALID_STATE,
        false,
        false,
        0xFFFF,
        0,
        0,
        nullptr,
        false
    );
    ESP_LOGI(TAG, "Wireless services stopped");
#if CONFIG_BT_CONTROLLER_ENABLED
    if (s_deferred_ble_dma) {
        ESP_LOGI(TAG, "BLE DMA reservation retained while radios are stopped: %lu bytes",
                 static_cast<unsigned long>(LUNASHELL::radio_memory::kBleControllerDmaHoldBytes));
    }
#endif
    return ESP_OK;
}

static esp_err_t run_watched_radio_operation(
    const char *operation_name,
    esp_err_t (*operation)(void)
)
{
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_RADIOS
    );
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Radio %s watchdog start failed (%s)",
            operation_name,
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }

    const esp_err_t operation_result = operation();
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_RADIOS,
        operation_result
    );
}

// No parameters: hold the controller startup budget before SD and display startup.
esp_err_t LUNASHELL_radios_reserve_startup_dma(void)
{
#if CONFIG_BT_CONTROLLER_ENABLED
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        ESP_LOGE(TAG, "Early BLE DMA reservation rejected: controller already active (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_deferred_ble_dma) {
        return ESP_OK;
    }

    log_radio_heap_checkpoint("before early BLE DMA reservation");
    const esp_err_t result =
        LUNASHELL::radio_memory::reserveBleControllerDma(s_deferred_ble_dma);
    if (result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(false, result);
        return result;
    }
    log_radio_heap_checkpoint("after early BLE DMA reservation");
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Early BLE DMA reservation unavailable: controller disabled (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// No parameters: start both radios with the early DMA reservation when available.
esp_err_t LUNASHELL_radios_init(void)
{
    return run_watched_radio_operation("startup", radios_init_impl);
}

// stage: Short admission boundary; keep each record below the logger's line limit.
static void log_pairing_dma_checkpoint(const char *stage)
{
    constexpr uint32_t capabilities = LUNASHELL::radio_memory::kBleControllerDmaCaps;
    ESP_LOGI(TAG, "Pairing DMA %s: free=%lu largest=%lu hold=%lu", stage,
        static_cast<unsigned long>(heap_caps_get_free_size(capabilities)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(capabilities)),
        static_cast<unsigned long>(LUNASHELL::radio_memory::kBleControllerDmaHoldBytes));
}

// No parameters: transfer telemetry to provisioning under the radio lifecycle mutex.
// Exception: Return the original admission/start error; the caller owns display parking.
static esp_err_t start_pairing_radios_locked()
{
#if CONFIG_BT_NIMBLE_ENABLED
    const esp_err_t stop_error = stop_ble_telemetry();
    if (stop_error != ESP_OK) {
        return stop_error;
    }
    radio_operation_progress("telemetry-to-provisioning handoff");
#endif
    log_pairing_dma_checkpoint("after telemetry stop");
#if CONFIG_BT_CONTROLLER_ENABLED
    auto &reservation = s_deferred_ble_dma;
    const esp_err_t reservation_error = reservation ? ESP_OK :
        LUNASHELL::radio_memory::reserveBleControllerDma(reservation);
    if (reservation_error != ESP_OK) {
        return reservation_error;
    }
    log_pairing_dma_checkpoint("reservation held");
#endif
    esp_err_t error = LUNASHELL_wifi_init();
    radio_operation_progress("pairing Wi-Fi initialization");
    if (error == ESP_OK) {
        error = LUNASHELL_provisioning_init();
        radio_operation_progress("pairing manager initialization");
    }
#if CONFIG_BT_CONTROLLER_ENABLED
    if (error != ESP_OK) {
        // The lifetime owner keeps the same block; unwinding this operation
        // must not release memory needed by the next explicit attempt.
        return error;
    }
    LUNASHELL::radio_memory::releaseBleControllerDma(reservation, "explicit pairing");
#endif
    log_pairing_dma_checkpoint("before controller start");
    if (error == ESP_OK) {
        error = LUNASHELL_provisioning_start();
        radio_operation_progress("explicit pairing start");
    }
    log_pairing_dma_checkpoint("after controller start");
    return error;
}

// No parameters: drain frame DMA before freeing or acquiring the BLE controller lease.
// Exception: Preserve the radio error if display restoration also fails.
static esp_err_t radios_start_pairing_impl()
{
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
    if (LUNASHELL_provisioning_initialized()) {
        ESP_LOGE(TAG, "Pairing start rejected: manager still owns BLE (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    log_pairing_dma_checkpoint("before display drain");
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    // Boot owns its own gate around radios_init. Only this explicit handoff
    // closes it here; the independent standby write gate remains untouched.
    const esp_err_t park_error = LUNASHELL_display_runtime_set_startup_quiescent(true);
    if (park_error != ESP_OK) {
        ESP_LOGE(TAG, "Pairing display DMA drain failed (%s)", esp_err_to_name(park_error));
        return park_error;
    }
    radio_operation_progress("pairing display DMA drained");
    log_pairing_dma_checkpoint("after display drain");
#endif
    const esp_err_t radio_error = start_pairing_radios_locked();
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    // Restore on every normal failure path, including admission before manager init.
    const esp_err_t resume_error = LUNASHELL_display_runtime_set_startup_quiescent(false);
    if (resume_error != ESP_OK) {
        ESP_LOGE(TAG, "Pairing display DMA resume failed (%s); radio result=%s",
                 esp_err_to_name(resume_error), esp_err_to_name(radio_error));
        if (radio_error == ESP_OK) {
            return resume_error;
        }
    }
#endif
    if (radio_error != ESP_OK) {
        ESP_LOGE(TAG, "Explicit pairing start failed (%s)", esp_err_to_name(radio_error));
    }
    return radio_error;
}

// No parameters: execute explicit pairing with the existing radio watchdog owner.
esp_err_t LUNASHELL_radios_start_pairing(void)
{
    return run_watched_radio_operation("pairing start", radios_start_pairing_impl);
}

// restore_telemetry: Explicit choice to restore BLE after a verified Wi-Fi lease.
// Exception: Returns actual manager cleanup and requested telemetry handoff errors.
static esp_err_t radios_end_pairing_impl(bool restore_telemetry)
{
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
    esp_err_t error = ESP_OK;
    if (LUNASHELL_provisioning_initialized()) {
        if (LUNASHELL_provisioning_active()) {
            error = LUNASHELL_provisioning_stop();
        }
        if (error == ESP_OK) {
            error = LUNASHELL_provisioning_deinit_for_ble();
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Pairing cleanup failed (%s)", esp_err_to_name(error));
        return error;
    }
    if (!restore_telemetry) {
        // Manager closure is complete. The shared cleanup wrapper restores
        // the lifetime DMA budget and returns its outcome separately.
        return ESP_OK;
    }
    LUNASHELL_wifi_status_t wifi{};
    LUNASHELL_wifi_get_status(&wifi);
    if (!wifi.ip_acquired || wifi.last_error != ESP_OK) {
        error = wifi.last_error == ESP_OK ? ESP_ERR_INVALID_STATE : wifi.last_error;
        ESP_LOGE(TAG, "Telemetry handoff rejected without verified Wi-Fi (%s)",
                 esp_err_to_name(error));
        return error;
    }
#if CONFIG_BT_CONTROLLER_ENABLED
    LUNASHELL::radio_memory::releaseBleControllerDma(s_deferred_ble_dma, "paired telemetry");
#endif
    return LUNASHELL_ble_init();
}

// stop_wifi: Close both radios instead of only the provisioning session.
// restore_telemetry: Transfer a successful provisioning session to telemetry.
// Exception: Keep hardware closure and DMA rearm failures separate for callers.
static LUNASHELL_radio_cleanup_result_t run_radio_cleanup(
    bool stop_wifi,
    bool restore_telemetry
)
{
    LUNASHELL_radio_cleanup_result_t result{ESP_OK, ESP_OK};
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(LUNASHELL_LIVENESS_RADIOS);
    if (watch_error != ESP_OK) {
        ESP_LOGE(TAG, "Radio cleanup watchdog start failed (%s)",
                 esp_err_to_name(watch_error));
        result.cleanup_error = watch_error;
        return result;
    }
    std::lock_guard<std::recursive_mutex> radios_guard(s_radios_init_mutex);
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    // Keep frame allocations away from the block as the SDK returns it.
    const esp_err_t park_error = LUNASHELL_display_runtime_set_startup_quiescent(true);
    const bool display_parked = park_error == ESP_OK;
    result.cleanup_error = park_error;
#endif
    if (result.cleanup_error == ESP_OK) {
        result.cleanup_error = stop_wifi ? radios_stop_impl() :
            radios_end_pairing_impl(restore_telemetry);
    }
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_BT_CONTROLLER_ENABLED
    if (result.cleanup_error == ESP_OK) {
        const bool controller_idle =
            esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
        if (controller_idle) {
            result.dma_reservation_error =
                LUNASHELL::radio_memory::reserveBleControllerDma(s_deferred_ble_dma);
            if (result.dma_reservation_error == ESP_OK && !s_deferred_ble_dma) {
                result.dma_reservation_error = ESP_ERR_INVALID_STATE;
            }
            if (result.dma_reservation_error == ESP_OK) {
                ESP_LOGI(TAG, "BLE lifetime DMA reserve ready after cleanup: %lu bytes",
                    static_cast<unsigned long>(LUNASHELL::radio_memory::kBleControllerDmaHoldBytes));
            } else {
                // The radios are actually closed. Publish the failed future
                // budget without turning clean Cancel into a cleanup failure.
                LUNASHELL::DeviceSnapshotService::instance().reportBle(
                    false, result.dma_reservation_error);
                ESP_LOGE(TAG, "Radios closed; BLE DMA rearm failed (%s)",
                    esp_err_to_name(result.dma_reservation_error));
            }
        } else if (stop_wifi || !restore_telemetry) {
            result.cleanup_error = ESP_ERR_INVALID_STATE;
            LUNASHELL::DeviceSnapshotService::instance().reportBle(false, result.cleanup_error);
            ESP_LOGE(TAG, "Radio cleanup left the BLE controller active (%s)",
                esp_err_to_name(result.cleanup_error));
        }
    }
#endif
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    if (display_parked) {
        const esp_err_t resume_error = LUNASHELL_display_runtime_set_startup_quiescent(false);
        if (resume_error != ESP_OK) {
            ESP_LOGE(TAG, "Radio cleanup display resume failed (%s); cleanup=%s DMA=%s",
                esp_err_to_name(resume_error), esp_err_to_name(result.cleanup_error),
                esp_err_to_name(result.dma_reservation_error));
            if (result.cleanup_error == ESP_OK) {
                result.cleanup_error = resume_error;
            }
        }
    }
#endif
    const esp_err_t operation_error = result.cleanup_error != ESP_OK ?
        result.cleanup_error : result.dma_reservation_error;
    const esp_err_t watched_error =
        LUNASHELL_liveness_watch_end(LUNASHELL_LIVENESS_RADIOS, operation_error);
    if (operation_error == ESP_OK && watched_error != ESP_OK) {
        result.cleanup_error = watched_error;
    }
    return result;
}

// No parameters: close the radio hardware and retain or rearm its lifetime DMA budget.
// Exception: Returns both real teardown and reservation errors without conflating them.
LUNASHELL_radio_cleanup_result_t LUNASHELL_radios_stop(void)
{
    return run_radio_cleanup(true, false);
}

// restore_telemetry: Explicit choice to restore BLE on verified Wi-Fi.
// Exception: A budget rearm failure does not imply the provisioning manager stayed open.
LUNASHELL_radio_cleanup_result_t LUNASHELL_radios_end_pairing(bool restore_telemetry)
{
    return run_radio_cleanup(false, restore_telemetry);
}
