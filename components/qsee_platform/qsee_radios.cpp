#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

static const char *TAG = "qsee_radios";

static esp_err_t init_wifi(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS: initialization failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi: netif init failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi: event loop failed (%s)", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGW(TAG, "Wi-Fi: STA netif unavailable");
        return ESP_FAIL;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = 0;
    err = esp_wifi_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi: driver init failed (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    err = esp_wifi_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi: STA ready, credentials not configured");
    } else {
        ESP_LOGW(TAG, "Wi-Fi: start failed (%s)", esp_err_to_name(err));
    }
    return err;
}

#if CONFIG_BT_NIMBLE_ENABLED
static uint8_t s_ble_address_type;

static void start_ble_advertising(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE: connection %s", event->connect.status == 0 ? "established" : "failed");
        if (event->connect.status != 0) {
            start_ble_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
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
        ESP_LOGI(TAG, "BLE: advertising as %s", name);
    } else {
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

static esp_err_t init_ble(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BLE: NimBLE init failed (%s)", esp_err_to_name(err));
        return err;
    }

    uint8_t mac[6] = {};
    (void)esp_read_mac(mac, ESP_MAC_BT);
    char name[20];
    snprintf(name, sizeof(name), "QSEE-%02X%02X", mac[4], mac[5]);

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(name) != 0) {
        ESP_LOGW(TAG, "BLE: device name rejected");
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE: NimBLE host ready");
    return ESP_OK;
}
#else
static esp_err_t init_ble(void)
{
    ESP_LOGW(TAG, "BLE: disabled by sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

esp_err_t qsee_radios_init(void)
{
    const esp_err_t wifi = init_wifi();
    const esp_err_t ble = init_ble();
    qsee::DeviceSnapshotService::instance().reportWifi(wifi == ESP_OK, wifi);
    qsee::DeviceSnapshotService::instance().reportBle(ble == ESP_OK, ble);
    if (wifi != ESP_OK || ble != ESP_OK) {
        ESP_LOGW(TAG, "Radio inventory completed with degraded capability");
    } else {
        ESP_LOGI(TAG, "Radio inventory: Wi-Fi + BLE ready");
    }
    return ESP_OK;
}
