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

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#if CONFIG_BT_NIMBLE_ENABLED
static uint8_t s_ble_address_type;
static bool s_ble_initialized;
static bool s_ble_advertising;
static bool s_ble_connected;
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
        s_device_name
    );
}

static void refresh_snapshot_task(void *argument)
{
    (void)argument;
    (void)qsee_hardware_refresh("ble-command");
    vTaskDelete(nullptr);
}

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
        ESP_LOGI(TAG, "BLE: disconnected, advertising resumed");
        start_ble_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_ble_advertising();
        break;
    default:
        break;
    }
    return 0;
}

static void start_ble_advertising(void)
{
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

extern "C" esp_err_t qsee_ble_init(void)
{
    if (s_ble_initialized) {
        return ESP_OK;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
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
        ESP_LOGW(TAG, "BLE: QSee GATT service registration failed (%d)", rc);
        return ESP_FAIL;
    }
    if (ble_svc_gap_device_name_set(s_device_name) != 0) {
        ESP_LOGW(TAG, "BLE: device name rejected");
        return ESP_FAIL;
    }

    s_ble_initialized = true;
    publish_ble_snapshot();
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE: NimBLE host and QSee telemetry GATT ready");
    return ESP_OK;
}
#else
extern "C" esp_err_t qsee_ble_init(void)
{
    ESP_LOGW(TAG, "BLE: disabled by sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

esp_err_t qsee_radios_init(void)
{
    const esp_err_t wifi = qsee_wifi_init();
    const esp_err_t provisioning = qsee_provisioning_init();
    qsee_wifi_status_t wifi_status = {};
    qsee_wifi_get_status(&wifi_status);
    const esp_err_t ble = wifi_status.credentials_configured
                              ? qsee_ble_init()
                              : qsee_provisioning_start();
    if (ble != ESP_OK) {
        qsee::DeviceSnapshotService::instance().reportBle(false, ble);
    }
    if (wifi != ESP_OK || ble != ESP_OK) {
        ESP_LOGW(TAG, "Radio inventory completed with degraded capability");
    } else if (!wifi_status.credentials_configured) {
        ESP_LOGI(
            TAG,
            "Radio inventory: Wi-Fi ready; secure BLE provisioning active as %s",
            qsee_provisioning_service_name()
        );
    } else {
        ESP_LOGI(TAG, "Radio inventory: Wi-Fi + BLE telemetry GATT ready");
    }
    if (provisioning != ESP_OK) {
        ESP_LOGW(TAG, "Provisioning manager unavailable: %s", esp_err_to_name(provisioning));
    }
    return ESP_OK;
}
