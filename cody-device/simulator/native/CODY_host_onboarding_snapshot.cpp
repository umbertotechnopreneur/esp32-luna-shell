#include "CODY_host_onboarding_snapshot.hpp"
#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "LUNASHELL_codex_presence.hpp"
#include "LUNASHELL_voice_input.hpp"
#include "LUNASHELL_logger.h"
#include "esp_timer.h"

#include <mutex>
#include <cstring>

#if defined(_WIN32)
#define CODY_HOST_SENSOR_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_SENSOR_API extern "C" __attribute__((visibility("default")))
#endif

// The simulator does not expose a physical SD logger.
extern "C" esp_err_t LUNASHELL_logger_get_sd_status(LUNASHELL_logger_sd_status_t *)
{
    return ESP_ERR_NOT_SUPPORTED;
}

namespace {
// The simulator has one canonical device snapshot. Derived observations are
// created only while holding this mutex; no projection survives as a cache.
std::mutex s_mutex;
LUNASHELL::DeviceSnapshot s_snapshot{};
bool s_seeded = false;
LUNASHELL::WifiScanSnapshot s_wifi_scan{};
bool s_wifi_scan_available = false;
// These are canonical host service observations, matching the production owner's
// separate bounded account/allowance storage rather than consumer-side caches.
LUNASHELL::CodexWeeklyLimitSnapshot s_studio_weekly{};
LUNASHELL::CodexAccountSnapshot s_studio_account{};
bool s_studio_statistics = false;
}

namespace CODY::host_onboarding {
// request: Explicit input for Settings, source animation samples or counter pages.
// Exception: Returns invalid argument or unavailable clock without changing observations.
esp_err_t validate_studio_case(const CodyHostStudioCase &request)
{
    const auto &identity = request.identity;
    const bool branding = std::strcmp(identity.target, "cody.boot.branding") == 0;
    const bool birth = std::strcmp(identity.target, "cody.boot.birth") == 0;
    if ((branding && identity.source_animation_sample_ms >= 3500) ||
        (birth && identity.source_animation_sample_ms >= 7000) ||
        (!branding && !birth && identity.source_animation_sample_ms != 0)) return ESP_ERR_INVALID_ARG;
    if (std::strcmp(identity.target, "cody.settings.quiet-edit") == 0) {
        if (request.quiet_enabled > 1 || request.quiet_edit_start > 1 ||
            request.quiet_start_minute >= 1440 || request.quiet_end_minute >= 1440 ||
            request.quiet_draft_minute >= 1440 || (request.quiet_enabled &&
            request.quiet_start_minute == request.quiet_end_minute)) return ESP_ERR_INVALID_ARG;
    }
    const bool reset = std::strcmp(identity.target, "cody.dashboard.reset") == 0;
    const bool limits = std::strcmp(identity.target, "cody.dashboard.limits") == 0;
    if (reset || limits) {
        if (std::strcmp(request.codex_link_outcome, "ready") != 0) return ESP_ERR_INVALID_ARG;
        LUNASHELL::HardwareStatus hardware{};
        const auto hardware_error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(hardware);
        if (hardware_error != ESP_OK) return hardware_error;
        const auto &wifi = hardware.wifi;
        if (!wifi.data_valid || wifi.data_stale || wifi.last_error != ESP_OK ||
            !wifi.connection_state_valid || !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0')
            return ESP_ERR_INVALID_ARG;
        LUNASHELL::ClockSnapshot clock{};
        const auto error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
        if (error != ESP_OK) return error;
        if (!clock.data_valid || clock.data_stale || clock.last_error != ESP_OK ||
            request.weekly_reset_unix_seconds <= clock.utc_epoch_seconds ||
            request.weekly_remaining_basis_points > 10000 || request.weekly_source_age_ms > 60000)
            return ESP_ERR_INVALID_ARG;
        if (limits && (request.short_reset_unix_seconds <= clock.utc_epoch_seconds ||
            request.short_remaining_basis_points > 10000 || request.short_window_minutes == 0 ||
            request.short_window_minutes > 10080 || request.short_source_age_ms > 60000))
            return ESP_ERR_INVALID_ARG;
    } else if (request.codex_link_outcome[0] != '\0') return ESP_ERR_INVALID_ARG;
    if (std::strcmp(identity.target, "cody.provisioning.error") == 0 &&
        std::strcmp(request.provisioning_outcome, "timeout") != 0 &&
        std::strcmp(request.provisioning_outcome, "authentication") != 0 &&
        std::strcmp(request.provisioning_outcome, "network-not-found") != 0) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

// request: Complete validated synthetic service response, replacing prior Studio page data.
// Exception: Rejects an unseeded canonical host owner; no transport or real provisioning is invoked.
esp_err_t publish_studio_case(const CodyHostStudioCase &request)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_INVALID_STATE;
    const auto now = esp_timer_get_time();
    s_studio_statistics = false;
    s_studio_weekly = {};
    s_studio_account = {};
    s_snapshot.radios.pairing = {};
    s_snapshot.radios.codex_link = {};
    const bool reset = std::strcmp(request.identity.target, "cody.dashboard.reset") == 0;
    const bool limits = std::strcmp(request.identity.target, "cody.dashboard.limits") == 0;
    if (reset || limits) {
        // This is an explicit synthetic READY/app lease, never a transport ACK.
        // The same snapshot owner and production aging rules qualify its data.
        auto &link = s_snapshot.radios.codex_link;
        link = {};
        LUNASHELL::snapshot_detail::mark_sample_success(link, now);
        link.state = LUNASHELL::CodexLinkState::Connected;
        link.ready_at_us = link.changed_at_us = now;
        link.retained_link_present = true;
        link.pc_time_state = LUNASHELL::PcTimeSyncState::Applied;
        std::memcpy(link.device_id, "000000000001", sizeof("000000000001"));
        std::memcpy(link.paired_pc_id, "11111111111111111111111111111111", sizeof("11111111111111111111111111111111"));
        std::memcpy(link.peer_ip_address, "192.0.2.1", sizeof("192.0.2.1"));
        link.app_presence.state = LUNASHELL::CodexAppPresenceState::Open;
        link.app_presence.data_valid = true;
        link.app_presence.last_error = ESP_OK;
        link.app_presence.received_at_us = now;
        link.app_presence.sequence = 1;
        LUNASHELL::snapshot_detail::mark_sample_success(s_studio_weekly, now);
        s_studio_weekly.reset_unix_seconds = request.weekly_reset_unix_seconds;
        s_studio_weekly.remaining_basis_points = static_cast<uint16_t>(request.weekly_remaining_basis_points);
        s_studio_weekly.source_age_at_receive_ms = request.weekly_source_age_ms;
        if (limits) {
            LUNASHELL::snapshot_detail::mark_sample_success(s_studio_account, now);
            s_studio_account.short_reset_unix_seconds = request.short_reset_unix_seconds;
            s_studio_account.short_remaining_basis_points = static_cast<uint16_t>(request.short_remaining_basis_points);
            s_studio_account.short_window_minutes = static_cast<uint16_t>(request.short_window_minutes);
            s_studio_account.source_age_at_receive_ms = request.short_source_age_ms;
        }
        s_studio_statistics = true;
    }
    if (std::strcmp(request.identity.target, "cody.provisioning.error") == 0) {
        auto &pairing = s_snapshot.radios.pairing;
        LUNASHELL::snapshot_detail::mark_sample_success(pairing, now);
        pairing.attempt_id = 1;
        pairing.changed_at_us = now;
        pairing.cleanup_error = pairing.dma_reservation_error = ESP_OK;
        pairing.last_error = std::strcmp(request.provisioning_outcome, "timeout") == 0 ? ESP_ERR_TIMEOUT : ESP_FAIL;
        pairing.state = pairing.last_error == ESP_ERR_TIMEOUT ? LUNASHELL::PairingSessionState::TimedOut :
            LUNASHELL::PairingSessionState::Failed;
        pairing.failure_reason = std::strcmp(request.provisioning_outcome, "authentication") == 0 ?
            LUNASHELL::ProvisioningFailureReason::Authentication :
            std::strcmp(request.provisioning_outcome, "network-not-found") == 0 ?
            LUNASHELL::ProvisioningFailureReason::AccessPointNotFound : LUNASHELL::ProvisioningFailureReason::None;
    }
    ++s_snapshot.sequence;
    return ESP_OK;
}

// automatic: Explicit synthetic auto-rotation preference, without gravity data.
// degrees: Retained manual logical orientation, restricted to 0 or 180.
// Exception: Rejects an unseeded owner or unsupported angle without mutation.
esp_err_t publish_static_orientation(bool automatic, uint16_t degrees)
{
    if (degrees != 0 && degrees != 180) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_INVALID_STATE;
    auto &orientation = s_snapshot.ui.orientation;
    orientation.health = LUNASHELL::DeviceHealth::Ready;
    orientation.last_error = ESP_OK;
    orientation.data_valid = true;
    orientation.data_stale = false;
    orientation.auto_rotation_enabled = automatic;
    orientation.gravity_valid = false;
    orientation.rotation_pending = false;
    orientation.rendering_rotation_degrees = degrees;
    orientation.sampled_at_us = esp_timer_get_time();
    orientation.changed_at_us = orientation.sampled_at_us;
    orientation.last_attempt_at_us = orientation.sampled_at_us;
    orientation.last_success_at_us = orientation.sampled_at_us;
    ++orientation.change_sequence;
    return ESP_OK;
}

// snapshot: Initial, explicitly synthetic scenario, never physical hardware data.
// now_us: Positive time from the same monotonic clock as the scenario.
// Exception: Rejects invalid time or a second seed without changing the owner.
esp_err_t publish_device_snapshot(const LUNASHELL::DeviceSnapshot &snapshot, int64_t now_us)
{
    if (now_us <= 0) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_seeded) return ESP_ERR_INVALID_STATE;
    s_snapshot = snapshot;
    s_seeded = true;
    return ESP_OK;
}

// observation: Receives the playback projection from the same canonical snapshot.
// Exception: An unseeded simulator does not modify the destination.
esp_err_t read_playback(LUNASHELL::PlaybackSnapshot &observation)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    observation = s_snapshot.audio.playback;
    return ESP_OK;
}
} // namespace CODY::host_onboarding

#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
// No parameters: publish the host AFE's explicitly unavailable, stopped state
// for an isolated live replay. This is not a physical microphone observation.
// Exception: Refuses to overwrite any supplied or active voice observation.
CODY_HOST_SENSOR_API esp_err_t cody_host_studio_seed_voice_stopped()
{
    if (cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000 ||
        LUNASHELL_voice_afe_running() || LUNASHELL_voice_afe_capture_active()) return ESP_ERR_INVALID_STATE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto &voice = s_snapshot.voice;
    if (voice.state != LUNASHELL::VoiceState::Disabled || voice.data_valid || voice.last_attempt_at_us != 0 ||
        voice.input.phase != LUNASHELL::VoiceInputPhase::Idle || voice.input.changed_at_us != 0)
        return ESP_ERR_INVALID_STATE;
    const auto now = esp_timer_get_time();
    if (now <= 0) return ESP_ERR_INVALID_STATE;
    LUNASHELL::snapshot_detail::mark_sample_failure(voice, ESP_ERR_NOT_SUPPORTED, now);
    ++s_snapshot.sequence;
    if (voice.state != LUNASHELL::VoiceState::Disabled || voice.last_error != ESP_ERR_NOT_SUPPORTED ||
        voice.last_attempt_at_us != now || voice.input.phase != LUNASHELL::VoiceInputPhase::Idle)
        return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
#endif

// percent: Explicit scenario battery level; this is not a PC or physical Cody reading.
CODY_HOST_SENSOR_API esp_err_t cody_host_simulated_battery_refresh(int percent)
{
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto &battery = s_snapshot.battery;
    LUNASHELL::snapshot_detail::mark_sample_success(battery, esp_timer_get_time());
    battery.battery_present = true;
    battery.level_percent_valid = true;
    battery.level_percent = percent;
    ++s_snapshot.sequence;
    return ESP_OK;
}

// percent: Receives the current synthetic battery percentage; -1 means unavailable.
// Exception: An unseeded owner or null output leaves the destination untouched.
CODY_HOST_SENSOR_API esp_err_t cody_host_simulated_battery_read(int *percent)
{
    if (!percent) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto battery = s_snapshot.battery;
    LUNASHELL::age_measured_sample(battery, esp_timer_get_time(), LUNASHELL::measured_sample_ttl_us(s_snapshot));
    *percent = battery.data_valid && !battery.data_stale && battery.level_percent_valid &&
        battery.level_percent >= 0 && battery.level_percent <= 100 ? battery.level_percent : -1;
    return ESP_OK;
}

// ssid: Scenario's current simulated access point; ignored if the device has moved on.
// rssi: Simulated signal strength, in dBm.
// Exception: Rejects malformed inputs and preserves the canonical snapshot lock.
CODY_HOST_SENSOR_API esp_err_t cody_host_simulated_wifi_signal_refresh(const char *ssid, int rssi)
{
    if (!ssid || rssi < -100 || rssi > -1) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto &wifi = s_snapshot.radios.wifi;
    if (!wifi.connection_state_valid || !wifi.connected || std::strcmp(wifi.ssid, ssid) != 0) return ESP_OK;
    const auto now = esp_timer_get_time();
    LUNASHELL::snapshot_detail::mark_sample_success(wifi, now);
    wifi.rssi_valid = true;
    wifi.rssi_error = ESP_OK;
    wifi.rssi = static_cast<int8_t>(rssi);
    wifi.rssi_sampled_at_us = now;
    ++s_snapshot.sequence;
    return ESP_OK;
}

namespace LUNASHELL::snapshot_detail {
// mutation: Original device report callback, invoked synchronously under the owner lock.
// context: Borrowed callback state; never retained after the call.
// policy: Original required/optional admission; the host never fabricates publication.
// Exception: Returns the source initialization or argument error without a mutation.
esp_err_t apply_snapshot_mutation(SnapshotMutation mutation, void *context, SnapshotPublicationPolicy policy)
{
    (void)policy;
    if (!mutation) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    mutation(s_snapshot, context);
    ++s_snapshot.sequence;
    return ESP_OK;
}
} // namespace LUNASHELL::snapshot_detail

namespace LUNASHELL {
DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService owner;
    return owner;
}

// These services have no connected host owner. Destinations remain untouched.
esp_err_t DeviceSnapshotService::readAssistantStatuses(AssistantCatalog &) const { return ESP_ERR_NOT_SUPPORTED; }
// limit: Receives the canonical synthetic allowance only for a selected Studio statistics case.
// Exception: Unavailable sessions preserve the output and return NOT_SUPPORTED.
esp_err_t DeviceSnapshotService::readCodexWeeklyLimit(CodexWeeklyLimitSnapshot &limit) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded || !s_studio_statistics) return ESP_ERR_NOT_SUPPORTED;
    auto current = s_studio_weekly;
    const auto now = esp_timer_get_time();
    const bool ready = codex_presence::ready_current(s_snapshot.radios.codex_link, now) &&
        codex_presence::is_open(s_snapshot.radios.codex_link.app_presence, now);
    age_codex_account_sample(current, current.source_age_at_receive_ms, ready, now);
    limit = current;
    return ESP_OK;
}

// account: Receives the canonical synthetic account observation, including its actual validity.
// Exception: Unavailable sessions preserve the output and return NOT_SUPPORTED.
esp_err_t DeviceSnapshotService::readCodexAccount(CodexAccountSnapshot &account) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded || !s_studio_statistics) return ESP_ERR_NOT_SUPPORTED;
    auto current = s_studio_account;
    const auto now = esp_timer_get_time();
    const bool ready = codex_presence::ready_current(s_snapshot.radios.codex_link, now) &&
        codex_presence::is_open(s_snapshot.radios.codex_link.app_presence, now);
    age_codex_account_sample(current, current.source_age_at_receive_ms, ready, now);
    account = current;
    return ESP_OK;
}
esp_err_t DeviceSnapshotService::readCodexVouchers(CodexVoucherSnapshot &) const { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::readCodexSaved(CodexSavedSnapshot &) const { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::readCodexTokenIntervals(CodexTokenIntervalsSnapshot &) const { return ESP_ERR_NOT_SUPPORTED; }
// request: Exact bounded source/chat/turn query from the native browser presenter.
// result: Receives a copied synthetic host-owner projection, never authenticated domain data.
// Exception: Preserves fixture scope, generation, availability and original query errors.
esp_err_t DeviceSnapshotService::readCodexDomainObservation(const CODY::Domain::StateQuery &request,
    CODY::Domain::StateQueryResult &result) const
{
    return CODY::host_studio_domain::query(request, result);
}
esp_err_t DeviceSnapshotService::readCodexSelection(CODY::Domain::ChatId &) const { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::saveCodexSelection(const CODY::Domain::ChatId &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::readCodexRecovery(CodexRecoveryObservation &) const { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::readOpenRouterCatalogStatus(OpenRouterCatalogStatus &) const { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t DeviceSnapshotService::readOpenRouterCatalogPage(uint32_t, size_t, OpenRouterModelQuote *, size_t, size_t &) const { return ESP_ERR_NOT_SUPPORTED; }

// observation: Receives one coherent connectivity projection from the canonical snapshot.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readConnectivityObservation(ConnectivityObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    ConnectivityObservation copy{};
    copy.radios = s_snapshot.radios;
    copy.operating_state = s_snapshot.power.state;
    codex_presence::age(copy.radios.codex_link.app_presence, esp_timer_get_time());
    observation = copy;
    return ESP_OK;
}

// observation: Receives provisioning, power and Wi-Fi from one canonical sample.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readPairingObservation(PairingObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    const auto &wifi = s_snapshot.radios.wifi;
    PairingObservation copy{};
    copy.provisioning = s_snapshot.radios.provisioning;
    copy.operating_state = s_snapshot.power.state;
    copy.wifi_online = wifi.health == DeviceHealth::Ready && wifi.data_valid && !wifi.data_stale &&
        wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    observation = copy;
    return ESP_OK;
}

// observation: Receives the independent scan service's latest coherent result.
// Exception: The scan remains unavailable until an actual simulated scan publishes.
esp_err_t DeviceSnapshotService::readWifiScanObservation(WifiScanSnapshot &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded || !s_wifi_scan_available) return ESP_ERR_NOT_FOUND;
    observation = s_wifi_scan;
    return ESP_OK;
}

// scan: Actual result published by the simulated scan service.
// Exception: Rejects oversized data and preserves a newer published scan.
esp_err_t DeviceSnapshotService::reportWifiScan(const WifiScanSnapshot &scan)
{
    if (scan.count > kWifiScanMaxNetworks) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    if (!s_wifi_scan_available || scan.revision >= s_wifi_scan.revision) {
        s_wifi_scan = scan;
        s_wifi_scan_available = true;
    }
    return ESP_OK;
}

// status: Receives aged hardware fields from one canonical snapshot.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readHardwareStatus(HardwareStatus &status) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    HardwareStatus copy{};
    copy.battery = s_snapshot.battery;
    copy.clock = s_snapshot.clock;
    copy.wifi = s_snapshot.radios.wifi;
    copy.ble = s_snapshot.radios.ble;
    const auto now = esp_timer_get_time();
    const auto ttl = measured_sample_ttl_us(s_snapshot);
    age_measured_sample(copy.battery, now, ttl);
    age_measured_sample(copy.clock, now, ttl);
    age_wifi_signal(copy.wifi, now, ttl);
    status = copy;
    return ESP_OK;
}

// observation: Receives current scenario diagnostics, not host hardware measurements.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readDiagnosticsObservation(DiagnosticsObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    DiagnosticsObservation copy{};
    copy.wifi = s_snapshot.radios.wifi;
    copy.memory = s_snapshot.memory;
    copy.processor = s_snapshot.processor;
    copy.storage = s_snapshot.storage;
    const auto now = esp_timer_get_time();
    const auto ttl = measured_sample_ttl_us(s_snapshot);
    age_measured_sample(copy.memory, now, ttl);
    age_measured_sample(copy.processor, now, ttl);
    age_measured_sample(copy.storage, now, 90LL * 1000000LL);
    observation = copy;
    return ESP_OK;
}

// observation: Receives motion, playback and power data from one canonical sample.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readMotionPowerObservation(MotionPowerObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    MotionPowerObservation copy{};
    copy.battery = s_snapshot.battery;
    copy.imu = s_snapshot.imu;
    copy.clock = s_snapshot.clock;
    copy.audio = s_snapshot.audio;
    copy.voice = s_snapshot.voice;
    copy.character_interaction = s_snapshot.character_interaction;
    copy.character_touch = s_snapshot.character_touch;
    copy.storage_available = s_snapshot.storage.mounted && !s_snapshot.storage.poisoned &&
        s_snapshot.storage.card_response_verified && s_snapshot.storage.last_error == ESP_OK;
    copy.rendering_rotation_degrees = s_snapshot.ui.orientation.rendering_rotation_degrees;
    copy.touch_last_activity_at_us = s_snapshot.ui.touch_last_activity_at_us;
    const auto now = esp_timer_get_time();
    const auto ttl = measured_sample_ttl_us(s_snapshot);
    age_measured_sample(copy.battery, now, ttl);
    age_measured_sample(copy.imu, now, ttl);
    age_measured_sample(copy.clock, now, ttl);
    if (copy.voice.state == VoiceState::Listening) age_measured_sample(copy.voice, now, 1000000);
    age_measured_sample(copy.audio.microphone, now, ttl);
    age_measured_sample(copy.audio.playback, now, ttl);
    age_measured_sample(copy.character_interaction, now, ttl);
    age_measured_sample(copy.character_touch, now, ttl);
    observation = copy;
    return ESP_OK;
}

// voice: Receives the observed scenario voice state with its actual freshness.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readVoiceObservation(VoiceSnapshot &voice) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto copy = s_snapshot.voice;
    if (copy.state == VoiceState::Listening) age_measured_sample(copy, esp_timer_get_time(), 1000000);
    voice = copy;
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// admission: Receives current link and settings policy from one canonical sample.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readVoiceInputAdmissionObservation(VoiceInputAdmissionObservation &admission) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    const auto &prefs = s_snapshot.cody_preferences;
    const auto &wifi = s_snapshot.radios.wifi;
    const auto &link = s_snapshot.radios.codex_link;
    const bool wifi_ready = wifi.health == DeviceHealth::Ready && wifi.data_valid && !wifi.data_stale &&
        wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    admission = {prefs.dictation_error, prefs.dictation_valid, prefs.dictation_enabled,
        wifi.started && wifi.connection_state_valid && wifi_ready && link.retained_link_present &&
        !link.pending_pairing_present && !link.command_pending && codex_presence::ready_current(link, esp_timer_get_time())};
    return ESP_OK;
}
#endif

// output: Receives a field from the same canonical snapshot under its owner lock.
// Exception: Every unseeded read leaves its destination untouched.
#define CODY_HOST_SNAPSHOT_READ(method, type, expression) \
esp_err_t DeviceSnapshotService::method(type &output) const { \
    std::lock_guard<std::mutex> lock(s_mutex); \
    if (!s_seeded) return ESP_ERR_NOT_FOUND; \
    output = (expression); return ESP_OK; \
}
CODY_HOST_SNAPSHOT_READ(readCodexInboxObservation, CodexInboxSnapshot, s_snapshot.codex_inbox)
CODY_HOST_SNAPSHOT_READ(readCodexEventObservation, CodexEventSnapshot, s_snapshot.codex_event)
CODY_HOST_SNAPSHOT_READ(readDisplayFrameObservation, DisplayFrameSnapshot, s_snapshot.ui.rendering)
CODY_HOST_SNAPSHOT_READ(readPowerState, PowerManagementSnapshot, s_snapshot.power)
CODY_HOST_SNAPSHOT_READ(readCodyIdleObservation, CodyIdleObservation, s_snapshot.cody_idle)
CODY_HOST_SNAPSHOT_READ(readCodyEventVisualObservation, CodyEventVisualObservation, s_snapshot.cody_event_visual)
CODY_HOST_SNAPSHOT_READ(readCodyParallaxObservation, CodyParallaxObservation, s_snapshot.cody_parallax)
CODY_HOST_SNAPSHOT_READ(readCodyCharacterThemeObservation, CodyCharacterThemeObservation, s_snapshot.cody_character_theme)
#undef CODY_HOST_SNAPSHOT_READ

// battery: Receives the aged synthetic battery from the canonical snapshot.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readBatteryObservation(BatterySnapshot &battery) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto copy = s_snapshot.battery;
    age_measured_sample(copy, esp_timer_get_time(), measured_sample_ttl_us(s_snapshot));
    battery = copy;
    return ESP_OK;
}

// clock: Receives an aged clock from the canonical snapshot, never the PC clock.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readClockObservation(ClockSnapshot &clock) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto copy = s_snapshot.clock;
    age_measured_sample(copy, esp_timer_get_time(), measured_sample_ttl_us(s_snapshot));
    clock = copy;
    return ESP_OK;
}

// observation: Receives settings, orientation and brightness from one snapshot.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readCodyOptionsObservation(CodyOptionsObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    const auto &ui = s_snapshot.ui;
    observation = {s_snapshot.cody_preferences, ui.orientation, ui.configured_brightness_percent,
        ui.brightness.data_valid && !ui.brightness.data_stale};
    return ESP_OK;
}

// observation: Receives link and technical state; firmware identity is unavailable on the host.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readCodexTechnicalObservation(CodexTechnicalObservation &observation) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    CodexTechnicalObservation copy{};
    copy.link = s_snapshot.radios.codex_link;
    copy.technical = s_snapshot.codex_technical;
    codex_presence::age(copy.link.app_presence, esp_timer_get_time());
    observation = copy;
    return ESP_OK;
}

// status: Receives presence projected from the exact same canonical link.
// Exception: An unseeded owner leaves the destination untouched.
esp_err_t DeviceSnapshotService::readCodexPresenceStatus(CodexPresenceStatus &status) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    const auto &link = s_snapshot.radios.codex_link;
    const auto now = esp_timer_get_time();
    CodexPresenceStatus copy{};
    copy.codex_link = link;
    copy.codex_link_state = link.state;
    copy.pc_time_state = link.pc_time_state;
    copy.pc_time_error = link.pc_time_error;
    copy.codex_app_open = codex_presence::ready_current(link, now) && codex_presence::is_open(link.app_presence, now);
    status = copy;
    return ESP_OK;
}

// active: Whether the actual host codec is playing a native cue.
// mean_level: Observed output amplitude.
// sample_rate_hz: Original PCM sample rate.
// volume_percent: Actual host output volume requested by the owner.
// error: Original playback or codec error.
// cue_id: Exact original cue identity.
// Exception: Rejects invalid output or unavailable canonical state.
esp_err_t DeviceSnapshotService::reportPlayback(bool active, uint32_t mean_level, uint32_t sample_rate_hz,
    int volume_percent, esp_err_t error, uint32_t cue_id)
{
    if (error == ESP_OK && (sample_rate_hz == 0 || mean_level > 32768U || volume_percent < 0 || volume_percent > 100))
        return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto &playback = s_snapshot.audio.playback;
    const auto now = esp_timer_get_time();
    playback.cue_id = cue_id;
    if (error == ESP_OK) {
        snapshot_detail::mark_sample_success(playback, now);
        playback.state = active ? PlaybackState::Playing : PlaybackState::Idle;
        playback.level_valid = active;
        if (active) playback.mean_level = mean_level;
        playback.sample_rate_hz = sample_rate_hz;
        playback.volume_percent = static_cast<uint8_t>(volume_percent);
    } else {
        snapshot_detail::mark_sample_failure(playback, error, now);
        playback.state = PlaybackState::Failed;
        playback.level_valid = false;
    }
    ++s_snapshot.sequence;
    return ESP_OK;
}
} // namespace LUNASHELL
