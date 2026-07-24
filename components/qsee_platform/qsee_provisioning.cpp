#include "qsee_provisioning.hpp"

#include <mutex>
#include <cstdio>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
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

esp_err_t ProvisioningService::initialize()
{
    std::lock_guard<std::mutex> guard(_mutex);
    if (_initialized) {
        return ESP_OK;
    }

    rebuildIdentity();

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = {
            .event_cb = &ProvisioningService::onEvent,
            .user_data = this,
        },
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
    };

    const esp_err_t err = wifi_prov_mgr_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "manager init failed: %s", esp_err_to_name(err));
        return err;
    }

    _initialized = true;
    _state = ProvisioningState::Idle;
    ESP_LOGI(kTag, "BLE provisioning ready as %s", _service_name);
    return ESP_OK;
}

esp_err_t ProvisioningService::start()
{
    std::lock_guard<std::mutex> guard(_mutex);
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (_running && _state != ProvisioningState::Failed) {
        return ESP_OK;
    }
    if (_running) {
        wifi_prov_mgr_stop_provisioning();
        _running = false;
    }

    rebuildIdentity();
    _state = ProvisioningState::Starting;
    const esp_err_t err = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,
        nullptr,
        _service_name,
        nullptr);
    if (err != ESP_OK) {
        _state = ProvisioningState::Failed;
        ESP_LOGE(kTag, "BLE provisioning start failed: %s", esp_err_to_name(err));
        return err;
    }

    _running = true;
    _state = ProvisioningState::WaitingForApp;
    return ESP_OK;
}

esp_err_t ProvisioningService::stop()
{
    std::lock_guard<std::mutex> guard(_mutex);
    if (!_initialized || !_running) {
        _state = ProvisioningState::Stopped;
        return ESP_OK;
    }

    wifi_prov_mgr_stop_provisioning();
    _running = false;
    _state = ProvisioningState::Stopped;
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
    return wifi_prov_mgr_is_provisioned(provisioned);
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
        std::snprintf(buffer, buffer_size, "Wi-Fi non configurato");
        return wifi_err == ESP_OK ? ESP_ERR_NOT_FOUND : wifi_err;
    }

    esp_netif_ip_info_t ip_info{};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const esp_err_t ip_err = netif == nullptr ? ESP_ERR_NOT_FOUND : esp_netif_get_ip_info(netif, &ip_info);
    if (ip_err == ESP_OK && ip_info.ip.addr != 0) {
        std::snprintf(
            buffer,
            buffer_size,
            "Rete: %s\\nIP: " IPSTR,
            reinterpret_cast<const char *>(config.sta.ssid),
            IP2STR(&ip_info.ip));
    } else {
        std::snprintf(buffer, buffer_size, "Rete: %s\\nIP in assegnazione", reinterpret_cast<const char *>(config.sta.ssid));
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

void ProvisioningService::onEvent(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    auto *service = static_cast<ProvisioningService *>(user_data);
    if (service != nullptr) {
        service->handleEvent(event, event_data);
    }
}

void ProvisioningService::handleEvent(wifi_prov_cb_event_t event, void *event_data)
{
    (void)event_data;
    std::lock_guard<std::mutex> guard(_mutex);
    switch (event) {
    case WIFI_PROV_START:
        _state = ProvisioningState::WaitingForApp;
        break;
    case WIFI_PROV_CRED_RECV:
        _state = ProvisioningState::CredentialsReceived;
        break;
    case WIFI_PROV_SET_STA_CONFIG:
        _state = ProvisioningState::Connecting;
        break;
    case WIFI_PROV_CRED_SUCCESS:
        _state = ProvisioningState::Success;
        _running = false;
        break;
    case WIFI_PROV_CRED_FAIL:
        _state = ProvisioningState::Failed;
        break;
    case WIFI_PROV_END:
        _running = false;
        if (_state != ProvisioningState::Success) {
            _state = ProvisioningState::Stopped;
        }
        break;
    default:
        break;
    }
}

void ProvisioningService::rebuildIdentity()
{
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        std::snprintf(_service_name, sizeof(_service_name), "PROV_QSEE");
    } else {
        std::snprintf(_service_name, sizeof(_service_name), "PROV_QSEE-%02X%02X", mac[4], mac[5]);
    }
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
    return qsee::ProvisioningService::instance().start();
}

extern "C" esp_err_t qsee_provisioning_stop(void)
{
    return qsee::ProvisioningService::instance().stop();
}

extern "C" bool qsee_provisioning_active(void)
{
    const qsee::ProvisioningState state = qsee::ProvisioningService::instance().state();
    return state != qsee::ProvisioningState::Idle && state != qsee::ProvisioningState::Stopped;
}

extern "C" const char *qsee_provisioning_service_name(void)
{
    return qsee::ProvisioningService::instance().serviceName();
}

extern "C" const char *qsee_provisioning_pop(void)
{
    return "";
}
