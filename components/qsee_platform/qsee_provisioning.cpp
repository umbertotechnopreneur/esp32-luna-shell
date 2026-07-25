// Provisioning flow for pairing and onboarding a QSee device.
// Owns the connection handoff needed to bootstrap Wi-Fi setup.

#include "qsee_provisioning.hpp"
#include "qsee_platform.h"

#include <cstdio>
#include <cstdint>
#include <mutex>

#include "esp_log.h"
#include "esp_mac.h"
#include "qsee_device_snapshot.hpp"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

namespace {
constexpr char kTag[] = "qsee_prov";
}

namespace qsee {

ProvisioningService &ProvisioningService::instance()
{
    static ProvisioningService service;
    return service;
}

// Prepare Espressif's BLE provisioning manager without starting a session.
esp_err_t ProvisioningService::initialize()
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_stopping || _initializing || _starting) {
            return ESP_ERR_INVALID_STATE;
        }
        if (_initialized) {
            return ESP_OK;
        }
        _initializing = true;
        rebuildIdentity();
    }

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        // Keep BLE memory reusable for QSee telemetry after provisioning.
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT,
        .app_event_handler = {
            .event_cb = &ProvisioningService::onEvent,
            .user_data = this,
        },
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
    };

    const esp_err_t err = wifi_prov_mgr_init(config);
    std::lock_guard<std::mutex> guard(_mutex);
    _initializing = false;
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "manager init failed: %s", esp_err_to_name(err));
        return err;
    }

    _initialized = true;
    _credentials_succeeded = false;
    _state = ProvisioningState::Idle;
    ESP_LOGI(kTag, "BLE provisioning ready as %s", _service_name);
    return ESP_OK;
}

// Start or reuse one secure BLE provisioning session.
esp_err_t ProvisioningService::start()
{
    bool retry_failed_session = false;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_initialized || _stopping || _initializing || _starting) {
            return ESP_ERR_INVALID_STATE;
        }
        if (_transport_active) {
            if (_running && _state == ProvisioningState::Failed) {
                retry_failed_session = true;
                _starting = true;
            } else if (_running) {
                return ESP_OK;
            } else {
                return ESP_ERR_INVALID_STATE;
            }
        } else {
            rebuildIdentity();
            _credentials_succeeded = false;
            _state = ProvisioningState::Starting;
            _starting = true;
        }
    }

    if (retry_failed_session) {
        const esp_err_t retry_error =
            wifi_prov_mgr_reset_sm_state_on_failure();
        std::lock_guard<std::mutex> guard(_mutex);
        _starting = false;
        if (retry_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "provisioning retry reset failed: %s",
                esp_err_to_name(retry_error)
            );
            return retry_error;
        }
        _credentials_succeeded = false;
        _state = ProvisioningState::WaitingForApp;
        return ESP_OK;
    }

    const esp_err_t wifi_handoff_error = qsee_wifi_begin_provisioning();
    if (wifi_handoff_error != ESP_OK) {
        std::lock_guard<std::mutex> guard(_mutex);
        _starting = false;
        _state = ProvisioningState::Failed;
        return wifi_handoff_error;
    }

    const esp_err_t err = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,
        nullptr,
        _service_name,
        nullptr);
    if (err != ESP_OK) {
        (void)qsee_wifi_end_provisioning(false);
    }

    std::lock_guard<std::mutex> guard(_mutex);
    _starting = false;
    if (err != ESP_OK) {
        _state = ProvisioningState::Failed;
        ESP_LOGE(kTag, "BLE provisioning start failed: %s", esp_err_to_name(err));
        return err;
    }

    _running = true;
    _transport_active = true;
    _state = ProvisioningState::WaitingForApp;
    return ESP_OK;
}

// Deinitialize synchronously so a following retry cannot race teardown.
esp_err_t ProvisioningService::stop()
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (!_initialized) {
            _state = ProvisioningState::Stopped;
            return ESP_OK;
        }
        if (_stopping) {
            return ESP_OK;
        }
        if (_initializing || _starting) {
            return ESP_ERR_INVALID_STATE;
        }
        _stopping = true;
        _running = false;
        _state = ProvisioningState::Stopped;
    }

    // Deinit is synchronous, so a following start cannot race WIFI_PROV_END.
    wifi_prov_mgr_deinit();
    std::lock_guard<std::mutex> guard(_mutex);
    _initialized = false;
    _running = false;
    _transport_active = false;
    _stopping = false;
    _credentials_succeeded = false;
    _state = ProvisioningState::Stopped;
    return ESP_OK;
}

// Expose whether provisioning currently owns the BLE transport.
bool ProvisioningService::transportActive() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _transport_active;
}

esp_err_t ProvisioningService::isProvisioned(bool *provisioned) const
{
    if (provisioned == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> guard(_mutex);
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return wifi_prov_mgr_is_provisioned(provisioned);
}

// Format current SSID and IP state into a caller-owned bounded buffer.
esp_err_t ProvisioningService::connectionSummary(char *buffer, size_t buffer_size) const
{
    if (buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    const WifiSnapshot &wifi =
        DeviceSnapshotService::instance().snapshot().radios.wifi;
    if (!wifi.credentials_configured || wifi.ssid[0] == '\0') {
        std::snprintf(buffer, buffer_size, "Wi-Fi not configured");
        return ESP_ERR_NOT_FOUND;
    }
    if (wifi.ip_acquired && wifi.ip_address[0] != '\0') {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %s\nIP: %s",
            wifi.ssid,
            wifi.ip_address
        );
    } else {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %s\nIP pending",
            wifi.ssid
        );
    }
    return ESP_OK;
}

ProvisioningState ProvisioningService::state() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _state;
}

const char *ProvisioningService::serviceName() const
{
    return _service_name;
}

const char *ProvisioningService::qrPayload() const
{
    return _qr_payload;
}

// Forward Espressif's C callback into the owning service instance.
void ProvisioningService::onEvent(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    auto *service = static_cast<ProvisioningService *>(user_data);
    if (service != nullptr) {
        service->handleEvent(event, event_data);
    }
}

// Map Espressif callbacks into the UI-facing provisioning state machine.
void ProvisioningService::handleEvent(wifi_prov_cb_event_t event, void *event_data)
{
    (void)event_data;
    bool finish_wifi_handoff = false;
    bool credentials_applied = false;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        switch (event) {
        case WIFI_PROV_START:
            _transport_active = true;
            _state = ProvisioningState::WaitingForApp;
            break;
        case WIFI_PROV_CRED_RECV:
            _state = ProvisioningState::CredentialsReceived;
            break;
        case WIFI_PROV_SET_STA_CONFIG:
            _state = ProvisioningState::Connecting;
            break;
        case WIFI_PROV_CRED_SUCCESS:
            _credentials_succeeded = true;
            _state = ProvisioningState::Success;
            _running = false;
            break;
        case WIFI_PROV_CRED_FAIL:
            _credentials_succeeded = false;
            _state = ProvisioningState::Failed;
            break;
        case WIFI_PROV_END:
            _running = false;
            _transport_active = false;
            credentials_applied = _credentials_succeeded;
            finish_wifi_handoff = true;
            if (_state != ProvisioningState::Success) {
                _state = ProvisioningState::Stopped;
            }
            break;
        default:
            break;
        }
    }
    if (finish_wifi_handoff) {
        const esp_err_t error =
            qsee_wifi_end_provisioning(credentials_applied);
        if (error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Wi-Fi handoff completion failed: %s",
                esp_err_to_name(error)
            );
        }
    }
}

// Rebuild the nearby-device name and matching Espressif QR payload.
void ProvisioningService::rebuildIdentity()
{
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        std::snprintf(_service_name, sizeof(_service_name), "PROV_QSEE");
    } else {
        std::snprintf(_service_name, sizeof(_service_name), "PROV_QSEE-%02X%02X", mac[4], mac[5]);
    }
    // Espressif's provisioning app consumes this exact JSON QR payload.
    std::snprintf(
        _qr_payload,
        sizeof(_qr_payload),
        "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"ble\",\"security\":1}",
        _service_name);
}

}  // namespace qsee

extern "C" esp_err_t qsee_provisioning_init(void)
{
    return qsee::ProvisioningService::instance().initialize();
}

extern "C" esp_err_t qsee_provisioning_start(void)
{
    const esp_err_t wifi_error = qsee_wifi_init();
    if (wifi_error != ESP_OK) {
        return wifi_error;
    }
    const esp_err_t ble_handoff_error = qsee_ble_prepare_provisioning();
    if (ble_handoff_error != ESP_OK) {
        return ble_handoff_error;
    }
    auto &service = qsee::ProvisioningService::instance();
    const esp_err_t init_error = service.initialize();
    if (init_error != ESP_OK) {
        (void)qsee_ble_restore_after_provisioning();
        return init_error;
    }
    const esp_err_t start_error = service.start();
    if (start_error != ESP_OK) {
        (void)service.stop();
        (void)qsee_ble_restore_after_provisioning();
        return start_error;
    }
    qsee::DeviceSnapshotService::instance().reportBle(
        true,
        ESP_OK,
        false,
        false,
        0xFFFF,
        0,
        0,
        service.serviceName(),
        true,
        qsee_ble_get_enabled()
    );
    return ESP_OK;
}

extern "C" esp_err_t qsee_provisioning_stop(void)
{
    const esp_err_t stop_error =
        qsee::ProvisioningService::instance().stop();
    const esp_err_t restore_error =
        qsee_ble_restore_after_provisioning();
    return stop_error != ESP_OK ? stop_error : restore_error;
}

extern "C" bool qsee_provisioning_active(void)
{
    return qsee::ProvisioningService::instance().transportActive();
}

extern "C" const char *qsee_provisioning_service_name(void)
{
    return qsee::ProvisioningService::instance().serviceName();
}

extern "C" const char *qsee_provisioning_pop(void)
{
    return "";
}
