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

#include <atomic>
#include <cstdio>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

namespace {

const char *TAG = "qsee_provision";
std::atomic_bool s_initialized;
std::atomic_bool s_active;
std::atomic_bool s_cleanup_task_started;
esp_event_handler_instance_t s_event_instance;
char s_service_name[20];
char s_proof_of_possession[20];

void publish_provisioning_snapshot(bool ready, esp_err_t error, bool active)
{
    qsee::DeviceSnapshotService::instance().reportBle(
        ready,
        error,
        active,
        false,
        0xFFFF,
        0,
        0,
        s_service_name,
        true
    );
}

void cleanup_and_start_telemetry_ble_task(void *argument)
{
    (void)argument;
    // Leave the event callback before unregistering it and releasing NimBLE.
    vTaskDelay(pdMS_TO_TICKS(100));
    if (s_event_instance != nullptr) {
        (void)esp_event_handler_instance_unregister(
            WIFI_PROV_EVENT,
            ESP_EVENT_ANY_ID,
            s_event_instance
        );
        s_event_instance = nullptr;
    }
    wifi_prov_mgr_deinit();
    s_initialized.store(false, std::memory_order_release);
    publish_provisioning_snapshot(false, ESP_OK, false);

    // Let the provisioning transport release its NimBLE host before telemetry.
    vTaskDelay(pdMS_TO_TICKS(150));
    const esp_err_t result = qsee_ble_init();
    ESP_LOGI(
        TAG,
        "BLE transition from provisioning to telemetry: %s",
        esp_err_to_name(result)
    );
    s_cleanup_task_started.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

void provisioning_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;
    if (event_base != WIFI_PROV_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_PROV_START:
        s_active.store(true, std::memory_order_release);
        publish_provisioning_snapshot(true, ESP_OK, true);
        ESP_LOGI(
            TAG,
            "Secure BLE provisioning started: service=%s PoP=%s",
            s_service_name,
            s_proof_of_possession
        );
        break;
    case WIFI_PROV_CRED_RECV: {
        const auto *configuration =
            static_cast<const wifi_sta_config_t *>(event_data);
        ESP_LOGI(
            TAG,
            "Wi-Fi credentials received for SSID=%s; password suppressed",
            configuration != nullptr
                ? reinterpret_cast<const char *>(configuration->ssid)
                : "<unknown>"
        );
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        const auto *reason =
            static_cast<const wifi_prov_sta_fail_reason_t *>(event_data);
        ESP_LOGW(
            TAG,
            "Provisioning connection failed: %s",
            reason != nullptr && *reason == WIFI_PROV_STA_AUTH_ERROR
                ? "authentication"
                : "access point not found"
        );
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning credentials accepted and persisted by ESP-IDF");
        break;
    case WIFI_PROV_END:
        s_active.store(false, std::memory_order_release);
        publish_provisioning_snapshot(true, ESP_OK, false);
        ESP_LOGI(TAG, "Provisioning transport stopped");
        if (!s_cleanup_task_started.exchange(true, std::memory_order_acq_rel)) {
            if (xTaskCreate(
                    cleanup_and_start_telemetry_ble_task,
                    "qsee_ble_transition",
                    4096,
                    nullptr,
                    5,
                    nullptr
                ) != pdPASS) {
                s_cleanup_task_started.store(false, std::memory_order_release);
                ESP_LOGE(TAG, "Unable to start BLE telemetry transition task");
            }
        }
        break;
    default:
        break;
    }
}

void make_identity()
{
    uint8_t mac[6] = {};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(
        s_service_name,
        sizeof(s_service_name),
        "PROV_QSEE_%02X%02X",
        mac[4],
        mac[5]
    );
    std::snprintf(
        s_proof_of_possession,
        sizeof(s_proof_of_possession),
        "QSEE-%02X%02X%02X",
        mac[3],
        mac[4],
        mac[5]
    );
}

}  // namespace

extern "C" esp_err_t qsee_provisioning_init(void)
{
    if (s_initialized.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    if (s_cleanup_task_started.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    make_identity();

    const wifi_prov_mgr_config_t configuration = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
    };
    esp_err_t result = wifi_prov_mgr_init(configuration);
    if (result != ESP_OK) {
        publish_provisioning_snapshot(false, result, false);
        ESP_LOGW(TAG, "Provisioning manager initialization failed: %s",
                 esp_err_to_name(result));
        return result;
    }
    result = esp_event_handler_instance_register(
        WIFI_PROV_EVENT,
        ESP_EVENT_ANY_ID,
        provisioning_event_handler,
        nullptr,
        &s_event_instance
    );
    if (result != ESP_OK) {
        wifi_prov_mgr_deinit();
        publish_provisioning_snapshot(false, result, false);
        return result;
    }

    static uint8_t service_uuid[] = {
        0x41, 0x2e, 0x45, 0x45, 0x53, 0x51, 0x71, 0x9f,
        0x51, 0x4e, 0x9a, 0x6b, 0x10, 0xa1, 0xf4, 0xd3,
    };
    result = wifi_prov_scheme_ble_set_service_uuid(service_uuid);
    if (result != ESP_OK) {
        (void)esp_event_handler_instance_unregister(
            WIFI_PROV_EVENT,
            ESP_EVENT_ANY_ID,
            s_event_instance
        );
        s_event_instance = nullptr;
        wifi_prov_mgr_deinit();
        publish_provisioning_snapshot(false, result, false);
        return result;
    }

    s_initialized.store(true, std::memory_order_release);
    publish_provisioning_snapshot(true, ESP_OK, false);
    ESP_LOGI(TAG, "Provisioning manager ready: Security 1 with proof of possession");
    return ESP_OK;
}

extern "C" esp_err_t qsee_provisioning_start(void)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        const esp_err_t result = qsee_provisioning_init();
        if (result != ESP_OK) {
            return result;
        }
    }
    if (s_active.load(std::memory_order_acquire)) {
        return ESP_OK;
    }

    bool provisioned = false;
    esp_err_t result = wifi_prov_mgr_is_provisioned(&provisioned);
    if (result != ESP_OK) {
        return result;
    }
    if (provisioned) {
        ESP_LOGI(TAG, "Stored Wi-Fi profile present; BLE provisioning not needed");
        return ESP_OK;
    }

    result = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,
        s_proof_of_possession,
        s_service_name,
        nullptr
    );
    if (result == ESP_OK) {
        s_active.store(true, std::memory_order_release);
        publish_provisioning_snapshot(true, ESP_OK, true);
        ESP_LOGI(
            TAG,
            "Provision with ESP SoftAP app: service=%s transport=ble PoP=%s",
            s_service_name,
            s_proof_of_possession
        );
    } else {
        publish_provisioning_snapshot(false, result, false);
    }
    return result;
}

extern "C" esp_err_t qsee_provisioning_stop(void)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    if (s_active.load(std::memory_order_acquire)) {
        wifi_prov_mgr_stop_provisioning();
        s_active.store(false, std::memory_order_release);
    }
    return ESP_OK;
}

extern "C" bool qsee_provisioning_active(void)
{
    return s_active.load(std::memory_order_acquire);
}

extern "C" const char *qsee_provisioning_service_name(void)
{
    return s_service_name;
}

extern "C" const char *qsee_provisioning_pop(void)
{
    return s_proof_of_possession;
}
