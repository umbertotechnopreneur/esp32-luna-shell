#pragma once

#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class StartupConnectionPhase : uint8_t {
    None, Unavailable, WifiStarting, WifiJoining, WifiAddress, WifiRetry, WifiUnavailable,
    PcReconnecting, PcWaiting, PcUnavailable, Ready, NeedsSetup,
};

struct StartupConnectionStatus {
    StartupConnectionPhase phase = StartupConnectionPhase::None;
    esp_err_t error = ESP_OK;
    uint16_t disconnect_reason = 0;
    bool wifi_ready = false;
};

// One display-owner boot window. It never owns Wi-Fi policy or persisted preferences.
struct StartupConnectionModel {
    static constexpr int64_t kWindowUs = 30LL * 1000000LL;
    static constexpr int64_t kWifiSuccessUs = 1200000LL;
    int64_t started_us = -1;
    int64_t wifi_success_started_us = -1;
    bool wifi_success_active = false;
    bool retired = false;
    StartupConnectionStatus presented{};
};

// model: Existing boot window; route departure permanently retires this lifetime.
// now_us: Canonical monotonic clock, never restarted for a radio retry.
inline bool startup_connection_window_open(const StartupConnectionModel &model, int64_t now_us)
{
    return !model.retired && model.started_us >= 0 && now_us >= model.started_us &&
        now_us - model.started_us < StartupConnectionModel::kWindowUs;
}

// model: One boot-lifetime window; an existing or retired window is never rearmed.
// now_us: First admitted monotonic instant; negative values are unavailable.
inline void begin_startup_connection(StartupConnectionModel &model, int64_t now_us)
{
    if (!model.retired && model.started_us < 0 && now_us >= 0) model.started_us = now_us;
}

// model: Existing boot-lifetime deadline.
// status: Canonical phase; Ready and missing setup never impose an artificial wait.
// now_us: Current monotonic instant.
inline bool startup_connection_should_wait(const StartupConnectionModel &model,
    const StartupConnectionStatus &status, int64_t now_us)
{
    return startup_connection_window_open(model, now_us) && status.phase != StartupConnectionPhase::Ready &&
        status.phase != StartupConnectionPhase::NeedsSetup;
}

// model: Display-owned reaction clock; one success per boot window, never rearmed after loss.
// status: Fresh canonical projection; Wi-Fi success does not imply an authenticated PC link.
// now_us: Monotonic instant, bounded by the original boot deadline even during the reaction.
inline bool update_startup_wifi_success(StartupConnectionModel &model,
    const StartupConnectionStatus &status, int64_t now_us)
{
    const bool eligible = startup_connection_window_open(model, now_us) && status.wifi_ready &&
        status.phase != StartupConnectionPhase::NeedsSetup;
    if (eligible && model.wifi_success_started_us < 0) {
        model.wifi_success_started_us = now_us;
        model.wifi_success_active = true;
    }
    if (model.wifi_success_active && (!eligible || now_us < model.wifi_success_started_us ||
        now_us - model.wifi_success_started_us >= StartupConnectionModel::kWifiSuccessUs))
        model.wifi_success_active = false;
    return model.wifi_success_active;
}

// observation: One coherent canonical read; ignored entirely when read_error is nonzero.
// read_error: Original snapshot-read result, preserved without synthesizing radio values.
// codex_enabled: Current build's admitted PC integration.
// now_us: Canonical monotonic clock used for authenticated READY freshness.
inline StartupConnectionStatus project_startup_connection(const LUNASHELL::ConnectivityObservation &observation,
    esp_err_t read_error, bool codex_enabled, int64_t now_us)
{
    using Phase = StartupConnectionPhase;
    if (read_error != ESP_OK) return {Phase::Unavailable, read_error};
    const auto &wifi = observation.radios.wifi;
    if (!wifi.data_valid || wifi.data_stale || !wifi.connection_state_valid)
        return {Phase::WifiUnavailable, wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE};
    if (!wifi.credentials_configured) return {Phase::NeedsSetup};
    const bool wifi_ready = wifi.health == LUNASHELL::DeviceHealth::Ready && wifi.last_error == ESP_OK &&
        wifi.started && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    if (!wifi_ready) {
        const auto phase = wifi.reconnect_pending && !wifi.connected ? Phase::WifiRetry :
            wifi.last_error != ESP_OK ? Phase::WifiUnavailable : !wifi.started ? Phase::WifiStarting :
            wifi.connected && !wifi.ip_acquired ? Phase::WifiAddress :
            wifi.connect_requested && !wifi.connected ? Phase::WifiJoining : Phase::WifiUnavailable;
        return {phase, wifi.last_error, static_cast<uint16_t>(!wifi.connected ? wifi.last_disconnect_reason : 0)};
    }
    if (!codex_enabled) return {Phase::Ready, ESP_OK, 0, true};
    const auto &link = observation.radios.codex_link;
    if (!link.data_valid || link.data_stale)
        return {Phase::PcUnavailable, link.last_error != ESP_OK ? link.last_error : ESP_ERR_INVALID_STATE, 0, true};
    if (link.pairing_repair_required || (!link.retained_link_present && !link.pending_pairing_present))
        return {Phase::NeedsSetup, link.last_error, 0, true};
    // READY is authenticated transport evidence; optional capability errors must not delay it.
    if (LUNASHELL::codex_presence::ready_current(link, now_us)) return {Phase::Ready, link.last_error, 0, true};
    if (link.last_error != ESP_OK) return {Phase::PcUnavailable, link.last_error, 0, true};
    return {link.state == LUNASHELL::CodexLinkState::Discovering ? Phase::PcReconnecting : Phase::PcWaiting,
        ESP_OK, 0, true};
}

// left: Last presented semantic connection phase.
// right: Current canonical phase; time and attempt counters do not restart the view.
inline bool same_startup_connection_status(const StartupConnectionStatus &left, const StartupConnectionStatus &right)
{
    return left.phase == right.phase && left.error == right.error && left.disconnect_reason == right.disconnect_reason &&
        left.wifi_ready == right.wifi_ready;
}

} // namespace CODY
