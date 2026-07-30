#include "qsee_provisioning.hpp"
#include "qsee_device_snapshot.hpp"
#include "qsee_platform.h"

#include <mutex>
#include <cstdio>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

namespace {
constexpr char kTag[] = "qsee_prov";

qsee_provisioning_state_t to_c_state(qsee::ProvisioningState state)
{
    switch (state) {
    case qsee::ProvisioningState::Idle:
        return QSEE_PROVISIONING_STATE_IDLE;
    case qsee::ProvisioningState::Starting:
        return QSEE_PROVISIONING_STATE_STARTING;
    case qsee::ProvisioningState::WaitingForApp:
        return QSEE_PROVISIONING_STATE_WAITING_FOR_APP;
    case qsee::ProvisioningState::CredentialsReceived:
        return QSEE_PROVISIONING_STATE_CREDENTIALS_RECEIVED;
    case qsee::ProvisioningState::Connecting:
        return QSEE_PROVISIONING_STATE_CONNECTING;
    case qsee::ProvisioningState::Success:
        return QSEE_PROVISIONING_STATE_SUCCESS;
    case qsee::ProvisioningState::Failed:
        return QSEE_PROVISIONING_STATE_FAILED;
    case qsee::ProvisioningState::Stopping:
        return QSEE_PROVISIONING_STATE_STOPPING;
    case qsee::ProvisioningState::Deinitializing:
        return QSEE_PROVISIONING_STATE_DEINITIALIZING;
    case qsee::ProvisioningState::Stopped:
        return QSEE_PROVISIONING_STATE_STOPPED;
    case qsee::ProvisioningState::Poisoned:
        return QSEE_PROVISIONING_STATE_POISONED;
    }

    ESP_LOGE(kTag, "unknown provisioning state %d", static_cast<int>(state));
    return QSEE_PROVISIONING_STATE_FAILED;
}

qsee_provisioning_failure_reason_t to_c_failure_reason(
    qsee::ProvisioningFailureReason reason
)
{
    switch (reason) {
    case qsee::ProvisioningFailureReason::None:
        return QSEE_PROVISIONING_FAILURE_NONE;
    case qsee::ProvisioningFailureReason::Authentication:
        return QSEE_PROVISIONING_FAILURE_AUTHENTICATION;
    case qsee::ProvisioningFailureReason::AccessPointNotFound:
        return QSEE_PROVISIONING_FAILURE_ACCESS_POINT_NOT_FOUND;
    case qsee::ProvisioningFailureReason::Unknown:
        return QSEE_PROVISIONING_FAILURE_UNKNOWN;
    }

    ESP_LOGE(
        kTag,
        "unknown provisioning failure reason %d",
        static_cast<int>(reason)
    );
    return QSEE_PROVISIONING_FAILURE_UNKNOWN;
}
}

namespace qsee {

ProvisioningService &ProvisioningService::instance()
{
    static ProvisioningService service;
    return service;
}

esp_err_t ProvisioningService::initialize()
{
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (_initialized) {
            return ESP_OK;
        }
        if (_operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t identity_error = rebuildIdentity();
        if (identity_error != ESP_OK) {
            _last_error = identity_error;
            _state = ProvisioningState::Failed;
            ESP_LOGE(
                kTag,
                "identity initialization failed: %s",
                esp_err_to_name(identity_error)
            );
            return identity_error;
        }
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
        _operation = Operation::Initialize;
    }

    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT,
        .app_event_handler = {
            .event_cb = &ProvisioningService::onEvent,
            .user_data = this,
        },
        .network_prov_wifi_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
    };

    // The manager invokes the application callback synchronously from init.
    // Never hold the service mutex across this call.
    const esp_err_t error = network_prov_mgr_init(config);
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            _initialized = true;
            _running = false;
            _state = ProvisioningState::Idle;
        } else {
            _initialized = false;
            _running = false;
            _state = ProvisioningState::Failed;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "manager init failed: %s", esp_err_to_name(error));
        return error;
    }

    ESP_LOGI(kTag, "BLE provisioning ready as %s", service_name);
    return ESP_OK;
}

esp_err_t ProvisioningService::start()
{
    bool restart_failed_session = false;
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (!_initialized || _operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }
        if (_running && _state != ProvisioningState::Failed) {
            return ESP_OK;
        }

        restart_failed_session = _running;
        _operation = Operation::Start;
        _failure_reason = ProvisioningFailureReason::None;
        _state = restart_failed_session
                     ? ProvisioningState::Stopping
                     : ProvisioningState::Starting;
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
    }

    if (restart_failed_session) {
        // Stop completion emits NETWORK_PROV_END synchronously or asynchronously.
        // Both manager calls must run without the service mutex held.
        network_prov_mgr_stop_provisioning();
        network_prov_mgr_wait();
        std::lock_guard<std::mutex> guard(_mutex);
        _state = ProvisioningState::Starting;
    }

    const esp_err_t error = network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_1,
        nullptr,
        service_name,
        nullptr);
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            _running = true;
            if (_state == ProvisioningState::Starting ||
                _state == ProvisioningState::Stopped) {
                _state = ProvisioningState::WaitingForApp;
            }
        } else {
            _running = false;
            _state = ProvisioningState::Failed;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "BLE provisioning start failed: %s",
            esp_err_to_name(error)
        );
    }
    return error;
}

esp_err_t ProvisioningService::stop()
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (!_initialized || _operation != Operation::None) {
            ESP_LOGE(
                kTag,
                "stop rejected outside an initialized idle lifecycle"
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (!_running) {
            _state = ProvisioningState::Stopped;
            return ESP_OK;
        }
        _operation = Operation::Stop;
        _state = ProvisioningState::Stopping;
    }

    // Stop can emit NETWORK_PROV_END; do not hold _mutex while it does.
    network_prov_mgr_stop_provisioning();
    network_prov_mgr_wait();
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = ESP_OK;
        _running = false;
        if (_state != ProvisioningState::Success) {
            _state = ProvisioningState::Stopped;
        }
    }
    return ESP_OK;
}

esp_err_t ProvisioningService::deinitializeForBleTelemetry()
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (_operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!_initialized) {
            return ESP_OK;
        }
        _operation = Operation::Deinitialize;
        _state = ProvisioningState::Deinitializing;
    }

    // Deinit synchronously emits END and DEINIT. The callbacks need _mutex.
    const esp_err_t error = network_prov_mgr_deinit();
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        _running = false;
        if (error == ESP_OK) {
            _initialized = false;
            _state = ProvisioningState::Stopped;
        } else {
            // Manager ownership is indeterminate after a failed deinit.
            // Keep telemetry blocked for the rest of this boot.
            _initialized = true;
            _poisoned = true;
            _state = ProvisioningState::Poisoned;
        }
    }

    if (error != ESP_OK) {
        qsee::DeviceSnapshotService::instance().reportBle(
            false,
            error,
            false,
            false,
            0xFFFF,
            0,
            0,
            serviceName(),
            true
        );
        ESP_LOGE(
            kTag,
            "provisioning manager deinit failed; BLE telemetry remains "
            "blocked for this boot (%s)",
            esp_err_to_name(error)
        );
        return error;
    }

    ESP_LOGI(
        kTag,
        "provisioning manager deinitialized; BLE ownership is available "
        "for telemetry"
    );
    return ESP_OK;
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
    return network_prov_mgr_is_wifi_provisioned(provisioned);
}

esp_err_t ProvisioningService::connectionSummary(char *buffer, size_t buffer_size) const
{
    if (buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    wifi_config_t config{};
    const esp_err_t wifi_err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (wifi_err != ESP_OK || config.sta.ssid[0] == '\0') {
        std::snprintf(buffer, buffer_size, "Wi-Fi is not configured");
        return wifi_err == ESP_OK ? ESP_ERR_NOT_FOUND : wifi_err;
    }
    const int ssid_length = static_cast<int>(sizeof(config.sta.ssid));

    esp_netif_ip_info_t ip_info{};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nInterface unavailable",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        ESP_LOGE(kTag, "connection summary: station netif not found");
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t ip_err = esp_netif_get_ip_info(netif, &ip_info);
    if (ip_err != ESP_OK) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nIP address unavailable",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        ESP_LOGE(
            kTag,
            "connection summary: IP query failed (%s)",
            esp_err_to_name(ip_err)
        );
        return ip_err;
    }
    if (ip_info.ip.addr == 0) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nIP address pending",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        return ESP_ERR_NOT_FOUND;
    }
    std::snprintf(
        buffer,
        buffer_size,
        "Network: %.*s\nIP: " IPSTR,
        ssid_length,
        reinterpret_cast<const char *>(config.sta.ssid),
        IP2STR(&ip_info.ip)
    );
    return ESP_OK;
}

ProvisioningState ProvisioningService::state() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _state;
}

ProvisioningFailureReason ProvisioningService::failureReason() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _failure_reason;
}

bool ProvisioningService::isInitialized() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _initialized || _operation != Operation::None;
}

bool ProvisioningService::isRunning() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _running;
}

const char *ProvisioningService::serviceName() const
{
    return _service_name;
}

const char *ProvisioningService::qrPayload() const
{
    return _qr_payload;
}

void ProvisioningService::onEvent(void *user_data, network_prov_cb_event_t event, void *event_data)
{
    auto *service = static_cast<ProvisioningService *>(user_data);
    if (service != nullptr) {
        service->handleEvent(event, event_data);
    }
}

void ProvisioningService::handleEvent(network_prov_cb_event_t event, void *event_data)
{
    bool publish_active = false;
    bool publish_stopped = false;
    bool ended_after_success = false;
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
        switch (event) {
        case NETWORK_PROV_INIT:
            break;
        case NETWORK_PROV_START:
            _running = true;
            _state = ProvisioningState::WaitingForApp;
            _failure_reason = ProvisioningFailureReason::None;
            publish_active = true;
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            _state = ProvisioningState::CredentialsReceived;
            _failure_reason = ProvisioningFailureReason::None;
            break;
        case NETWORK_PROV_SET_WIFI_STA_CONFIG:
            _state = ProvisioningState::Connecting;
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            _state = ProvisioningState::Success;
            _failure_reason = ProvisioningFailureReason::None;
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            _state = ProvisioningState::Failed;
            if (event_data == nullptr) {
                _failure_reason = ProvisioningFailureReason::Unknown;
                ESP_LOGE(
                    kTag,
                    "Wi-Fi credential failure omitted its reason"
                );
                break;
            }
            const auto reason =
                *static_cast<network_prov_wifi_sta_fail_reason_t *>(event_data);
            switch (reason) {
            case NETWORK_PROV_WIFI_STA_AUTH_ERROR:
                _failure_reason = ProvisioningFailureReason::Authentication;
                ESP_LOGE(kTag, "Wi-Fi authentication failed");
                break;
            case NETWORK_PROV_WIFI_STA_AP_NOT_FOUND:
                _failure_reason =
                    ProvisioningFailureReason::AccessPointNotFound;
                ESP_LOGE(kTag, "Wi-Fi access point was not found");
                break;
            default:
                _failure_reason = ProvisioningFailureReason::Unknown;
                ESP_LOGE(
                    kTag,
                    "Wi-Fi provisioning failed with unknown reason %d",
                    static_cast<int>(reason)
                );
                break;
            }
            break;
        }
        case NETWORK_PROV_END:
            ended_after_success = _state == ProvisioningState::Success;
            _running = false;
            if (!ended_after_success &&
                _state != ProvisioningState::Deinitializing) {
                _state = ProvisioningState::Stopped;
            }
            publish_stopped = true;
            break;
        case NETWORK_PROV_DEINIT:
            _initialized = false;
            _running = false;
            if (_operation != Operation::Deinitialize) {
                _state = ProvisioningState::Stopped;
            }
            publish_stopped = true;
            break;
        default:
            break;
        }
    }

    if (publish_active) {
        qsee::DeviceSnapshotService::instance().reportBle(
            true,
            ESP_OK,
            true,
            false,
            0xFFFF,
            0,
            0,
            service_name,
            true
        );
    }
    if (publish_stopped) {
        qsee::DeviceSnapshotService::instance().reportBle(
            false,
            ESP_ERR_INVALID_STATE,
            false,
            false,
            0xFFFF,
            0,
            0,
            service_name,
            false
        );
    }
    if (event == NETWORK_PROV_END && ended_after_success) {
        ESP_LOGI(
            kTag,
            "provisioning completed; telemetry stays off until the explicit "
            "radio handoff deinitializes the provisioning manager"
        );
    }
}

esp_err_t ProvisioningService::rebuildIdentity()
{
    uint8_t mac[6]{};
    const esp_err_t error = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (error != ESP_OK) {
        _service_name[0] = '\0';
        _qr_payload[0] = '\0';
        return error;
    }
    std::snprintf(
        _service_name,
        sizeof(_service_name),
        "PROV_DEVICE-%02X%02X",
        mac[4],
        mac[5]
    );
    std::snprintf(
        _qr_payload,
        sizeof(_qr_payload),
        "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"ble\",\"security\":1}",
        _service_name);
    return ESP_OK;
}

}  // namespace qsee

extern "C" esp_err_t qsee_provisioning_init(void)
{
    return qsee::ProvisioningService::instance().initialize();
}

extern "C" esp_err_t qsee_provisioning_start(void)
{
    return qsee::ProvisioningService::instance().start();
}

extern "C" esp_err_t qsee_provisioning_stop(void)
{
    return qsee::ProvisioningService::instance().stop();
}

extern "C" esp_err_t qsee_provisioning_deinit_for_ble(void)
{
    return qsee::ProvisioningService::instance().deinitializeForBleTelemetry();
}

extern "C" bool qsee_provisioning_initialized(void)
{
    return qsee::ProvisioningService::instance().isInitialized();
}

extern "C" bool qsee_provisioning_active(void)
{
    return qsee::ProvisioningService::instance().isRunning();
}

extern "C" qsee_provisioning_state_t qsee_provisioning_state(void)
{
    return to_c_state(qsee::ProvisioningService::instance().state());
}

extern "C" qsee_provisioning_failure_reason_t
qsee_provisioning_failure_reason(void)
{
    return to_c_failure_reason(
        qsee::ProvisioningService::instance().failureReason()
    );
}

extern "C" esp_err_t qsee_provisioning_connection_summary(
    char *buffer,
    size_t buffer_size
)
{
    return qsee::ProvisioningService::instance().connectionSummary(
        buffer,
        buffer_size
    );
}

extern "C" const char *qsee_provisioning_service_name(void)
{
    return qsee::ProvisioningService::instance().serviceName();
}

extern "C" const char *qsee_provisioning_qr_payload(void)
{
    return qsee::ProvisioningService::instance().qrPayload();
}

extern "C" const char *qsee_provisioning_pop(void)
{
    return "";
}
