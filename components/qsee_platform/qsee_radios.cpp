/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

// Radio-state helpers for Wi-Fi and Bluetooth status reporting.
// Feeds the UI and snapshot layer with current connectivity state.

#include "qsee_platform.h"
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

static const char *TAG = "qsee_radios";
static constexpr char kBleEnabledSetting[] = "ble_on";
static bool s_ble_enabled;
static bool s_ble_preference_loaded;
static bool s_ble_provisioning_reserved;

#if CONFIG_BT_NIMBLE_ENABLED
static uint8_t s_ble_address_type;
static bool s_ble_initialized;
static bool s_ble_advertising;
static bool s_ble_connected;
static bool s_ble_sleep_suspended;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint32_t s_connection_count;
static uint32_t s_disconnection_count;
static char s_device_name[20];

/* d3f4a100-6b9a-4e51-9f71-515345452e41 */
static const ble_uuid128_t s_qsee_service_uuid =
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
    qsee::DeviceSnapshotService::instance().reportBle(
        s_ble_initialized,
        error,
        s_ble_advertising,
        s_ble_connected,
        s_connection_handle,
        s_connection_count,
        s_disconnection_count,
        s_device_name,
        false,
        s_ble_enabled
    );
}

// Refresh hardware outside the non-blocking NimBLE access callback.
static void refresh_snapshot_task(void *argument)
{
    (void)argument;
    (void)qsee_hardware_refresh("ble-command");
    vTaskDelete(nullptr);
}

// Serve chunked telemetry reads and the bounded "refresh" command.
static int qsee_gatt_access(
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
        const qsee::DeviceSnapshot snapshot =
            qsee::DeviceSnapshotService::instance().snapshot();
        char json[384];
        const int length = snprintf(
            json,
            sizeof(json),
            "{\"seq\":%lu,\"battery\":%d,\"usb\":%s,\"wifi\":\"%s\","
            "\"ip\":\"%s\",\"gps_fix\":%s,\"sat\":%u,\"imu\":%s,"
            "\"sd\":%s,\"heap\":%lu,\"psram_free\":%lu}",
            static_cast<unsigned long>(snapshot.sequence),
            snapshot.battery.level_percent,
            snapshot.battery.usb_present ? "true" : "false",
            snapshot.radios.wifi.ssid,
            snapshot.radios.wifi.ip_address,
            snapshot.gps.has_fix ? "true" : "false",
            static_cast<unsigned>(snapshot.gps.satellites),
            snapshot.imu.calibrated ? "true" : "false",
            snapshot.storage.card_present ? "true" : "false",
            static_cast<unsigned long>(snapshot.memory.heap_free_bytes),
            static_cast<unsigned long>(snapshot.memory.psram_free_bytes)
        );
        if (length < 0 || context->offset > static_cast<uint16_t>(length)) {
            return BLE_ATT_ERR_INVALID_OFFSET;
        }
        const size_t remaining =
            static_cast<size_t>(length) - context->offset;
        // Honor ATT offsets so clients can read telemetry across MTU chunks.
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
        if (xTaskCreate(
                refresh_snapshot_task,
                "qsee_ble_refresh",
                4096,
                nullptr,
                4,
                nullptr
            ) != pdPASS) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        ESP_LOGI(TAG, "BLE GATT: canonical snapshot refresh requested");
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def s_qsee_characteristics[] = {
    {&s_telemetry_uuid.u, qsee_gatt_access, nullptr, nullptr,
     BLE_GATT_CHR_F_READ, 0, nullptr, nullptr},
    {&s_command_uuid.u, qsee_gatt_access, nullptr, nullptr,
     BLE_GATT_CHR_F_WRITE, 0, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};

static const struct ble_gatt_svc_def s_qsee_services[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &s_qsee_service_uuid.u, nullptr,
     s_qsee_characteristics},
    {0, nullptr, nullptr, nullptr},
};

static void start_ble_advertising(void);

// Track connection lifecycle and resume advertising after disconnect.
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE: connection %s", event->connect.status == 0 ? "established" : "failed");
        if (event->connect.status == 0) {
            s_ble_connected = true;
            s_ble_advertising = false;
            s_connection_handle = event->connect.conn_handle;
            ++s_connection_count;
            publish_ble_snapshot();
        } else {
            start_ble_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_ble_connected = false;
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        ++s_disconnection_count;
        publish_ble_snapshot();
        if (!s_ble_sleep_suspended) {
            ESP_LOGI(TAG, "BLE: disconnected, advertising resumed");
            start_ble_advertising();
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_ble_sleep_suspended) {
            start_ble_advertising();
        }
        break;
    default:
        break;
    }
    return 0;
}

static void start_ble_advertising(void)
{
    if (s_ble_sleep_suspended) {
        s_ble_advertising = false;
        publish_ble_snapshot();
        return;
    }

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE: advertising data failed (%d)", rc);
        return;
    }

    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_ble_address_type, NULL, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc == 0) {
        s_ble_advertising = true;
        publish_ble_snapshot();
        ESP_LOGI(TAG, "BLE: advertising as %s", name);
    } else {
        s_ble_advertising = false;
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGW(TAG, "BLE: advertising start failed (%d)", rc);
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE: host reset (%d)", reason);
}

static void ble_on_sync(void)
{
    if (s_ble_sleep_suspended) {
        return;
    }
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_ble_address_type) != 0) {
        ESP_LOGW(TAG, "BLE: no usable address");
        return;
    }
    start_ble_advertising();
}

static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Start NimBLE with QSee telemetry and command characteristics.
extern "C" esp_err_t qsee_ble_init(void)
{
    if (s_ble_initialized) {
        publish_ble_snapshot();
        return ESP_OK;
    }
    if (s_ble_provisioning_reserved || qsee_provisioning_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ble_sleep_suspended = false;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        publish_ble_snapshot(err);
        ESP_LOGW(TAG, "BLE: NimBLE init failed (%s)", esp_err_to_name(err));
        return err;
    }

    uint8_t mac[6] = {};
    (void)esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_device_name, sizeof(s_device_name), "QSEE-%02X%02X", mac[4], mac[5]);

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_dis_init();
    ble_svc_bas_init();
    (void)ble_svc_dis_manufacturer_name_set("QSee.ai");
    (void)ble_svc_dis_model_number_set("Waveshare ESP32-S3 AMOLED 1.75");
    (void)ble_svc_dis_hardware_revision_set("ESP32-S3 AMOLED GPS");
    (void)ble_svc_dis_firmware_revision_set(esp_app_get_description()->version);
    const qsee::DeviceSnapshot snapshot =
        qsee::DeviceSnapshotService::instance().snapshot();
    if (snapshot.battery.level_percent >= 0) {
        (void)ble_svc_bas_battery_level_set(
            static_cast<uint8_t>(snapshot.battery.level_percent)
        );
    }
    int rc = ble_gatts_count_cfg(s_qsee_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_qsee_services);
    }
    if (rc != 0) {
        (void)nimble_port_deinit();
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGW(TAG, "BLE: QSee GATT service registration failed (%d)", rc);
        return ESP_FAIL;
    }
    if (ble_svc_gap_device_name_set(s_device_name) != 0) {
        (void)nimble_port_deinit();
        publish_ble_snapshot(ESP_FAIL);
        ESP_LOGW(TAG, "BLE: device name rejected");
        return ESP_FAIL;
    }

    s_ble_initialized = true;
    publish_ble_snapshot();
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE: NimBLE host and QSee telemetry GATT ready");
    return ESP_OK;
}

// Stop the telemetry host while preserving whether sleep should block restart.
static esp_err_t stop_ble_runtime(
    bool keep_suspended,
    bool *was_initialized
)
{
    if (was_initialized == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *was_initialized = s_ble_initialized;
    if (!s_ble_initialized) {
        return ESP_OK;
    }

    s_ble_sleep_suspended = true;
    if (s_ble_advertising) {
        (void)ble_gap_adv_stop();
    }
    const int stop_result = nimble_port_stop();
    if (stop_result != 0) {
        s_ble_sleep_suspended = false;
        start_ble_advertising();
        return ESP_FAIL;
    }

    const esp_err_t deinit_result = nimble_port_deinit();
    s_ble_initialized = false;
    s_ble_advertising = false;
    s_ble_connected = false;
    s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble_sleep_suspended = keep_suspended;
    publish_ble_snapshot(deinit_result);
    return deinit_result;
}

static esp_err_t suspend_ble_for_light_sleep(bool *was_initialized)
{
    return stop_ble_runtime(true, was_initialized);
}
#else
extern "C" esp_err_t qsee_ble_init(void)
{
    ESP_LOGW(TAG, "BLE: disabled by sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t suspend_ble_for_light_sleep(bool *was_initialized)
{
    if (was_initialized == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *was_initialized = false;
    return ESP_OK;
}

static esp_err_t stop_ble_runtime(
    bool keep_suspended,
    bool *was_initialized
)
{
    (void)keep_suspended;
    return suspend_ble_for_light_sleep(was_initialized);
}
#endif

enum class RadioSleepState {
    Awake,
    Suspending,
    Suspended,
    Restoring,
};

static RadioSleepState s_radio_sleep_state = RadioSleepState::Awake;
static bool s_restore_wifi_started;
static bool s_restore_wifi_connect;
static bool s_restore_ble;

// Read the user-owned BLE intent once without starting the controller.
static esp_err_t load_ble_preference()
{
    if (s_ble_preference_loaded) {
        return ESP_OK;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            return init_error;
        }
    }
    const esp_err_t read_error =
        settings.getBool(kBleEnabledSetting, false, &s_ble_enabled);
    if (read_error == ESP_OK) {
        s_ble_preference_loaded = true;
    }
    return read_error;
}

// Apply saved telemetry advertising after boot-critical allocations finish.
extern "C" esp_err_t qsee_ble_apply_saved_enabled(void)
{
    const esp_err_t preference_error = load_ble_preference();
    if (preference_error != ESP_OK) {
        return preference_error;
    }
    if (s_ble_enabled) {
        const esp_err_t start_error = qsee_ble_init();
        if (start_error != ESP_OK) {
            qsee::DeviceSnapshotService::instance().reportBle(
                false,
                start_error,
                false,
                false,
                0xFFFF,
                0,
                0,
                nullptr,
                false,
                true
            );
        }
        return start_error;
    }

    bool was_initialized = false;
    const esp_err_t stop_error =
        stop_ble_runtime(false, &was_initialized);
    qsee::DeviceSnapshotService::instance().reportBle(
        false,
        stop_error,
        false,
        false,
        0xFFFF,
        0,
        0,
        nullptr,
        false,
        false
    );
    ESP_LOGI(TAG, "BLE telemetry: disabled by saved preference");
    return stop_error;
}

// Persist normal BLE intent, then converge the runtime to that state.
extern "C" esp_err_t qsee_ble_set_enabled(bool enabled)
{
    if (s_ble_provisioning_reserved || qsee_provisioning_active() ||
        s_radio_sleep_state != RadioSleepState::Awake) {
        return ESP_ERR_INVALID_STATE;
    }

    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            return init_error;
        }
    }
    const esp_err_t persist_error =
        settings.setBool(kBleEnabledSetting, enabled);
    if (persist_error != ESP_OK) {
        return persist_error;
    }
    s_ble_enabled = enabled;
    s_ble_preference_loaded = true;

    esp_err_t runtime_error = ESP_OK;
    if (enabled) {
        runtime_error = qsee_ble_init();
        if (runtime_error != ESP_OK) {
            qsee::DeviceSnapshotService::instance().reportBle(
                false,
                runtime_error,
                false,
                false,
                0xFFFF,
                0,
                0,
                nullptr,
                false,
                true
            );
        }
    } else {
        bool was_initialized = false;
        runtime_error = stop_ble_runtime(false, &was_initialized);
        qsee::DeviceSnapshotService::instance().reportBle(
            false,
            runtime_error,
            false,
            false,
            0xFFFF,
            0,
            0,
            nullptr,
            false,
            false
        );
    }
    ESP_LOGI(
        TAG,
        "BLE telemetry preference: %s",
        enabled ? "enabled" : "disabled"
    );
    return runtime_error;
}

extern "C" bool qsee_ble_get_enabled(void)
{
    (void)load_ble_preference();
    return s_ble_enabled;
}

// Stop telemetry without changing its preference before provisioning starts.
extern "C" esp_err_t qsee_ble_prepare_provisioning(void)
{
    if (s_ble_provisioning_reserved) {
        return ESP_OK;
    }
    if (s_radio_sleep_state != RadioSleepState::Awake ||
        qsee_provisioning_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    bool was_initialized = false;
    const esp_err_t stop_error =
        stop_ble_runtime(false, &was_initialized);
    if (stop_error != ESP_OK) {
        return stop_error;
    }
    s_ble_provisioning_reserved = true;
    ESP_LOGI(TAG, "BLE controller reserved for Espressif provisioning");
    return ESP_OK;
}

// Return the controller to telemetry only when the saved preference requests it.
extern "C" esp_err_t qsee_ble_restore_after_provisioning(void)
{
    if (!s_ble_provisioning_reserved) {
        return ESP_OK;
    }
    s_ble_provisioning_reserved = false;
    const esp_err_t preference_error = load_ble_preference();
    if (preference_error != ESP_OK) {
        return preference_error;
    }
    if (s_ble_enabled) {
        return qsee_ble_init();
    }
    qsee::DeviceSnapshotService::instance().reportBle(
        false,
        ESP_OK,
        false,
        false,
        0xFFFF,
        0,
        0,
        nullptr,
        false,
        false
    );
    return ESP_OK;
}

// Initialize Wi-Fi plus only the BLE runtime requested by user preference.
esp_err_t qsee_radios_init(void)
{
    const esp_err_t wifi = qsee_wifi_init();
    const esp_err_t ble = qsee_ble_apply_saved_enabled();
    if (wifi != ESP_OK || ble != ESP_OK) {
        ESP_LOGW(TAG, "Radio inventory completed with degraded capability");
    } else {
        ESP_LOGI(
            TAG,
            "Radio inventory: Wi-Fi ready; BLE telemetry %s",
            s_ble_enabled ? "enabled" : "disabled"
        );
    }
    return ESP_OK;
}

// Capture and stop radios; reject sleep while provisioning owns BLE.
extern "C" esp_err_t qsee_radios_prepare_light_sleep(void)
{
    if (s_radio_sleep_state == RadioSleepState::Suspended) {
        return ESP_OK;
    }
    if (s_radio_sleep_state != RadioSleepState::Awake ||
        s_ble_provisioning_reserved || qsee_provisioning_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_radio_sleep_state = RadioSleepState::Suspending;
    esp_err_t result = suspend_ble_for_light_sleep(&s_restore_ble);
    if (result != ESP_OK) {
        if (s_restore_ble) {
            const esp_err_t restore_error = qsee_ble_init();
            if (restore_error != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "BLE rollback after suspend failure: %s",
                    esp_err_to_name(restore_error)
                );
            }
        }
        s_restore_ble = false;
        s_radio_sleep_state = RadioSleepState::Awake;
        return result;
    }

    result = qsee_wifi_prepare_light_sleep(
        &s_restore_wifi_started,
        &s_restore_wifi_connect
    );
    if (result != ESP_OK) {
        if (s_restore_ble) {
            const esp_err_t restore_error = qsee_ble_init();
            if (restore_error != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "BLE rollback after Wi-Fi stop failure: %s",
                    esp_err_to_name(restore_error)
                );
            }
        }
        s_restore_wifi_started = false;
        s_restore_wifi_connect = false;
        s_restore_ble = false;
        s_radio_sleep_state = RadioSleepState::Awake;
        return result;
    }

    s_radio_sleep_state = RadioSleepState::Suspended;
    return ESP_OK;
}

// Restore Wi-Fi first, then return an optional telemetry GATT to advertising.
extern "C" esp_err_t qsee_radios_restore_after_light_sleep(void)
{
    if (s_radio_sleep_state == RadioSleepState::Awake) {
        return ESP_OK;
    }
    if (s_radio_sleep_state != RadioSleepState::Suspended) {
        return ESP_ERR_INVALID_STATE;
    }

    s_radio_sleep_state = RadioSleepState::Restoring;
    esp_err_t first_error = qsee_wifi_restore_after_light_sleep(
        s_restore_wifi_started,
        s_restore_wifi_connect
    );
    if (s_restore_ble) {
        const esp_err_t ble_error = qsee_ble_init();
        if (ble_error != ESP_OK && first_error == ESP_OK) {
            first_error = ble_error;
        }
    }

    s_restore_wifi_started = false;
    s_restore_wifi_connect = false;
    s_restore_ble = false;
    s_radio_sleep_state = RadioSleepState::Awake;
    return first_error;
}
