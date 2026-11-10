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


#include <cstring>

#include "LUNASHELL_platform.h"
#include "LUNASHELL_wifi_provisioning.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace {

void fill_disabled_status(LUNASHELL_wifi_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    std::memset(status, 0, sizeof(*status));
    status->last_error = ESP_ERR_NOT_SUPPORTED;
}

}  // namespace

extern "C" esp_err_t LUNASHELL_wifi_init(void)
{
    LUNASHELL::WifiSnapshot snapshot{};
    snapshot.last_error = ESP_ERR_NOT_SUPPORTED;
    snapshot.health = LUNASHELL::DeviceHealth::Missing;
    LUNASHELL::DeviceSnapshotService::instance().reportWifi(snapshot);
    LUNASHELL::WifiScanSnapshot scan{};
    scan.state = LUNASHELL::WifiScanState::Unavailable;
    scan.error = ESP_ERR_NOT_SUPPORTED;
    scan.revision = 1;
    (void)LUNASHELL::DeviceSnapshotService::instance().reportWifiScan(scan);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_connect(
    const char *ssid,
    const char *password,
    bool persist
)
{
    (void)ssid;
    (void)password;
    (void)persist;
    return ESP_ERR_NOT_SUPPORTED;
}

// ssid: Requested network name.
// password: Secret supplied by the caller; never logged.
// persist: Requested persistence policy.
// selection: Exact AP and security selected by the caller.
// Exception: The disabled Wi-Fi owner always returns NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_wifi_connect_selected(const char *ssid, const char *password,
    bool persist, const LUNASHELL_wifi_selection_t *selection)
{
    (void)ssid;
    (void)password;
    (void)persist;
    (void)selection;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_disconnect(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: disabled Wi-Fi has no reconnect owner to expedite.
// Exception: Returns NOT_SUPPORTED; no connection intent or timer is created.
extern "C" esp_err_t LUNASHELL_wifi_expedite_reconnect(void)
{
    ESP_LOGW("LUNASHELL_wifi", "Wake reconnect unavailable (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: disabled Wi-Fi cannot verify or clear persistent driver credentials.
// Exception: Returns NOT_SUPPORTED so the orchestrator cannot claim a completed factory reset.
extern "C" esp_err_t LUNASHELL_wifi_reset_for_factory(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_start_scan(void)
{
    LUNASHELL::WifiScanSnapshot scan{};
    scan.state = LUNASHELL::WifiScanState::Unavailable;
    scan.error = ESP_ERR_NOT_SUPPORTED;
    scan.revision = 1;
    (void)LUNASHELL::DeviceSnapshotService::instance().reportWifiScan(scan);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" size_t LUNASHELL_wifi_get_networks(
    LUNASHELL_wifi_network_t *networks,
    size_t capacity
)
{
    (void)networks;
    (void)capacity;
    return 0;
}

// No parameters: a disabled radio cannot acquire signal measurements.
// Exception: Returns NOT_SUPPORTED without publishing a fabricated sample.
extern "C" esp_err_t LUNASHELL_wifi_refresh_signal(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void LUNASHELL_wifi_get_status(LUNASHELL_wifi_status_t *status)
{
    fill_disabled_status(status);
}

namespace LUNASHELL::wifi_provisioning {
esp_err_t begin()
{
    return ESP_ERR_NOT_SUPPORTED;
}

// ssid: Unused credential bytes in a profile without Wi-Fi.
// ssid_capacity: Unused source capacity.
esp_err_t credentialsReceived(const uint8_t *ssid, size_t ssid_capacity)
{
    (void)ssid;
    (void)ssid_capacity;
    return ESP_ERR_NOT_SUPPORTED;
}

// keep_connection: Unused in a profile without Wi-Fi.
esp_err_t end(bool keep_connection)
{
    (void)keep_connection;
    return ESP_ERR_NOT_SUPPORTED;
}
}  // namespace LUNASHELL::wifi_provisioning
