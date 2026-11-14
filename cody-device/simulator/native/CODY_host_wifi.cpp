#include "CODY_host_wifi.hpp"

// Transitional scenario driver: this is not the production Wi-Fi owner and
// must not be used as evidence of device behavior or a completed simulator.
#include "LUNASHELL_platform.h"
#include "esp_timer.h"
#include "nvs.h"

#include <cstring>
#include <limits>
#include <mutex>

namespace {
std::mutex s_mutex;
bool s_configured = false;
LUNASHELL::WifiScanSnapshot s_result{};
bool s_connection_configured = false;
esp_err_t s_connection_error = ESP_ERR_NOT_SUPPORTED;
LUNASHELL::WifiSnapshot s_connected{};
bool s_protected = false;
}

// result: Explicit terminal synthetic scan, not acquired from host Wi-Fi.
// Exception: Rejects malformed results before mutating the host scenario.
esp_err_t CODY::host_onboarding::configure_wifi_scan(const LUNASHELL::WifiScanSnapshot &result)
{
    using LUNASHELL::WifiScanState;
    if (result.count > LUNASHELL::kWifiScanMaxNetworks ||
        (result.state != WifiScanState::Complete && result.state != WifiScanState::Failed) ||
        (result.state == WifiScanState::Complete && result.error != ESP_OK) ||
        (result.state == WifiScanState::Failed && (result.error == ESP_OK || result.count != 0)))
        return ESP_ERR_INVALID_ARG;
    for (uint8_t index = 0; index < result.count; ++index) {
        const auto &ssid = result.networks[index].ssid;
        if (std::strncmp(ssid, "SIM-", 4) != 0 || std::memchr(ssid, '\0', sizeof(ssid)) == nullptr)
            return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    s_result = result;
    s_configured = true;
    return ESP_OK;
}

// result: Explicit simulated station outcome, never acquired from a host radio.
// error: Scenario-provided admission outcome, including failure.
// Exception: Rejects invalid success snapshots before mutating the scenario.
esp_err_t CODY::host_onboarding::configure_wifi_connection(const LUNASHELL::WifiSnapshot &result, esp_err_t error)
{
    if (error == ESP_OK && (!result.data_valid || result.data_stale || result.last_error != ESP_OK ||
        !result.started || !result.connection_state_valid || !result.connected || !result.ip_acquired ||
        !result.bssid_valid || !result.rssi_valid || result.ip_address[0] == '\0' ||
        std::strncmp(result.ssid, "SIM-", 4) != 0 ||
        std::memchr(result.ssid, '\0', sizeof(result.ssid)) == nullptr ||
        std::memchr(result.ip_address, '\0', sizeof(result.ip_address)) == nullptr)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_connected = result;
    s_connection_error = error;
    s_connection_configured = true;
    return ESP_OK;
}

// No parameters: the caller explicitly requests a synthetic empty scan scenario.
// Exception: Returns the original configuration or publication failure.
CODY_HOST_WIFI_API esp_err_t cody_host_wifi_seed_empty_scan()
{
    LUNASHELL::WifiScanSnapshot terminal{};
    terminal.state = LUNASHELL::WifiScanState::Complete;
    auto error = CODY::host_onboarding::configure_wifi_scan(terminal);
    if (error != ESP_OK) return error;
    LUNASHELL::WifiScanSnapshot initial{};
    initial.state = LUNASHELL::WifiScanState::Idle;
    return LUNASHELL::DeviceSnapshotService::instance().reportWifiScan(initial);
}

// No parameters: explicitly configure a non-routable, open SIM-CODY scenario.
// Exception: Preserves the first scan, station or initial publication error.
CODY_HOST_WIFI_API esp_err_t cody_host_wifi_seed_open_network()
{
    constexpr uint8_t bssid[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    LUNASHELL::WifiScanSnapshot scan{};
    scan.state = LUNASHELL::WifiScanState::Complete;
    scan.count = 1;
    std::memcpy(scan.networks[0].ssid, "SIM-CODY", 9);
    std::memcpy(scan.networks[0].bssid, bssid, sizeof(bssid));
    scan.networks[0].rssi = -55;
    scan.networks[0].channel = 1;
    scan.networks[0].security = LUNASHELL::WifiNetworkSecurity::Open;
    auto error = CODY::host_onboarding::configure_wifi_scan(scan);
    LUNASHELL::WifiSnapshot wifi{};
    wifi.health = LUNASHELL::DeviceHealth::Ready;
    wifi.last_error = ESP_OK;
    wifi.data_valid = true;
    wifi.started = wifi.connection_state_valid = wifi.connected = wifi.ip_acquired = true;
    wifi.bssid_valid = wifi.rssi_valid = true;
    wifi.rssi_error = ESP_OK;
    wifi.rssi = -55;
    wifi.channel = 1;
    std::memcpy(wifi.ssid, "SIM-CODY", 9);
    std::memcpy(wifi.ip_address, "192.0.2.2", 10);
    std::memcpy(wifi.bssid, bssid, sizeof(bssid));
    if (error == ESP_OK) error = CODY::host_onboarding::configure_wifi_connection(wifi, ESP_OK);
    if (error == ESP_OK) {
        LUNASHELL::WifiScanSnapshot initial{};
        error = LUNASHELL::DeviceSnapshotService::instance().reportWifiScan(initial);
    }
    if (error == ESP_OK) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_protected = false;
    }
    return error;
}

// mode: 1 protected AP, 2 no AP, 3 protected AP returning a connection timeout.
// Exception: Preserves the first simulated configuration or publication error.
CODY_HOST_WIFI_API esp_err_t cody_host_wifi_seed_scenario(int mode)
{
    if (mode == 2) return cody_host_wifi_seed_empty_scan();
    if (mode != 1 && mode != 3) return ESP_ERR_INVALID_ARG;
    auto error = cody_host_wifi_seed_open_network();
    if (error != ESP_OK) return error;
    std::lock_guard<std::mutex> lock(s_mutex);
    s_result.networks[0].security = LUNASHELL::WifiNetworkSecurity::Wpa2Personal;
    s_protected = true;
    s_connection_error = mode == 3 ? ESP_ERR_TIMEOUT : ESP_OK;
    return ESP_OK;
}

// connected: Receives -1 when connectivity is unavailable, otherwise 0 or 1.
// ssid: Receives a terminated simulated network name.
// capacity: Size of the caller's SSID buffer.
// rssi: Receives -101 when signal strength is unavailable.
// Exception: Preserves snapshot and buffer errors without inventing a reading.
CODY_HOST_WIFI_API esp_err_t cody_host_simulated_wifi_read(int *connected, char *ssid, size_t capacity, int *rssi)
{
    if (!connected || !ssid || capacity < sizeof(LUNASHELL::WifiSnapshot{}.ssid) || !rssi)
        return ESP_ERR_INVALID_ARG;
    LUNASHELL::HardwareStatus hardware{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(hardware);
    if (error != ESP_OK) return error;
    const auto &wifi = hardware.wifi;
    *connected = wifi.connection_state_valid ? (wifi.connected ? 1 : 0) : -1;
    std::memcpy(ssid, wifi.ssid, sizeof(wifi.ssid));
    ssid[sizeof(wifi.ssid) - 1] = '\0';
    *rssi = wifi.rssi_valid && !wifi.data_stale ? wifi.rssi : -101;
    return ESP_OK;
}

// connected: Explicit simulated association state, never a host radio operation.
// ssid: Printable ASCII synthetic network name, at most 32 bytes.
// rssi: Signal in dBm, or -101 when the observation is unavailable.
// Exception: Rejects malformed input and preserves the original publication error.
CODY_HOST_WIFI_API esp_err_t cody_host_simulated_wifi_apply(int connected, const char *ssid, int rssi)
{
    if ((connected != 0 && connected != 1) || !ssid) return ESP_ERR_INVALID_ARG;
    size_t length = 0;
    while (length < 33 && ssid[length] != '\0') {
        if (ssid[length] < 0x20 || ssid[length] > 0x7e) return ESP_ERR_INVALID_ARG;
        ++length;
    }
    if (length > 32 || (connected && !length) ||
        (rssi != -101 && (rssi < -100 || rssi > -1))) return ESP_ERR_INVALID_ARG;
    LUNASHELL::HardwareStatus hardware{};
    auto &owner = LUNASHELL::DeviceSnapshotService::instance();
    auto error = owner.readHardwareStatus(hardware);
    if (error != ESP_OK) return error;
    auto wifi = hardware.wifi;
    wifi.health = LUNASHELL::DeviceHealth::Ready;
    wifi.last_error = ESP_OK;
    wifi.started = true;
    wifi.connection_state_valid = true;
    wifi.connected = connected != 0;
    wifi.ip_acquired = wifi.connected;
    wifi.reconnect_pending = false;
    wifi.connect_requested = false;
    wifi.credentials_configured = false;
    wifi.bssid_valid = wifi.connected;
    const uint8_t synthetic_bssid[] = {0x02, 0, 0, 0, 0, 1};
    std::memcpy(wifi.bssid, synthetic_bssid, sizeof(wifi.bssid));
    wifi.channel = wifi.connected ? 1 : 0;
    wifi.rssi_valid = wifi.connected && rssi != -101;
    wifi.rssi_error = wifi.rssi_valid ? ESP_OK : ESP_ERR_INVALID_STATE;
    wifi.rssi = wifi.rssi_valid ? static_cast<int8_t>(rssi) : 0;
    wifi.rssi_sampled_at_us = wifi.rssi_valid ? esp_timer_get_time() : 0;
    std::memcpy(wifi.ssid, ssid, length + 1);
    if (wifi.connected) std::memcpy(wifi.ip_address, "192.0.2.2", 10);
    else wifi.ip_address[0] = '\0';
    return owner.reportWifi(wifi);
}

// No parameters: runs the explicitly configured synthetic scan without touching host radios.
// Exception: Preserves unavailable source and publication errors, with no invented networks.
extern "C" esp_err_t LUNASHELL_wifi_start_scan(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_configured) return ESP_ERR_NOT_SUPPORTED;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::WifiScanSnapshot previous{};
    auto error = snapshots.readWifiScanObservation(previous);
    if (error != ESP_OK) return error;
    if (previous.state == LUNASHELL::WifiScanState::Scanning ||
        previous.generation == UINT32_MAX || previous.revision > UINT64_MAX - 2) return ESP_ERR_INVALID_STATE;
    auto starting = previous;
    ++starting.generation;
    ++starting.revision;
    starting.state = LUNASHELL::WifiScanState::Scanning;
    starting.error = ESP_OK;
    starting.completed_at_us = 0;
    starting.count = 0;
    error = snapshots.reportWifiScan(starting);
    if (error != ESP_OK) return error;
    auto terminal = s_result;
    terminal.generation = starting.generation;
    terminal.revision = starting.revision + 1;
    terminal.completed_at_us = esp_timer_get_time();
    return snapshots.reportWifiScan(terminal);
}

// ssid: Explicit synthetic SSID, never forwarded to the host OS.
// password: Borrowed simulated credential, never logged or persisted.
// persist: If true, the synthetic profile identity is committed to host-only NVS.
// selection: Exact requested AP identity.
// Exception: Preserves scenario, host NVS and snapshot errors; never connects a real radio.
extern "C" esp_err_t LUNASHELL_wifi_connect_selected(const char *ssid, const char *password,
    bool persist, const LUNASHELL_wifi_selection_t *selection)
{
    if (!ssid || !password || !selection) return ESP_ERR_INVALID_ARG;
    size_t ssid_length = 0;
    while (ssid_length < 33 && ssid[ssid_length] != '\0') ++ssid_length;
    if (ssid_length == 33 || std::strncmp(ssid, "SIM-", 4) != 0) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_connection_configured) return ESP_ERR_NOT_SUPPORTED;
    if (std::strcmp(ssid, s_connected.ssid) != 0 || std::memcmp(selection->bssid, s_connected.bssid, 6) != 0)
        return ESP_ERR_INVALID_STATE;
    if (s_protected) {
        if (selection->security != LUNASHELL_WIFI_SELECTED_WPA2) return ESP_ERR_INVALID_ARG;
        if (std::strcmp(password, "CodyDemo42") != 0) return ESP_FAIL;
    } else if (selection->security != LUNASHELL_WIFI_SELECTED_OPEN || password[0] != '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_connection_error != ESP_OK) return s_connection_error;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::ConnectivityObservation observation{};
    auto error = snapshots.readConnectivityObservation(observation);
    if (error != ESP_OK) return error;
    const auto &old = observation.radios.wifi;
    if (old.connect_attempt_count == UINT32_MAX || old.association_count == UINT32_MAX) return ESP_ERR_INVALID_STATE;
    if (persist) {
        nvs_handle_t handle = 0;
        error = nvs_open("CODY_sim_wifi", NVS_READWRITE, &handle);
        if (error == ESP_OK) {
            error = nvs_set_str(handle, "ssid", ssid);
            if (error == ESP_OK) error = nvs_set_blob(handle, "bssid", selection->bssid, sizeof(selection->bssid));
            if (error == ESP_OK) error = nvs_commit(handle);
            nvs_close(handle);
        }
        if (error != ESP_OK) return error;
    }
    auto result = s_connected;
    result.connect_attempt_count = old.connect_attempt_count + 1;
    result.association_count = old.association_count + 1;
    result.rssi_sampled_at_us = esp_timer_get_time();
    result.last_attempt_at_us = result.last_success_at_us = result.rssi_sampled_at_us;
    result.credentials_configured = persist;
    return snapshots.reportWifi(result);
}
