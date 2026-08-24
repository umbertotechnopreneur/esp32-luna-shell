/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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
#define WIFI_STATE_LOCK_TIMEOUT_MS 1000U

namespace {

const char *TAG = "LUNASHELL_wifi";

SemaphoreHandle_t s_lock;
StaticSemaphore_t s_lock_storage;
SemaphoreHandle_t s_lifecycle_lock;
StaticSemaphore_t s_lifecycle_lock_storage;
portMUX_TYPE s_lock_init_mux = portMUX_INITIALIZER_UNLOCKED;
TimerHandle_t s_reconnect_timer;
StaticTimer_t s_reconnect_timer_storage;
esp_netif_t *s_station_netif;
esp_event_handler_instance_t s_wifi_event_instance;
esp_event_handler_instance_t s_ip_event_instance;
bool s_initialized;
bool s_driver_initialized;
bool s_driver_started;
esp_err_t s_initialization_result = ESP_ERR_INVALID_STATE;
bool s_scan_watch_active;
bool s_connect_requested;
uint8_t s_consecutive_disconnects;
LUNASHELL_wifi_status_t s_status;
// Scan results are CPU-copied and never used by the Wi-Fi DMA/ISR path.
EXT_RAM_BSS_ATTR LUNASHELL_wifi_network_t s_networks[LUNASHELL_WIFI_MAX_NETWORKS];
EXT_RAM_BSS_ATTR wifi_ap_record_t s_scan_records[LUNASHELL_WIFI_MAX_NETWORKS];
size_t s_network_count;

void log_scan_heap_checkpoint(const char *stage)
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
        "Wi-Fi heap %s: internal free=%lu min=%lu largest=%lu; "
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

SemaphoreHandle_t ensure_wifi_state_lock()
{
    portENTER_CRITICAL(&s_lock_init_mux);
    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    SemaphoreHandle_t lock = s_lock;
    portEXIT_CRITICAL(&s_lock_init_mux);
    return lock;
}

SemaphoreHandle_t ensure_wifi_lifecycle_lock()
{
    portENTER_CRITICAL(&s_lock_init_mux);
    if (s_lifecycle_lock == nullptr) {
        s_lifecycle_lock =
            xSemaphoreCreateMutexStatic(&s_lifecycle_lock_storage);
    }
    SemaphoreHandle_t lock = s_lifecycle_lock;
    portEXIT_CRITICAL(&s_lock_init_mux);
    return lock;
}

esp_err_t wifi_lifecycle_lock(const char *operation)
{
    SemaphoreHandle_t lock = ensure_wifi_lifecycle_lock();
    if (lock == nullptr) {
        ESP_LOGE(TAG, "Wi-Fi %s lock creation failed", operation);
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(
            lock,
            pdMS_TO_TICKS(WIFI_STATE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "Wi-Fi %s rejected: lifecycle busy for %u ms",
            operation,
            WIFI_STATE_LOCK_TIMEOUT_MS
        );
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t wifi_lifecycle_try_lock(const char *operation)
{
    SemaphoreHandle_t lock = ensure_wifi_lifecycle_lock();
    if (lock == nullptr) {
        ESP_LOGE(TAG, "Wi-Fi %s lock creation failed", operation);
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(lock, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Wi-Fi %s deferred: lifecycle is busy", operation);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void wifi_lifecycle_unlock()
{
    if (s_lifecycle_lock == nullptr ||
        xSemaphoreGive(s_lifecycle_lock) != pdTRUE) {
        ESP_LOGE(TAG, "Wi-Fi lifecycle lock release invariant failed");
        std::abort();
    }
}

class WifiLifecycleGuard final {
public:
    ~WifiLifecycleGuard()
    {
        wifi_lifecycle_unlock();
    }

    WifiLifecycleGuard() = default;
    WifiLifecycleGuard(const WifiLifecycleGuard &) = delete;
    WifiLifecycleGuard &operator=(const WifiLifecycleGuard &) = delete;
};

class WifiEventWatch final {
public:
    WifiEventWatch()
    {
        const esp_err_t result = LUNASHELL_liveness_watch_begin(
            LUNASHELL_LIVENESS_WIFI_EVENTS
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Wi-Fi event watchdog start failed (%s)",
                esp_err_to_name(result)
            );
            std::abort();
        }
    }

    ~WifiEventWatch()
    {
        const esp_err_t result = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_WIFI_EVENTS,
            ESP_OK
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Wi-Fi event watchdog stop failed (%s)",
                esp_err_to_name(result)
            );
            std::abort();
        }
    }

    WifiEventWatch(const WifiEventWatch &) = delete;
    WifiEventWatch &operator=(const WifiEventWatch &) = delete;
};

void wifi_lock()
{
    SemaphoreHandle_t lock = ensure_wifi_state_lock();
    if (lock == nullptr) {
        ESP_LOGE(TAG, "Wi-Fi state lock creation failed");
        std::abort();
    }
    if (xSemaphoreTake(
            lock,
            pdMS_TO_TICKS(WIFI_STATE_LOCK_TIMEOUT_MS)
        ) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "Wi-Fi state lock timed out after %u ms",
            WIFI_STATE_LOCK_TIMEOUT_MS
        );
        std::abort();
    }
}

void wifi_unlock()
{
    if (xSemaphoreGive(s_lock) != pdTRUE) {
        ESP_LOGE(TAG, "Wi-Fi state lock release invariant failed");
        std::abort();
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

void publish_snapshot()
{
    LUNASHELL_wifi_status_t status = {};
    wifi_lock();
    status = s_status;
    wifi_unlock();

    LUNASHELL::WifiSnapshot snapshot;
    snapshot.health = status.initialized && status.started
                          ? LUNASHELL::DeviceHealth::Ready
                          : (status.last_error == ESP_ERR_NOT_FOUND
                                 ? LUNASHELL::DeviceHealth::Missing
                                 : LUNASHELL::DeviceHealth::Degraded);
    snapshot.last_error = status.last_error;
    snapshot.started = status.started;
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
    strlcpy(snapshot.ssid, status.ssid, sizeof(snapshot.ssid));
    strlcpy(snapshot.ip_address, status.ip_address, sizeof(snapshot.ip_address));
    strlcpy(snapshot.mac_address, status.mac_address, sizeof(snapshot.mac_address));
    LUNASHELL::DeviceSnapshotService::instance().reportWifi(snapshot);
}

void record_error(esp_err_t error)
{
    wifi_lock();
    s_status.last_error = error;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
}

esp_err_t finish_scan_watch_if_active(esp_err_t result)
{
    wifi_lock();
    const bool active = s_scan_watch_active;
    s_scan_watch_active = false;
    wifi_unlock();
    if (!active) {
        return ESP_OK;
    }
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_WIFI_SCAN,
        result
    );
}

[[noreturn]] void abort_incomplete_cleanup(
    const char *operation,
    esp_err_t error
)
{
    ESP_LOGE(
        TAG,
        "%s failed; Wi-Fi ownership is indeterminate (%s)",
        operation,
        esp_err_to_name(error)
    );
    wifi_lock();
    s_initialized = false;
    s_status.initialized = false;
    s_status.started = s_driver_started;
    s_status.last_error = error;
    s_initialization_result = error;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
    std::abort();
}

esp_err_t cancel_reconnect(
    bool clear_connection_intent,
    bool restore_connection_intent_on_failure
)
{
    wifi_lock();
    const bool previous_connect_requested = s_connect_requested;
    const bool previous_status_connect_requested = s_status.connect_requested;
    if (clear_connection_intent) {
        // Intent and timer cancellation share the status lock, so a stale
        // disconnect event cannot observe the old intent after cancellation.
        s_connect_requested = false;
        s_status.connect_requested = false;
    }
    const bool timer_available = s_reconnect_timer != nullptr;
    const bool command_accepted = timer_available &&
        xTimerStop(s_reconnect_timer, 0) == pdPASS;
    if (command_accepted) {
        s_status.reconnect_pending = false;
        s_status.reconnect_delay_ms = 0;
    } else {
        if (clear_connection_intent &&
            restore_connection_intent_on_failure) {
            s_connect_requested = previous_connect_requested;
            s_status.connect_requested = previous_status_connect_requested;
        }
        s_status.last_error = timer_available
            ? ESP_ERR_TIMEOUT
            : ESP_ERR_INVALID_STATE;
    }
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
    if (!command_accepted) {
        const esp_err_t error = timer_available
            ? ESP_ERR_TIMEOUT
            : ESP_ERR_INVALID_STATE;
        if (timer_available) {
            ESP_LOGE(
                TAG,
                "Reconnect cancellation failed: timer queue full (%s)",
                esp_err_to_name(error)
            );
        } else {
            ESP_LOGE(
                TAG,
                "Reconnect cancellation failed: timer unavailable (%s)",
                esp_err_to_name(error)
            );
        }
        return error;
    }
    return ESP_OK;
}

esp_err_t schedule_reconnect(uint32_t delay_ms)
{
    if (s_reconnect_timer == nullptr) {
        ESP_LOGE(TAG, "Scheduling Wi-Fi reconnect failed: timer unavailable");
        wifi_lock();
        s_status.last_error = ESP_ERR_INVALID_STATE;
        ++s_status.event_sequence;
        wifi_unlock();
        publish_snapshot();
        return ESP_ERR_INVALID_STATE;
    }
    if (delay_ms < WIFI_RECONNECT_INITIAL_MS) {
        delay_ms = WIFI_RECONNECT_INITIAL_MS;
    } else if (delay_ms > WIFI_RECONNECT_MAX_MS) {
        delay_ms = WIFI_RECONNECT_MAX_MS;
    }

    wifi_lock();
    if (!s_initialized || !s_status.started || !s_connect_requested) {
        // A disconnect event may have captured an older intent before an
        // explicit disconnect/stop transaction cancelled it. Revalidate under
        // the same lock as the timer command and treat the newer intent as the
        // authoritative no-reconnect result.
        s_status.reconnect_pending = false;
        s_status.reconnect_delay_ms = 0;
        ++s_status.event_sequence;
        wifi_unlock();
        publish_snapshot();
        ESP_LOGI(
            TAG,
            "Reconnect scheduling skipped: connection intent is inactive"
        );
        return ESP_OK;
    }
    // Publish the pending reconnect only after the timer daemon has accepted
    // the command. On failure, an already-active reconnect (if any) remains
    // represented by its previous delay instead of exposing an unscheduled one.
    const bool command_accepted = xTimerChangePeriod(
        s_reconnect_timer,
        pdMS_TO_TICKS(delay_ms),
        0
    ) == pdPASS;
    if (command_accepted) {
        s_status.reconnect_pending = true;
        s_status.reconnect_delay_ms = delay_ms;
    } else {
        s_status.last_error = ESP_ERR_TIMEOUT;
    }
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
    if (!command_accepted) {
        ESP_LOGE(TAG, "Scheduling Wi-Fi reconnect failed: timer queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void reconnect_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    const esp_err_t lifecycle_error =
        wifi_lifecycle_try_lock("reconnect callback");
    if (lifecycle_error != ESP_OK) {
        wifi_lock();
        const bool retry =
            s_initialized && s_status.started && s_connect_requested;
        // A one-shot timer is no longer pending once its callback begins.
        // Clear it before rescheduling so a queue failure cannot leave a false
        // pending latch behind.
        s_status.reconnect_pending = false;
        s_status.reconnect_delay_ms = 0;
        ++s_status.event_sequence;
        wifi_unlock();
        if (retry) {
            const esp_err_t schedule_error =
                schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
            if (schedule_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Deferred reconnect could not be rescheduled (%s)",
                    esp_err_to_name(schedule_error)
                );
            }
        } else {
            publish_snapshot();
        }
        return;
    }
    WifiLifecycleGuard lifecycle_guard;

    // State may have changed while this timer was pending. Recheck only after
    // acquiring the lifecycle gate, before calling into the Wi-Fi driver.
    wifi_lock();
    const bool should_connect = s_initialized && s_status.started && s_connect_requested;
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
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_WIFI
    );
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Wi-Fi reconnect watchdog start failed (%s)",
            esp_err_to_name(watch_error)
        );
        std::abort();
    }
    const esp_err_t error = esp_wifi_connect();
    if (error == ESP_ERR_WIFI_STATE) {
        record_error(error);
        const esp_err_t schedule_error =
            schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
        if (schedule_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Reconnect retry could not be scheduled (%s)",
                esp_err_to_name(schedule_error)
            );
        }
    } else {
        record_error(error);
    }
    (void)LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_WIFI,
        error
    );
}

void handle_scan_done()
{
    uint16_t count = LUNASHELL_WIFI_MAX_NETWORKS;
    std::memset(s_scan_records, 0, sizeof(s_scan_records));
    const esp_err_t error = esp_wifi_scan_get_ap_records(&count, s_scan_records);

    wifi_lock();
    ++s_status.event_sequence;
    if (error == ESP_OK) {
        s_network_count = count;
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
        s_status.last_error = error;
    }
    wifi_unlock();

    // Keep the scan unavailable to a new caller until its cross-task watchdog
    // user has been closed. The handler's WIFI_EVENTS watch remains active
    // until this callback exits.
    (void)finish_scan_watch_if_active(error);
    wifi_lock();
    s_status.scanning = false;
    wifi_unlock();
    publish_snapshot();

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Scan complete: %u access point(s)", static_cast<unsigned>(count));
    } else {
        ESP_LOGW(TAG, "Scan result failed: %s", esp_err_to_name(error));
    }
    log_scan_heap_checkpoint("after-scan");
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    WifiEventWatch event_watch;

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
            const esp_err_t event_error =
                connected != nullptr ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            wifi_lock();
            s_status.connected = true;
            s_status.ip_acquired = false;
            s_status.channel = connected != nullptr ? connected->channel : 0;
            s_status.last_error = event_error;
            ++s_status.event_sequence;
            wifi_unlock();
            publish_snapshot();
            if (event_error != ESP_OK) {
                ESP_LOGE(TAG, "Connected event had no payload");
            }
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
            reconnect = s_connect_requested;
            s_status.last_error =
                disconnected == nullptr
                    ? ESP_ERR_INVALID_RESPONSE
                    : (reconnect ? ESP_ERR_WIFI_CONN : ESP_OK);
            ++s_status.event_sequence;
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
                const esp_err_t schedule_error =
                    schedule_reconnect(delay_ms);
                if (schedule_error != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "Disconnect recovery could not be scheduled (%s)",
                        esp_err_to_name(schedule_error)
                    );
                }
            }
            break;
        }

        case WIFI_EVENT_SCAN_DONE:
            handle_scan_done();
            break;

        case WIFI_EVENT_STA_STOP: {
            const esp_err_t cancel_error = cancel_reconnect(true, false);
            wifi_lock();
            s_status.started = false;
            s_status.connected = false;
            s_status.ip_acquired = false;
            s_status.last_error =
                cancel_error == ESP_OK
                    ? ESP_ERR_INVALID_STATE
                    : cancel_error;
            ++s_status.event_sequence;
            wifi_unlock();
            publish_snapshot();
            ESP_LOGI(TAG, "Station driver stopped");
            break;
        }

        default:
            break;
        }
        return;
    }

    if (event_base != IP_EVENT) {
        return;
    }
    if (event_id == IP_EVENT_STA_GOT_IP) {
        wifi_lock();
        const bool ip_expected =
            s_initialized && s_status.started && s_connect_requested;
        wifi_unlock();
        if (!ip_expected) {
            ESP_LOGW(TAG, "Ignoring stale IP event without connection intent");
            return;
        }

        const auto *got_ip = static_cast<const ip_event_got_ip_t *>(event_data);
        wifi_ap_record_t access_point = {};
        const esp_err_t access_point_error =
            esp_wifi_sta_get_ap_info(&access_point);
        esp_err_t event_error =
            got_ip == nullptr
                ? ESP_ERR_INVALID_RESPONSE
                : access_point_error;
        const esp_err_t cancel_error = cancel_reconnect(false, false);
        if (event_error == ESP_OK && cancel_error != ESP_OK) {
            event_error = cancel_error;
        }

        wifi_lock();
        if (!s_initialized || !s_status.started || !s_connect_requested) {
            wifi_unlock();
            ESP_LOGW(
                TAG,
                "Ignoring IP event after connection intent changed"
            );
            return;
        }
        s_status.connected = true;
        s_status.ip_acquired = got_ip != nullptr;
        s_status.credentials_configured = true;
        s_status.connect_requested = true;
        if (access_point_error == ESP_OK) {
            s_status.rssi = access_point.rssi;
            s_status.channel = access_point.primary;
            copy_ssid(
                s_status.ssid,
                sizeof(s_status.ssid),
                access_point.ssid,
                sizeof(access_point.ssid)
            );
        }
        if (cancel_error == ESP_OK) {
            s_status.reconnect_pending = false;
            s_status.reconnect_delay_ms = 0;
        }
        s_status.last_disconnect_reason = 0;
        s_status.last_error = event_error;
        if (event_error == ESP_OK) {
            s_initialization_result = ESP_OK;
        }
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
        ++s_status.event_sequence;
        s_consecutive_disconnects = 0;
        wifi_unlock();
        publish_snapshot();
        if (event_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Got-IP event incomplete (%s)",
                esp_err_to_name(event_error)
            );
            return;
        }
        ESP_LOGI(
            TAG,
            "Online: SSID=%s IP=%s RSSI=%d dBm",
            s_status.ssid,
            s_status.ip_address,
            s_status.rssi
        );
        const esp_err_t time_error = LUNASHELL_time_notify_wifi_online();
        if (time_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Starting SNTP after DHCP failed (%s)",
                esp_err_to_name(time_error)
            );
        }
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        wifi_lock();
        s_status.ip_acquired = false;
        s_status.ip_address[0] = '\0';
        s_status.netmask[0] = '\0';
        s_status.gateway[0] = '\0';
        s_status.last_error =
            s_connect_requested ? ESP_ERR_WIFI_CONN : ESP_OK;
        ++s_status.event_sequence;
        wifi_unlock();
        publish_snapshot();
        ESP_LOGW(TAG, "DHCP lease lost");
    }
}

esp_err_t initialize_nvs()
{
    const esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS initialization failed; explicit recovery required (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}

}  // namespace

static esp_err_t wifi_init_impl(void)
{
    if (s_initialized) {
        wifi_lock();
        const esp_err_t result = s_initialization_result;
        wifi_unlock();
        return result;
    }
    if (s_ip_event_instance != nullptr ||
        s_wifi_event_instance != nullptr ||
        s_driver_initialized ||
        s_driver_started ||
        s_station_netif != nullptr) {
        const esp_err_t residual_error =
            s_initialization_result != ESP_OK
                ? s_initialization_result
                : ESP_ERR_INVALID_STATE;
        ESP_LOGE(
            TAG,
            "Wi-Fi initialization retry rejected after incomplete cleanup "
            "(%s)",
            esp_err_to_name(residual_error)
        );
        return residual_error;
    }

    if (ensure_wifi_state_lock() == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (s_reconnect_timer == nullptr) {
        s_reconnect_timer = xTimerCreateStatic(
            "LUNASHELL_wifi_retry",
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
    s_driver_started = true;
    error = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (error != ESP_OK) {
        goto fail;
    }
    ESP_LOGI(TAG, "Wi-Fi power save: native minimum-modem mode enabled");

    {
        uint8_t mac[6] = {};
        wifi_config_t stored = {};
        error = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Reading Wi-Fi MAC failed (%s)", esp_err_to_name(error));
            goto fail;
        }
        std::snprintf(
            s_status.mac_address,
            sizeof(s_status.mac_address),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        );
        const esp_err_t stored_result = esp_wifi_get_config(WIFI_IF_STA, &stored);
        if (stored_result != ESP_OK) {
            error = stored_result;
            ESP_LOGE(
                TAG,
                "Reading persisted Wi-Fi profile failed (%s)",
                esp_err_to_name(error)
            );
            goto fail;
        }
        const bool has_profile = stored.sta.ssid[0] != '\0';

        wifi_lock();
        s_initialized = true;
        s_status.initialized = true;
        s_status.started = true;
        s_status.credentials_configured = has_profile;
        s_status.connect_requested = has_profile;
        s_connect_requested = has_profile;
        s_initialization_result = ESP_OK;
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
            if (error != ESP_OK) {
                wifi_lock();
                s_initialization_result = error;
                wifi_unlock();
                record_error(error);
                return error;
            }
        }
    }
    return ESP_OK;

fail:
    if (s_driver_started) {
        const esp_err_t cleanup_error = esp_wifi_stop();
        if (cleanup_error != ESP_OK) {
            abort_incomplete_cleanup("Wi-Fi driver stop", cleanup_error);
        }
        s_driver_started = false;
    }
    // A successful driver stop is an explicit cancellation boundary for an
    // asynchronous scan. SCAN_DONE may already have claimed the watch; the
    // helper makes ownership single-use across the stop task and event loop.
    const esp_err_t scan_watch_error = finish_scan_watch_if_active(ESP_OK);
    if (scan_watch_error != ESP_OK) {
        abort_incomplete_cleanup("Wi-Fi scan watchdog cleanup", scan_watch_error);
    }
    if (s_ip_event_instance != nullptr) {
        const esp_err_t cleanup_error =
            esp_event_handler_instance_unregister(
                IP_EVENT,
                ESP_EVENT_ANY_ID,
                s_ip_event_instance
            );
        if (cleanup_error == ESP_OK) {
            s_ip_event_instance = nullptr;
        } else {
            abort_incomplete_cleanup(
                "IP event handler cleanup",
                cleanup_error
            );
        }
    }
    if (s_wifi_event_instance != nullptr) {
        const esp_err_t cleanup_error =
            esp_event_handler_instance_unregister(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                s_wifi_event_instance
            );
        if (cleanup_error == ESP_OK) {
            s_wifi_event_instance = nullptr;
        } else {
            abort_incomplete_cleanup(
                "Wi-Fi event handler cleanup",
                cleanup_error
            );
        }
    }
    if (s_driver_initialized) {
        const esp_err_t cleanup_error = esp_wifi_deinit();
        if (cleanup_error == ESP_OK) {
            s_driver_initialized = false;
        } else {
            abort_incomplete_cleanup(
                "Wi-Fi driver cleanup",
                cleanup_error
            );
        }
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
    s_initialization_result = error;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();
    ESP_LOGE(TAG, "Station lifecycle failed: %s", esp_err_to_name(error));
    return error;
}

static esp_err_t wifi_stop_impl(void)
{
    if (!s_initialized &&
        s_ip_event_instance == nullptr &&
        s_wifi_event_instance == nullptr &&
        !s_driver_initialized &&
        !s_driver_started &&
        s_station_netif == nullptr) {
        return ESP_OK;
    }

    const esp_err_t reconnect_error = cancel_reconnect(true, true);
    if (reconnect_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Wi-Fi stop rejected because reconnect cancellation failed (%s)",
            esp_err_to_name(reconnect_error)
        );
        record_error(reconnect_error);
        return reconnect_error;
    }

    if (s_driver_started) {
        const esp_err_t cleanup_error = esp_wifi_stop();
        if (cleanup_error != ESP_OK) {
            abort_incomplete_cleanup("Wi-Fi driver stop", cleanup_error);
        }
        s_driver_started = false;
    }
    // A successful driver stop is an explicit cancellation boundary for an
    // asynchronous scan. SCAN_DONE may already have claimed the watch; the
    // helper makes ownership single-use across the stop task and event loop.
    const esp_err_t scan_watch_error = finish_scan_watch_if_active(ESP_OK);
    if (scan_watch_error != ESP_OK) {
        abort_incomplete_cleanup("Wi-Fi scan watchdog cleanup", scan_watch_error);
    }
    if (s_ip_event_instance != nullptr) {
        const esp_err_t cleanup_error =
            esp_event_handler_instance_unregister(
                IP_EVENT,
                ESP_EVENT_ANY_ID,
                s_ip_event_instance
            );
        if (cleanup_error != ESP_OK) {
            abort_incomplete_cleanup(
                "IP event handler cleanup",
                cleanup_error
            );
        }
        s_ip_event_instance = nullptr;
    }
    if (s_wifi_event_instance != nullptr) {
        const esp_err_t cleanup_error =
            esp_event_handler_instance_unregister(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                s_wifi_event_instance
            );
        if (cleanup_error != ESP_OK) {
            abort_incomplete_cleanup(
                "Wi-Fi event handler cleanup",
                cleanup_error
            );
        }
        s_wifi_event_instance = nullptr;
    }
    if (s_driver_initialized) {
        const esp_err_t cleanup_error = esp_wifi_deinit();
        if (cleanup_error != ESP_OK) {
            abort_incomplete_cleanup("Wi-Fi driver cleanup", cleanup_error);
        }
        s_driver_initialized = false;
    }
    if (s_station_netif != nullptr) {
        esp_netif_destroy_default_wifi(s_station_netif);
        s_station_netif = nullptr;
    }

    wifi_lock();
    const uint32_t next_sequence = s_status.event_sequence + 1U;
    std::memset(&s_status, 0, sizeof(s_status));
    s_status.last_error = ESP_ERR_INVALID_STATE;
    s_status.event_sequence = next_sequence;
    s_initialized = false;
    s_initialization_result = ESP_ERR_INVALID_STATE;
    s_network_count = 0;
    wifi_unlock();
    publish_snapshot();
    ESP_LOGI(TAG, "Wi-Fi station lifecycle stopped by explicit user action");
    return ESP_OK;
}

static esp_err_t wifi_connect_impl(
    const char *ssid,
    const char *password,
    bool persist
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == nullptr || ssid[0] == '\0' || std::strlen(ssid) > LUNASHELL_WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *safe_password = password != nullptr ? password : "";
    if (std::strlen(safe_password) > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {};
    strlcpy(reinterpret_cast<char *>(config.sta.ssid), ssid, sizeof(config.sta.ssid));
    strlcpy(reinterpret_cast<char *>(config.sta.password), safe_password, sizeof(config.sta.password));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = safe_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_err_t error = cancel_reconnect(false, false);
    if (error != ESP_OK) {
        record_error(error);
        return error;
    }
    error = esp_wifi_set_storage(
        persist ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM
    );
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (error != ESP_OK) {
        record_error(error);
        return error;
    }

    wifi_lock();
    s_connect_requested = true;
    s_status.credentials_configured = true;
    s_status.connect_requested = true;
    s_status.connected = false;
    s_status.ip_acquired = false;
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    ++s_status.connect_attempt_count;
    s_status.last_error = ESP_OK;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    ESP_LOGI(TAG, "Connecting to SSID=%s, persistence=%s", ssid, persist ? "NVS" : "RAM");
    error = esp_wifi_connect();
    if (error == ESP_ERR_WIFI_STATE) {
        record_error(error);
        const esp_err_t schedule_error =
            schedule_reconnect(WIFI_RECONNECT_INITIAL_MS);
        if (schedule_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Initial retry could not be scheduled (%s)",
                esp_err_to_name(schedule_error)
            );
        }
        return error;
    }
    record_error(error);
    return error;
}

static esp_err_t wifi_disconnect_impl(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t aggregate = cancel_reconnect(true, false);
    wifi_lock();
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

    const esp_err_t disconnect_error = esp_wifi_disconnect();
    if (aggregate == ESP_OK && disconnect_error != ESP_OK) {
        aggregate = disconnect_error;
    }
    record_error(aggregate);
    return aggregate;
}

static esp_err_t wifi_start_scan_impl(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_lock();
    if (s_status.scanning) {
        wifi_unlock();
        ESP_LOGE(TAG, "Wi-Fi scan rejected: a scan is already active");
        record_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.scanning = true;
    ++s_status.event_sequence;
    wifi_unlock();
    publish_snapshot();

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_WIFI_SCAN
    );
    if (watch_error != ESP_OK) {
        wifi_lock();
        s_status.scanning = false;
        wifi_unlock();
        record_error(watch_error);
        return watch_error;
    }
    wifi_lock();
    s_scan_watch_active = true;
    wifi_unlock();

    wifi_scan_config_t config = {};
    config.show_hidden = true;
    log_scan_heap_checkpoint("before-scan");
    const esp_err_t error = esp_wifi_scan_start(&config, false);
    if (error != ESP_OK) {
        (void)finish_scan_watch_if_active(error);
        wifi_lock();
        s_status.scanning = false;
        wifi_unlock();
        record_error(error);
    } else {
        ESP_LOGI(TAG, "Asynchronous scan started");
    }
    log_scan_heap_checkpoint(
        error == ESP_OK ? "scan-started" : "scan-start-failed"
    );
    return error;
}

extern "C" size_t LUNASHELL_wifi_get_networks(LUNASHELL_wifi_network_t *networks, size_t capacity)
{
    wifi_lock();
    const size_t count = s_network_count < capacity ? s_network_count : capacity;
    if (networks != nullptr && count > 0) {
        std::memcpy(networks, s_networks, count * sizeof(LUNASHELL_wifi_network_t));
    }
    wifi_unlock();
    return count;
}

extern "C" void LUNASHELL_wifi_get_status(LUNASHELL_wifi_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    wifi_lock();
    *status = s_status;
    wifi_unlock();
}

static esp_err_t run_watched_wifi_operation(
    const char *operation_name,
    esp_err_t (*operation)(void)
)
{
    const esp_err_t lifecycle_error = wifi_lifecycle_lock(operation_name);
    if (lifecycle_error != ESP_OK) {
        return lifecycle_error;
    }
    WifiLifecycleGuard lifecycle_guard;

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_WIFI
    );
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Wi-Fi %s watchdog start failed (%s)",
            operation_name,
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }
    const esp_err_t operation_result = operation();
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_WIFI,
        operation_result
    );
}

extern "C" esp_err_t LUNASHELL_wifi_init(void)
{
    return run_watched_wifi_operation("initialization", wifi_init_impl);
}

extern "C" esp_err_t LUNASHELL_wifi_stop(void)
{
    return run_watched_wifi_operation("stop", wifi_stop_impl);
}

extern "C" esp_err_t LUNASHELL_wifi_connect(
    const char *ssid,
    const char *password,
    bool persist
)
{
    const esp_err_t lifecycle_error = wifi_lifecycle_lock("connect");
    if (lifecycle_error != ESP_OK) {
        return lifecycle_error;
    }
    WifiLifecycleGuard lifecycle_guard;

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_WIFI
    );
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Wi-Fi connect watchdog start failed (%s)",
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }
    const esp_err_t operation_result =
        wifi_connect_impl(ssid, password, persist);
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_WIFI,
        operation_result
    );
}

extern "C" esp_err_t LUNASHELL_wifi_disconnect(void)
{
    return run_watched_wifi_operation("disconnect", wifi_disconnect_impl);
}

extern "C" esp_err_t LUNASHELL_wifi_start_scan(void)
{
    return run_watched_wifi_operation("scan", wifi_start_scan_impl);
}
