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

// Wi-Fi connectivity helpers for scan, connect, and link-state reporting.
// Supplies network status to the platform snapshot and provisioning flow.

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

#define WIFI_RECONNECT_INITIAL_MS 1000U
#define WIFI_RECONNECT_MAX_MS 30000U

namespace {

const char *TAG = "qsee_wifi";
static_assert(
    QSEE_WIFI_MAX_NETWORKS == qsee::kWifiSnapshotMaxNetworks,
    "Wi-Fi driver and snapshot capacities must match"
);

SemaphoreHandle_t s_lock;
StaticSemaphore_t s_lock_storage;
TimerHandle_t s_reconnect_timer;
StaticTimer_t s_reconnect_timer_storage;
esp_netif_t *s_station_netif;
esp_event_handler_instance_t s_wifi_event_instance;
esp_event_handler_instance_t s_ip_event_instance;
bool s_initialized;
bool s_driver_initialized;
bool s_connect_requested;
bool s_sleep_suspended;
bool s_station_operation_active;
bool s_provisioning_active;
bool s_provisioning_profile_valid;
bool s_provisioning_connect_requested;
wifi_config_t s_provisioning_profile;
wifi_storage_t s_profile_storage = WIFI_STORAGE_FLASH;
wifi_storage_t s_provisioning_profile_storage = WIFI_STORAGE_FLASH;
uint8_t s_consecutive_disconnects;
qsee_wifi_status_t s_status;
qsee_wifi_network_t s_networks[QSEE_WIFI_MAX_NETWORKS];
wifi_ap_record_t s_scan_records[QSEE_WIFI_MAX_NETWORKS];
size_t s_network_count;

void wifi_lock()
{
    if (s_lock != nullptr) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void wifi_unlock()
{
    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }
}

void copy_ssid(char *destination, size_t destination_size, const uint8_t *source, size_t source_size)
{
    if (destination == nullptr || destination_size == 0 || source == nullptr) {
        return;
    }
    size_t length = 0;
    while (length < source_size && source[length] != '\0') {
        ++length;
    }
    if (length >= destination_size) {
        length = destination_size - 1U;
    }
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

void secure_zero(void *data, size_t size)
{
    auto *cursor = static_cast<volatile uint8_t *>(data);
    while (cursor != nullptr && size-- > 0U) {
        *cursor++ = 0U;
    }
}

// Copy mutable Wi-Fi state under lock, then publish without holding it.
void publish_snapshot()
{
    qsee_wifi_status_t status = {};
    qsee::WifiSnapshot snapshot;
    size_t network_count = 0;
    wifi_lock();
    status = s_status;
    network_count = s_network_count;
    if (network_count > QSEE_WIFI_MAX_NETWORKS) {
        network_count = QSEE_WIFI_MAX_NETWORKS;
    }
    for (size_t index = 0; index < network_count; ++index) {
        strlcpy(
            snapshot.networks[index].ssid,
            s_networks[index].ssid,
            sizeof(snapshot.networks[index].ssid)
        );
        snapshot.networks[index].rssi = s_networks[index].rssi;
        snapshot.networks[index].channel = s_networks[index].channel;
        snapshot.networks[index].secured = s_networks[index].secured;
    }
    wifi_unlock();

    snapshot.available = status.initialized;
    snapshot.health = status.initialized && status.started
                          ? qsee::DeviceHealth::Ready
                          : (status.last_error == ESP_ERR_NOT_FOUND
                                 ? qsee::DeviceHealth::Missing
                                 : qsee::DeviceHealth::Degraded);
    snapshot.last_error = status.last_error;
    snapshot.started = status.started;
    snapshot.scanning = status.scanning;
    snapshot.scan_completed = status.scan_completed;
    snapshot.credentials_configured = status.credentials_configured;
    snapshot.connect_requested = status.connect_requested;
    snapshot.connected = status.connected;
    snapshot.ip_acquired = status.ip_acquired;
    snapshot.reconnect_pending = status.reconnect_pending;
    snapshot.rssi = status.rssi;
    snapshot.channel = status.channel;
    snapshot.last_disconnect_reason = status.last_disconnect_reason;
    snapshot.event_sequence = status.event_sequence;
    snapshot.connect_attempt_count = status.connect_attempt_count;
    snapshot.reconnect_attempt_count = status.reconnect_attempt_count;
    snapshot.disconnect_count = status.disconnect_count;
    snapshot.scan_sequence = status.scan_sequence;
    snapshot.network_count = static_cast<uint8_t>(network_count);
    strlcpy(snapshot.ssid, status.ssid, sizeof(snapshot.ssid));
    strlcpy(snapshot.ip_address, status.ip_address, sizeof(snapshot.ip_address));
    strlcpy(snapshot.netmask, status.netmask, sizeof(snapshot.netmask));
    strlcpy(snapshot.gateway, status.gateway, sizeof(snapshot.gateway));
    strlcpy(snapshot.mac_address, status.mac_address, sizeof(snapshot.mac_address));
    qsee::DeviceSnapshotService::instance().reportWifi(snapshot);
}

void record_error(esp_err_t error)
{
    wifi_lock();
    s_status.last_error = error;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
}

void cancel_reconnect()
{
    if (s_reconnect_timer != nullptr) {
        (void)xTimerStop(s_reconnect_timer, 0);
    }
    wifi_lock();
    s_status.reconnect_pending = false;
    s_status.reconnect_delay_ms = 0;
    wifi_unlock();
}

// Arm one bounded reconnect timer and expose the pending delay.
void schedule_reconnect(uint32_t delay_ms)
{
    if (s_reconnect_timer == nullptr) {
        record_error(ESP_ERR_INVALID_STATE);
        return;
    }
    if (delay_ms < WIFI_RECONNECT_INITIAL_MS) {
        delay_ms = WIFI_RECONNECT_INITIAL_MS;
    } else if (delay_ms > WIFI_RECONNECT_MAX_MS) {
        delay_ms = WIFI_RECONNECT_MAX_MS;
    }

    wifi_lock();
    s_status.reconnect_pending = true;
    s_status.reconnect_delay_ms = delay_ms;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    if (xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(delay_ms), 0) != pdPASS) {
        record_error(ESP_ERR_TIMEOUT);
    }
}

// Retry only while the caller still requests a connection.
void reconnect_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    wifi_lock();
    const bool should_connect =
        s_initialized && s_status.started && s_connect_requested &&
        !s_sleep_suspended && !s_provisioning_active;
    s_status.reconnect_pending = false;
    s_status.reconnect_delay_ms = 0;
    if (should_connect) {
        ++s_status.connect_attempt_count;
        ++s_status.reconnect_attempt_count;
        ++s_status.event_sequence;
    }
    wifi_unlock();
    publish_snapshot();

    if (!should_connect) {
        return;
    }
    const esp_err_t error = esp_wifi_connect();
    if (error == ESP_ERR_WIFI_STATE) {
        schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
    } else {
        record_error(error);
    }
}

// Copy driver-owned scan records into the fixed public network list.
void handle_scan_done()
{
    uint16_t count = QSEE_WIFI_MAX_NETWORKS;
    std::memset(s_scan_records, 0, sizeof(s_scan_records));
    const esp_err_t error = esp_wifi_scan_get_ap_records(&count, s_scan_records);

    wifi_lock();
    s_status.scanning = false;
    s_status.scan_completed = true;
    ++s_status.scan_sequence;
    ++s_status.event_sequence;
    if (error == ESP_OK) {
        s_network_count = count;
        s_status.network_count = static_cast<uint8_t>(count);
        for (size_t index = 0; index < count; ++index) {
            copy_ssid(
                s_networks[index].ssid,
                sizeof(s_networks[index].ssid),
                s_scan_records[index].ssid,
                sizeof(s_scan_records[index].ssid)
            );
            s_networks[index].rssi = s_scan_records[index].rssi;
            s_networks[index].channel = s_scan_records[index].primary;
            s_networks[index].secured = s_scan_records[index].authmode != WIFI_AUTH_OPEN;
        }
        s_status.last_error = ESP_OK;
    } else {
        s_network_count = 0;
        s_status.network_count = 0;
        s_status.last_error = error;
    }
    wifi_unlock();
    publish_snapshot();

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Scan complete: %u access point(s)", static_cast<unsigned>(count));
    } else {
        ESP_LOGW(TAG, "Scan result failed: %s", esp_err_to_name(error));
    }
}

// Fold ESP-IDF Wi-Fi/IP events into one published station state.
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            wifi_lock();
            s_status.started = true;
            s_status.last_error = ESP_OK;
            ++s_status.event_sequence;
            wifi_unlock();
            publish_snapshot();
            ESP_LOGI(TAG, "Station driver started");
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            const auto *connected = static_cast<const wifi_event_sta_connected_t *>(event_data);
            wifi_lock();
            s_status.connected = true;
            s_status.ip_acquired = false;
            s_status.channel = connected != nullptr ? connected->channel : 0;
            s_status.last_error = ESP_OK;
            ++s_status.event_sequence;
            wifi_unlock();
            publish_snapshot();
            ESP_LOGI(TAG, "Associated with %s on channel %u; waiting for DHCP",
                     s_status.ssid, static_cast<unsigned>(s_status.channel));
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            const auto *disconnected = static_cast<const wifi_event_sta_disconnected_t *>(event_data);
            bool reconnect = false;
            uint32_t delay_ms = 0;
            wifi_lock();
            s_status.connected = false;
            s_status.ip_acquired = false;
            s_status.rssi = 0;
            s_status.channel = 0;
            s_status.ip_address[0] = '\0';
            s_status.netmask[0] = '\0';
            s_status.gateway[0] = '\0';
            ++s_status.disconnect_count;
            s_status.last_disconnect_reason = disconnected != nullptr ? disconnected->reason : 0;
            s_status.last_error = ESP_OK;
            ++s_status.event_sequence;
            reconnect =
                s_connect_requested && !s_sleep_suspended &&
                !s_provisioning_active;
            if (reconnect) {
                const uint8_t shift = s_consecutive_disconnects < 5 ? s_consecutive_disconnects : 5;
                delay_ms = WIFI_RECONNECT_INITIAL_MS << shift;
                if (s_consecutive_disconnects < UINT8_MAX) {
                    ++s_consecutive_disconnects;
                }
            }
            wifi_unlock();
            publish_snapshot();
            ESP_LOGW(TAG, "Disconnected from %s, reason=%u, reconnect=%s in %lu ms",
                     s_status.ssid[0] != '\0' ? s_status.ssid : "<unset>",
                     disconnected != nullptr ? static_cast<unsigned>(disconnected->reason) : 0,
                     reconnect ? "yes" : "no",
                     static_cast<unsigned long>(delay_ms));
            if (reconnect) {
                // Each consecutive failure doubles the delay up to the cap.
                schedule_reconnect(delay_ms);
            }
            break;
        }

        case WIFI_EVENT_SCAN_DONE:
            handle_scan_done();
            break;

        case WIFI_EVENT_STA_STOP:
            cancel_reconnect();
            wifi_lock();
            s_status.started = false;
            s_status.connected = false;
            s_status.ip_acquired = false;
            ++s_status.event_sequence;
            wifi_unlock();
            publish_snapshot();
            ESP_LOGI(TAG, "Station driver stopped");
            break;

        default:
            break;
        }
        return;
    }

    if (event_base != IP_EVENT) {
        return;
    }
    if (event_id == IP_EVENT_STA_GOT_IP) {
        const auto *got_ip = static_cast<const ip_event_got_ip_t *>(event_data);
        wifi_ap_record_t access_point = {};
        (void)esp_wifi_sta_get_ap_info(&access_point);

        char online_ssid[QSEE_WIFI_SSID_MAX_LEN + 1U] = {};
        char online_ip[16] = {};
        int online_rssi = 0;
        wifi_lock();
        s_status.connected = true;
        s_status.ip_acquired = true;
        s_status.credentials_configured = true;
        s_status.connect_requested = true;
        s_connect_requested = true;
        s_status.rssi = access_point.rssi;
        s_status.channel = access_point.primary;
        s_status.reconnect_pending = false;
        s_status.reconnect_delay_ms = 0;
        s_status.last_disconnect_reason = 0;
        s_status.last_error = ESP_OK;
        copy_ssid(s_status.ssid, sizeof(s_status.ssid), access_point.ssid, sizeof(access_point.ssid));
        if (got_ip != nullptr) {
            ip4addr_ntoa_r(
                reinterpret_cast<const ip4_addr_t *>(&got_ip->ip_info.ip),
                s_status.ip_address,
                sizeof(s_status.ip_address)
            );
            ip4addr_ntoa_r(
                reinterpret_cast<const ip4_addr_t *>(&got_ip->ip_info.netmask),
                s_status.netmask,
                sizeof(s_status.netmask)
            );
            ip4addr_ntoa_r(
                reinterpret_cast<const ip4_addr_t *>(&got_ip->ip_info.gw),
                s_status.gateway,
                sizeof(s_status.gateway)
            );
        }
        strlcpy(online_ssid, s_status.ssid, sizeof(online_ssid));
        strlcpy(online_ip, s_status.ip_address, sizeof(online_ip));
        online_rssi = s_status.rssi;
        ++s_status.event_sequence;
        s_consecutive_disconnects = 0;
        wifi_unlock();
        cancel_reconnect();
        publish_snapshot();
        ESP_LOGI(
            TAG,
            "Wi-Fi online: SSID=\"%s\" IPv4=%s RSSI=%d dBm",
            online_ssid,
            online_ip,
            online_rssi
        );
        (void)qsee_time_notify_wifi_online();
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        wifi_lock();
        s_status.ip_acquired = false;
        s_status.ip_address[0] = '\0';
        s_status.netmask[0] = '\0';
        s_status.gateway[0] = '\0';
        ++s_status.event_sequence;
        wifi_unlock();
        publish_snapshot();
        ESP_LOGW(TAG, "DHCP lease lost");
    }
}

esp_err_t initialize_nvs()
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires recovery: %s", esp_err_to_name(error));
        error = nvs_flash_erase();
        if (error == ESP_OK) {
            error = nvs_flash_init();
        }
    }
    return error;
}

}  // namespace

// Initialize the station once and reconnect any profile stored by ESP-IDF.
extern "C" esp_err_t qsee_wifi_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    if (s_lock == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (s_reconnect_timer == nullptr) {
        s_reconnect_timer = xTimerCreateStatic(
            "qsee_wifi_retry",
            pdMS_TO_TICKS(WIFI_RECONNECT_INITIAL_MS),
            pdFALSE,
            nullptr,
            reconnect_timer_callback,
            &s_reconnect_timer_storage
        );
    }
    if (s_reconnect_timer == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    std::memset(&s_status, 0, sizeof(s_status));
    s_status.last_error = ESP_OK;

    esp_err_t error = initialize_nvs();
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    s_station_netif = esp_netif_create_default_wifi_sta();
    if (s_station_netif == nullptr) {
        error = ESP_ERR_NO_MEM;
        goto fail;
    }

    {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        config.nvs_enable = 1;
        error = esp_wifi_init(&config);
    }
    if (error != ESP_OK) {
        goto fail;
    }
    s_driver_initialized = true;
    error = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        nullptr,
        &s_wifi_event_instance
    );
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_event_handler_instance_register(
        IP_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        nullptr,
        &s_ip_event_instance
    );
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_wifi_start();
    if (error != ESP_OK) {
        goto fail;
    }
    error = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (error != ESP_OK) {
        goto fail;
    }
    ESP_LOGI(TAG, "Wi-Fi power save: native minimum-modem mode enabled");

    {
        uint8_t mac[6] = {};
        wifi_config_t stored = {};
        (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
        std::snprintf(
            s_status.mac_address,
            sizeof(s_status.mac_address),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        );
        const esp_err_t stored_result = esp_wifi_get_config(WIFI_IF_STA, &stored);
        const bool has_profile = stored_result == ESP_OK && stored.sta.ssid[0] != '\0';

        wifi_lock();
        s_initialized = true;
        s_status.initialized = true;
        s_status.started = true;
        s_status.credentials_configured = has_profile;
        s_status.connect_requested = has_profile;
        s_connect_requested = has_profile;
        if (has_profile) {
            copy_ssid(s_status.ssid, sizeof(s_status.ssid), stored.sta.ssid, sizeof(stored.sta.ssid));
        }
        ++s_status.event_sequence;
        wifi_unlock();
        publish_snapshot();

        ESP_LOGI(TAG, "Station lifecycle ready: MAC=%s profile=%s",
                 s_status.mac_address, has_profile ? s_status.ssid : "not-configured");
        if (has_profile) {
            wifi_lock();
            ++s_status.connect_attempt_count;
            wifi_unlock();
            error = esp_wifi_connect();
            if (error != ESP_OK && error != ESP_ERR_WIFI_STATE) {
                record_error(error);
                return error;
            }
        }
    }
    return ESP_OK;

fail:
    // Unwind only resources acquired by this initialization attempt.
    if (s_ip_event_instance != nullptr) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_event_instance);
        s_ip_event_instance = nullptr;
    }
    if (s_wifi_event_instance != nullptr) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        s_wifi_event_instance = nullptr;
    }
    if (s_driver_initialized) {
        (void)esp_wifi_deinit();
        s_driver_initialized = false;
    }
    if (s_station_netif != nullptr) {
        esp_netif_destroy_default_wifi(s_station_netif);
        s_station_netif = nullptr;
    }
    wifi_lock();
    s_initialized = false;
    s_status.initialized = false;
    s_status.started = false;
    s_status.last_error = error;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
    ESP_LOGE(TAG, "Station lifecycle failed: %s", esp_err_to_name(error));
    return error;
}

// Apply a RAM or flash-backed profile and begin asynchronous association.
extern "C" esp_err_t qsee_wifi_connect(const char *ssid, const char *password, bool persist)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == nullptr || ssid[0] == '\0' || std::strlen(ssid) > QSEE_WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t ssid_length = std::strlen(ssid);
    const char *safe_password = password != nullptr ? password : "";
    const size_t password_length = strnlen(safe_password, 65U);
    if (password_length > 64U) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_lock();
    const bool unavailable =
        s_sleep_suspended || s_provisioning_active ||
        s_station_operation_active || s_status.scanning;
    if (!unavailable) {
        s_station_operation_active = true;
    }
    wifi_unlock();
    if (unavailable) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t config = {};
    if (ssid_length == sizeof(config.sta.ssid)) {
        std::memcpy(config.sta.ssid, ssid, ssid_length);
    } else {
        strlcpy(
            reinterpret_cast<char *>(config.sta.ssid),
            ssid,
            sizeof(config.sta.ssid)
        );
    }
    if (password_length == sizeof(config.sta.password)) {
        // ESP-IDF accepts a 64-byte hexadecimal PSK without a trailing NUL.
        std::memcpy(config.sta.password, safe_password, password_length);
    } else {
        strlcpy(
            reinterpret_cast<char *>(config.sta.password),
            safe_password,
            sizeof(config.sta.password)
        );
    }
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = safe_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    cancel_reconnect();
    esp_err_t error = esp_wifi_set_storage(persist ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (error != ESP_OK) {
        wifi_lock();
        s_station_operation_active = false;
        wifi_unlock();
        secure_zero(&config, sizeof(config));
        record_error(error);
        return error;
    }
    // The driver owns its copy after esp_wifi_set_config().
    secure_zero(&config, sizeof(config));

    wifi_lock();
    s_connect_requested = true;
    s_status.credentials_configured = true;
    s_status.connect_requested = true;
    s_status.connected = false;
    s_status.ip_acquired = false;
    s_profile_storage = persist ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM;
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    ++s_status.connect_attempt_count;
    s_status.last_error = ESP_OK;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    ESP_LOGI(TAG, "Connecting to SSID=%s, persistence=%s", ssid, persist ? "NVS" : "RAM");
    error = esp_wifi_connect();
    wifi_lock();
    s_station_operation_active = false;
    wifi_unlock();
    if (error == ESP_ERR_WIFI_STATE) {
        schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
        return ESP_OK;
    }
    record_error(error);
    return error;
}

// Reuse the stored password unless the editor explicitly replaced it.
extern "C" esp_err_t qsee_wifi_update_credentials(
    const char *ssid,
    const char *password,
    bool replace_password,
    bool persist
)
{
    if (ssid == nullptr || ssid[0] == '\0' ||
        std::strlen(ssid) > QSEE_WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = qsee_wifi_init();
    if (error != ESP_OK) {
        return error;
    }
    if (replace_password) {
        return qsee_wifi_connect(ssid, password, persist);
    }

    wifi_config_t stored = {};
    error = esp_wifi_get_config(WIFI_IF_STA, &stored);
    if (error != ESP_OK) {
        secure_zero(&stored, sizeof(stored));
        return error;
    }
    const auto *stored_ssid = reinterpret_cast<const char *>(stored.sta.ssid);
    if (stored_ssid[0] == '\0' ||
        std::strncmp(stored_ssid, ssid, sizeof(stored.sta.ssid)) != 0) {
        secure_zero(&stored, sizeof(stored));
        return ESP_ERR_NOT_FOUND;
    }

    char saved_password[sizeof(stored.sta.password) + 1U] = {};
    std::memcpy(saved_password, stored.sta.password, sizeof(stored.sta.password));
    saved_password[sizeof(stored.sta.password)] = '\0';
    error = qsee_wifi_connect(ssid, saved_password, persist);
    secure_zero(saved_password, sizeof(saved_password));
    secure_zero(&stored, sizeof(stored));
    return error;
}

// Cancel reconnect intent while keeping the station driver available.
extern "C" esp_err_t qsee_wifi_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_lock();
    const bool unavailable =
        s_sleep_suspended || s_provisioning_active ||
        s_station_operation_active || s_status.scanning;
    if (!unavailable) {
        s_station_operation_active = true;
    }
    wifi_unlock();
    if (unavailable) {
        return ESP_ERR_INVALID_STATE;
    }
    cancel_reconnect();
    wifi_lock();
    s_connect_requested = false;
    s_status.connect_requested = false;
    s_status.connected = false;
    s_status.ip_acquired = false;
    s_status.rssi = 0;
    s_status.channel = 0;
    s_status.ip_address[0] = '\0';
    s_status.netmask[0] = '\0';
    s_status.gateway[0] = '\0';
    ++s_status.event_sequence;
    s_consecutive_disconnects = 0;
    wifi_unlock();
    publish_snapshot();

    const esp_err_t error = esp_wifi_disconnect();
    wifi_lock();
    s_station_operation_active = false;
    wifi_unlock();
    if (error == ESP_ERR_WIFI_NOT_CONNECT) {
        record_error(ESP_OK);
        return ESP_OK;
    }
    record_error(error);
    return error;
}

// Start one asynchronous scan and expose its pending state immediately.
extern "C" esp_err_t qsee_wifi_start_scan(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_lock();
    if (s_sleep_suspended || s_provisioning_active ||
        s_station_operation_active) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.scanning) {
        wifi_unlock();
        return ESP_OK;
    }
    s_station_operation_active = true;
    s_status.scanning = true;
    s_status.scan_completed = false;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    wifi_scan_config_t config = {};
    config.show_hidden = true;
    const esp_err_t error = esp_wifi_scan_start(&config, false);
    wifi_lock();
    s_station_operation_active = false;
    wifi_unlock();
    if (error != ESP_OK) {
        wifi_lock();
        s_status.scanning = false;
        s_status.scan_completed = true;
        ++s_status.scan_sequence;
        wifi_unlock();
        record_error(error);
    } else {
        ESP_LOGI(TAG, "Asynchronous scan started");
    }
    return error;
}

// Checkpoint the current profile and suppress reconnects during BLE onboarding.
extern "C" esp_err_t qsee_wifi_begin_provisioning(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    const bool unavailable =
        s_sleep_suspended || s_provisioning_active ||
        s_station_operation_active || s_status.scanning;
    if (!unavailable) {
        s_station_operation_active = true;
    }
    wifi_unlock();
    if (unavailable) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t profile = {};
    const esp_err_t profile_error = esp_wifi_get_config(WIFI_IF_STA, &profile);
    if (profile_error != ESP_OK) {
        wifi_lock();
        s_station_operation_active = false;
        wifi_unlock();
        secure_zero(&profile, sizeof(profile));
        return profile_error;
    }

    wifi_lock();
    s_provisioning_profile = profile;
    s_provisioning_profile_valid = true;
    s_provisioning_connect_requested = s_connect_requested;
    s_provisioning_profile_storage = s_profile_storage;
    s_provisioning_active = true;
    s_station_operation_active = false;
    wifi_unlock();
    secure_zero(&profile, sizeof(profile));

    cancel_reconnect();
    ESP_LOGI(TAG, "Wi-Fi reconnects paused for BLE provisioning");
    return ESP_OK;
}

// Restore the checkpoint after cancellation, or keep the newly applied profile.
extern "C" esp_err_t qsee_wifi_end_provisioning(bool credentials_applied)
{
    wifi_config_t checkpoint = {};
    bool checkpoint_valid = false;
    bool reconnect_requested = false;
    bool station_started = false;
    wifi_storage_t checkpoint_storage = WIFI_STORAGE_FLASH;

    wifi_lock();
    if (!s_provisioning_active || s_station_operation_active) {
        wifi_unlock();
        return s_provisioning_active ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    s_station_operation_active = true;
    checkpoint = s_provisioning_profile;
    checkpoint_valid = s_provisioning_profile_valid;
    checkpoint_storage = s_provisioning_profile_storage;
    reconnect_requested =
        credentials_applied ? true : s_provisioning_connect_requested;
    station_started = s_status.started;
    wifi_unlock();

    cancel_reconnect();
    esp_err_t first_error = ESP_OK;
    if (!credentials_applied && checkpoint_valid) {
        const esp_err_t disconnect_error = esp_wifi_disconnect();
        if (disconnect_error != ESP_OK &&
            disconnect_error != ESP_ERR_WIFI_NOT_CONNECT) {
            first_error = disconnect_error;
        }
        if (first_error == ESP_OK) {
            first_error = esp_wifi_set_storage(checkpoint_storage);
        }
        if (first_error == ESP_OK) {
            first_error = esp_wifi_set_config(WIFI_IF_STA, &checkpoint);
        }
    }
    if (first_error != ESP_OK) {
        wifi_lock();
        s_station_operation_active = false;
        wifi_unlock();
        secure_zero(&checkpoint, sizeof(checkpoint));
        record_error(first_error);
        return first_error;
    }

    wifi_config_t active_profile = {};
    const esp_err_t active_profile_error =
        esp_wifi_get_config(WIFI_IF_STA, &active_profile);

    wifi_lock();
    secure_zero(&s_provisioning_profile, sizeof(s_provisioning_profile));
    s_provisioning_profile_valid = false;
    s_provisioning_connect_requested = false;
    s_provisioning_profile_storage = WIFI_STORAGE_FLASH;
    s_provisioning_active = false;
    s_connect_requested = reconnect_requested;
    s_status.connect_requested = reconnect_requested;
    if (credentials_applied) {
        s_profile_storage = WIFI_STORAGE_FLASH;
    } else if (first_error == ESP_OK) {
        s_profile_storage = checkpoint_storage;
    }
    if (active_profile_error == ESP_OK) {
        s_status.credentials_configured = active_profile.sta.ssid[0] != '\0';
        copy_ssid(
            s_status.ssid,
            sizeof(s_status.ssid),
            active_profile.sta.ssid,
            sizeof(active_profile.sta.ssid)
        );
    }
    ++s_status.event_sequence;
    wifi_unlock();
    secure_zero(&active_profile, sizeof(active_profile));
    secure_zero(&checkpoint, sizeof(checkpoint));
    publish_snapshot();

    esp_err_t connect_error = ESP_OK;
    if (!credentials_applied && reconnect_requested && station_started &&
        first_error == ESP_OK) {
        wifi_lock();
        ++s_status.connect_attempt_count;
        wifi_unlock();
        connect_error = esp_wifi_connect();
        if (connect_error != ESP_OK &&
            connect_error != ESP_ERR_WIFI_STATE) {
            first_error = connect_error;
        }
    }
    wifi_lock();
    s_station_operation_active = false;
    wifi_unlock();
    if (connect_error == ESP_ERR_WIFI_STATE) {
        schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
    }

    if (first_error != ESP_OK) {
        record_error(first_error);
    } else {
        ESP_LOGI(TAG, "%s",
            credentials_applied
                ? "Provisioned Wi-Fi profile accepted"
                : "Previous Wi-Fi profile restored after provisioning"
        );
    }
    return first_error;
}

// Capture Wi-Fi intent and stop only the driver needed for explicit sleep.
extern "C" esp_err_t qsee_wifi_prepare_light_sleep(
    bool *was_started,
    bool *reconnect_requested
)
{
    if (was_started == nullptr || reconnect_requested == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_lock();
    if (s_provisioning_active || s_sleep_suspended ||
        s_station_operation_active) {
        *was_started = false;
        *reconnect_requested = false;
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_station_operation_active = true;
    *was_started = s_initialized && s_status.started;
    *reconnect_requested = s_connect_requested;
    s_sleep_suspended = true;
    s_status.scanning = false;
    wifi_unlock();
    cancel_reconnect();

    if (!*was_started) {
        wifi_lock();
        s_station_operation_active = false;
        wifi_unlock();
        return ESP_OK;
    }

    const esp_err_t error = esp_wifi_stop();
    wifi_lock();
    s_station_operation_active = false;
    if (error != ESP_OK) {
        s_sleep_suspended = false;
    }
    wifi_unlock();
    if (error != ESP_OK) {
        record_error(error);
    }
    return error;
}

// Restart the station and reconnect only when that intent existed pre-sleep.
extern "C" esp_err_t qsee_wifi_restore_after_light_sleep(
    bool was_started,
    bool reconnect_requested
)
{
    if (!was_started) {
        wifi_lock();
        if (s_station_operation_active) {
            wifi_unlock();
            return ESP_ERR_INVALID_STATE;
        }
        s_sleep_suspended = false;
        wifi_unlock();
        return ESP_OK;
    }
    if (!s_initialized) {
        wifi_lock();
        s_sleep_suspended = false;
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    if (!s_sleep_suspended || s_station_operation_active) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_station_operation_active = true;
    wifi_unlock();

    esp_err_t first_error = esp_wifi_start();
    const bool driver_started = first_error == ESP_OK;
    if (driver_started) {
        const esp_err_t power_save_error =
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        if (power_save_error != ESP_OK) {
            first_error = power_save_error;
        }
    }

    wifi_lock();
    s_sleep_suspended = false;
    s_connect_requested = reconnect_requested;
    s_status.connect_requested = reconnect_requested;
    s_status.scanning = false;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    esp_err_t connect_error = ESP_OK;
    if (driver_started && reconnect_requested) {
        wifi_lock();
        ++s_status.connect_attempt_count;
        wifi_unlock();
        connect_error = esp_wifi_connect();
        if (connect_error != ESP_OK &&
            connect_error != ESP_ERR_WIFI_STATE &&
            first_error == ESP_OK) {
            first_error = connect_error;
        }
    }
    wifi_lock();
    s_station_operation_active = false;
    wifi_unlock();
    if (connect_error == ESP_ERR_WIFI_STATE) {
        schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
    }
    if (first_error != ESP_OK) {
        record_error(first_error);
    }
    return first_error;
}

extern "C" size_t qsee_wifi_get_networks(qsee_wifi_network_t *networks, size_t capacity)
{
    wifi_lock();
    const size_t count = s_network_count < capacity ? s_network_count : capacity;
    if (networks != nullptr && count > 0) {
        std::memcpy(networks, s_networks, count * sizeof(qsee_wifi_network_t));
    }
    wifi_unlock();
    return count;
}

extern "C" void qsee_wifi_get_status(qsee_wifi_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    wifi_lock();
    *status = s_status;
    wifi_unlock();
}
