#include "CODY_host_onboarding_snapshot.hpp"
#include "CODY_host_pairing.hpp"
#include "CODY_host_studio_statistics.hpp"
#include "cody_host_studio_events.hpp"
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
// A scenario-owned service read failure, retired on every subsequent publication.
esp_err_t s_studio_event_read_error = ESP_OK;
// The N01 fixture projects an unavailable inbox without erasing any host observation.
bool s_studio_unavailable_inbox = false;
// Static voice projections are host-owned and never overwrite live observations.
LUNASHELL::VoiceSnapshot s_studio_voice{};
bool s_studio_voice_active = false;

// target: Named static native voice presenter destination.
// phase: Receives the declared phase only when the target is recognized.
// Returns false without mutating phase for unsupported destinations.
bool studio_voice_target(const char *target, LUNASHELL::VoiceInputPhase &phase)
{
    struct Entry { const char *target; LUNASHELL::VoiceInputPhase phase; };
    static constexpr Entry entries[]{
        {"cody.voice.preparing", LUNASHELL::VoiceInputPhase::Preparing},
        {"cody.voice.listening", LUNASHELL::VoiceInputPhase::Listening},
        {"cody.voice.sending", LUNASHELL::VoiceInputPhase::Sending}};
    for (const auto &entry : entries) {
        if (std::strcmp(target, entry.target) == 0) { phase = entry.phase; return true; }
    }
    return false;
}

// target: Explicit synthetic dashboard scope requiring a current READY/app lease.
// Exception: No exceptions; no prefix matches admit an unrelated native page.
bool studio_counter_target(const char *target)
{
    static constexpr const char *targets[]{"cody.dashboard.today", "cody.dashboard.seven-days",
        "cody.dashboard.token-history", "cody.dashboard.credits", "cody.dashboard.account", "cody.dashboard.context",
        "cody.activity.current-turn"};
    for (const auto *candidate : targets) if (std::strcmp(target, candidate) == 0) return true;
    return false;
}

// target: Exact pure static link view, never a service action or browser scope.
// Exception: No exceptions; unknown targets have no synthetic link outcome.
const char *studio_static_codex_outcome(const char *target)
{
    struct Entry { const char *target; const char *outcome; };
    static constexpr Entry entries[]{
        {"cody.codex.pair-pc", "pairing"}, {"cody.codex.committed", "committed"},
        {"cody.codex.discovering", "discovering"}, {"cody.codex.connected", "ready"}};
    for (const auto &entry : entries) if (std::strcmp(target, entry.target) == 0) return entry.outcome;
    return nullptr;
}
}

namespace CODY::host_onboarding {
// request: Explicit Settings, animation, counter or static Codex-link observation input.
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
    const char *static_codex_outcome = studio_static_codex_outcome(identity.target);
    const bool studio_event = CODY::host_studio_events::is_target(identity.target);
    const bool studio_provisioning_debug = std::strcmp(identity.target, "cody.provisioning.debug") == 0;
    if (studio_event || studio_provisioning_debug) {
        LUNASHELL::HardwareStatus hardware{};
        const auto error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(hardware);
        if (error != ESP_OK) return error;
        const auto &wifi = hardware.wifi;
        if (!wifi.data_valid || wifi.data_stale || wifi.last_error != ESP_OK ||
            !wifi.connection_state_valid || !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0')
            return ESP_ERR_INVALID_ARG;
    }
    if (static_codex_outcome) {
        if (std::strcmp(request.codex_link_outcome, static_codex_outcome) != 0) return ESP_ERR_INVALID_ARG;
        LUNASHELL::HardwareStatus hardware{};
        const auto error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(hardware);
        if (error != ESP_OK) return error;
        const auto &wifi = hardware.wifi;
        if (!wifi.data_valid || wifi.data_stale || wifi.last_error != ESP_OK ||
            !wifi.connection_state_valid || !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0')
            return ESP_ERR_INVALID_ARG;
    }
    const bool statistics = studio_counter_target(identity.target);
    if (statistics) {
        if (std::strcmp(request.codex_link_outcome, "ready") != 0) return ESP_ERR_INVALID_ARG;
        LUNASHELL::HardwareStatus hardware{};
        const auto error = LUNASHELL::DeviceSnapshotService::instance().readHardwareStatus(hardware);
        if (error != ESP_OK) return error;
        const auto &wifi = hardware.wifi;
        if (!wifi.data_valid || wifi.data_stale || wifi.last_error != ESP_OK ||
            !wifi.connection_state_valid || !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0')
            return ESP_ERR_INVALID_ARG;
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
    } else if (!statistics && !static_codex_outcome && request.codex_link_outcome[0] != '\0')
        return ESP_ERR_INVALID_ARG;
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
    const char *target = request.identity.target;
    const bool studio_event = CODY::host_studio_events::is_target(target);
    // Host-only staging is a bounded caller-stack copy. Validate the complete
    // event and its native destination before touching the canonical owner.
    LUNASHELL::CodexEventSnapshot staged_event{};
    const auto *event_definition = CODY::host_studio_events::definition(target);
    if (studio_event && event_definition->observation_error == ESP_OK &&
        !CODY::host_studio_events::fill(target, now, staged_event))
        return ESP_ERR_INVALID_STATE;
    s_studio_event_read_error = studio_event ? event_definition->observation_error : ESP_OK;
    s_studio_statistics = false;
    s_studio_weekly = {};
    s_studio_account = {};
    s_snapshot.radios.pairing = {};
    s_snapshot.radios.provisioning = {};
    s_snapshot.radios.codex_link = {};
    // A preceding event never survives another Studio scenario's publication.
    s_snapshot.codex_event = {};
    s_studio_unavailable_inbox = std::strcmp(target, "cody.activity.events") == 0;
    s_studio_voice = {};
    LUNASHELL::VoiceInputPhase studio_voice_phase{};
    s_studio_voice_active = studio_voice_target(target, studio_voice_phase);
    if (s_studio_voice_active) {
        // The scenario is an observation, not microphone or transport activity.
        LUNASHELL::snapshot_detail::mark_sample_success(s_studio_voice, now);
        s_studio_voice.state = LUNASHELL::VoiceState::Listening;
        auto &input = s_studio_voice.input;
        input.phase = studio_voice_phase;
        input.trigger = LUNASHELL::VoiceInputTrigger::DeviceButton;
        input.started_at_us = input.changed_at_us = now;
        if (studio_voice_phase != LUNASHELL::VoiceInputPhase::Preparing) {
            std::memcpy(input.destination_title, "Synthetic chat", sizeof("Synthetic chat"));
            input.capturing_started_at_us = now;
        }
        if (studio_voice_phase == LUNASHELL::VoiceInputPhase::Sending)
            input.captured_samples = 16000;
    }
    s_wifi_scan = {};
    s_wifi_scan_available = false;
    // A static code is readable through the actual host service API only while
    // this explicitly selected Pair Codex case owns its canonical observation.
    configure_studio_codex_pairing_code(std::strcmp(target, "cody.codex.pair-pc") == 0);
    const bool studio_qr = std::strcmp(target, "cody.provisioning.qr") == 0;
    // The QR source is a host adapter projection, not an admitted BLE operation.
    configure_studio_pairing_qr(studio_qr);
    const bool local_wifi = std::strcmp(target, "cody.wifi.networks") == 0 ||
        std::strcmp(target, "cody.wifi.password") == 0 || std::strcmp(target, "cody.wifi.open-confirm") == 0 ||
        std::strcmp(target, "cody.wifi.connecting") == 0 || std::strcmp(target, "cody.wifi.connection-error") == 0;
    if (local_wifi) {
        s_wifi_scan_available = true;
        s_wifi_scan.state = LUNASHELL::WifiScanState::Complete;
        s_wifi_scan.generation = 1;
        s_wifi_scan.revision = 1;
        s_wifi_scan.completed_at_us = now;
        s_wifi_scan.count = 3;
        static constexpr const char *names[]{"SIM-STUDIO-SECURE", "SIM-STUDIO-OPEN", "SIM-STUDIO-GUEST"};
        static constexpr int8_t strengths[]{-45, -62, -76};
        static constexpr uint8_t channels[]{1, 6, 11};
        for (uint8_t index = 0; index < s_wifi_scan.count; ++index) {
            auto &network = s_wifi_scan.networks[index];
            std::strcpy(network.ssid, names[index]);
            network.bssid[0] = 0x02;
            network.bssid[5] = static_cast<uint8_t>(index + 1U);
            network.rssi = strengths[index];
            network.channel = channels[index];
            network.security = index == 1 ? LUNASHELL::WifiNetworkSecurity::Open :
                LUNASHELL::WifiNetworkSecurity::Wpa2Personal;
        }
    }
    const bool starting = std::strcmp(target, "cody.provisioning.starting") == 0;
    const bool connecting = std::strcmp(target, "cody.provisioning.connecting") == 0;
    const bool completing = std::strcmp(target, "cody.provisioning.completing") == 0;
    if (starting || connecting || completing || studio_qr) {
        auto &pairing = s_snapshot.radios.pairing;
        LUNASHELL::snapshot_detail::mark_sample_success(pairing, now);
        pairing.attempt_id = 1;
        pairing.changed_at_us = now;
        pairing.cleanup_error = pairing.dma_reservation_error = ESP_OK;
        pairing.state = starting ? LUNASHELL::PairingSessionState::Starting :
            connecting ? LUNASHELL::PairingSessionState::Connecting :
            completing ? LUNASHELL::PairingSessionState::Completing : LUNASHELL::PairingSessionState::ReadyToScan;
        if (studio_qr) {
            pairing.provisioning_session_id = 1;
            auto &provisioning = s_snapshot.radios.provisioning;
            LUNASHELL::snapshot_detail::mark_sample_success(provisioning, now);
            provisioning.initialized = provisioning.running = true;
            provisioning.session_id = 1;
            provisioning.state = LUNASHELL::ProvisioningState::WaitingForApp;
        }
    }
    const bool retained = std::strcmp(target, "cody.codex.retained-pc") == 0 ||
        std::strcmp(target, "cody.codex.forget-pc") == 0;
    const bool cleanup = std::strcmp(target, "cody.codex.cleanup") == 0;
    const bool forgotten = std::strcmp(target, "cody.codex.forgotten") == 0;
    if (retained || cleanup || forgotten) {
        auto &link = s_snapshot.radios.codex_link;
        LUNASHELL::snapshot_detail::mark_sample_success(link, now);
        link.attempt_id = 1;
        link.changed_at_us = now;
        link.state = retained ? LUNASHELL::CodexLinkState::Paired : LUNASHELL::CodexLinkState::Idle;
        link.retained_link_present = retained || cleanup;
        link.command_pending = cleanup;
        if (link.retained_link_present) {
            std::strcpy(link.device_id, "000000000001");
            std::strcpy(link.paired_pc_id, "11111111111111111111111111111111");
        }
    }
    const char *static_codex_outcome = studio_static_codex_outcome(target);
    if (static_codex_outcome) {
        auto &link = s_snapshot.radios.codex_link;
        LUNASHELL::snapshot_detail::mark_sample_success(link, now);
        link.attempt_id = 1;
        link.changed_at_us = now;
        std::strcpy(link.device_id, "000000000001");
        const bool pairing = std::strcmp(static_codex_outcome, "pairing") == 0;
        const bool ready = std::strcmp(static_codex_outcome, "ready") == 0;
        link.state = pairing ? LUNASHELL::CodexLinkState::Pairing :
            ready ? LUNASHELL::CodexLinkState::Connected :
            std::strcmp(static_codex_outcome, "discovering") == 0 ? LUNASHELL::CodexLinkState::Discovering :
            LUNASHELL::CodexLinkState::Paired;
        link.pairing_window_active = pairing;
        link.retained_link_present = !pairing;
        if (pairing) link.deadline_at_us = now + 180000000LL;
        else std::strcpy(link.paired_pc_id, "11111111111111111111111111111111");
        if (ready) {
            // Explicit synthetic READY/app lease, not a transport acknowledgement
            // or an inferred selected chat. No ACTIVITY or TECH fact is supplied.
            link.ready_at_us = now;
            link.pc_time_state = LUNASHELL::PcTimeSyncState::Applied;
            std::strcpy(link.peer_ip_address, "192.0.2.1");
            link.app_presence.state = LUNASHELL::CodexAppPresenceState::Open;
            link.app_presence.data_valid = true;
            link.app_presence.last_error = ESP_OK;
            link.app_presence.received_at_us = now;
            link.app_presence.sequence = 1;
        }
    }
    // Studio has no saved-credential or live-BLE base inputs. Retire the
    // preceding case's explicitly supplied setup context before publishing this
    // case; no acquisition, advertising or connection operation is started.
    s_snapshot.radios.wifi.credentials_configured = false;
    s_snapshot.radios.ble = {};
    if (std::strcmp(target, "cody.setup.wifi-guide") == 0 ||
        std::strcmp(target, "cody.setup.bluetooth-instructions") == 0 ||
        std::strcmp(target, "cody.provisioning.debug") == 0) {
        auto &ble = s_snapshot.radios.ble;
        LUNASHELL::snapshot_detail::mark_sample_success(ble, now);
        std::memcpy(ble.device_name, "AiGadget-000001", sizeof("AiGadget-000001"));
        // Name availability alone does not imply provisioning or a live link.
        // The Debug diagnostics sample explicitly supplies the completed host
        // handoff states its production presenter describes, with no radio work.
        if (std::strcmp(target, "cody.provisioning.debug") == 0) {
            ble.advertising = true;
            s_snapshot.radios.wifi.credentials_configured = true;
            auto &pairing = s_snapshot.radios.pairing;
            LUNASHELL::snapshot_detail::mark_sample_success(pairing, now);
            pairing.state = LUNASHELL::PairingSessionState::Online;
            pairing.attempt_id = 1;
            pairing.changed_at_us = now;
            auto &provisioning = s_snapshot.radios.provisioning;
            LUNASHELL::snapshot_detail::mark_sample_success(provisioning, now);
            provisioning.state = LUNASHELL::ProvisioningState::Stopped;
        }
    }
    if (std::strcmp(target, "cody.wifi.connecting") == 0 ||
        std::strcmp(target, "cody.wifi.connection-error") == 0) {
        auto &wifi = s_snapshot.radios.wifi;
        std::strcpy(wifi.ssid, "SIM-STUDIO-SECURE");
        wifi.connect_requested = std::strcmp(target, "cody.wifi.connecting") == 0;
        if (!wifi.connect_requested)
            LUNASHELL::snapshot_detail::mark_sample_failure(wifi, ESP_ERR_TIMEOUT, now);
    }
    if (std::strcmp(request.identity.target, "cody.setup.online") == 0) {
        // This synthetic result represents a successfully retained local Wi-Fi
        // profile, not an ephemeral association. Publish configuration with the
        // same canonical owner as connection validity and the observed SSID.
        auto &wifi = s_snapshot.radios.wifi;
        if (!wifi.data_valid || wifi.data_stale || !wifi.connection_state_valid ||
            !wifi.connected || !wifi.ip_acquired || wifi.ssid[0] == '\0') return ESP_ERR_INVALID_STATE;
        wifi.credentials_configured = true;
    }
    const bool reset = std::strcmp(request.identity.target, "cody.dashboard.reset") == 0;
    const bool limits = std::strcmp(request.identity.target, "cody.dashboard.limits") == 0;
    if (reset || limits || studio_counter_target(target) || studio_event) {
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
        if (studio_event && s_studio_event_read_error == ESP_OK) s_snapshot.codex_event = staged_event;
        if (reset || limits) {
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
// level: Bounded synthetic PCM mean used only by an isolated host regression worker.
// Exception: Rejects active AFE, nonstatic clocks and invalid levels before mutating the host owner.
CODY_HOST_SENSOR_API esp_err_t cody_host_studio_test_microphone_seed(uint32_t level)
{
    if (level > 32767) return ESP_ERR_INVALID_ARG;
    if (cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000 ||
        LUNASHELL_voice_afe_running() || LUNASHELL_voice_afe_capture_active()) return ESP_ERR_INVALID_STATE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    auto &audio = s_snapshot.audio;
    LUNASHELL::snapshot_detail::mark_sample_success(audio.microphone, esp_timer_get_time());
    audio.channel_count = 1;
    audio.sample_rate_hz = 16000;
    audio.channel_mean_level[0] = audio.microphone_mean_level = level;
    audio.channel_peak_level[0] = audio.microphone_peak_level = level;
    ++s_snapshot.sequence;
    return ESP_OK;
}

// stored: Receives the synthetic owner level, never a pointer into the snapshot.
// projected: Receives the level on the canonical motion projection.
// valid: Receives whether its microphone is current and valid.
// error: Receives the projection's original microphone error.
// playback_preserved: Receives whether key playback facts match the host owner.
// Exception: Rejects null outputs and preserves the original observation error.
CODY_HOST_SENSOR_API esp_err_t cody_host_studio_test_microphone_read(
    uint32_t *stored, uint32_t *projected, uint8_t *valid, int32_t *error, uint8_t *playback_preserved)
{
    if (!stored || !projected || !valid || !error || !playback_preserved) return ESP_ERR_INVALID_ARG;
    LUNASHELL::MotionPowerObservation motion{};
    const auto result = LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(motion);
    if (result != ESP_OK) return result;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    const auto &raw = s_snapshot.audio;
    *stored = raw.channel_mean_level[0];
    *projected = motion.audio.channel_mean_level[0];
    *valid = motion.audio.microphone.data_valid && !motion.audio.microphone.data_stale;
    *error = motion.audio.microphone.last_error;
    *playback_preserved = motion.audio.playback.state == raw.playback.state &&
        motion.audio.playback.last_error == raw.playback.last_error &&
        motion.audio.playback.cue_id == raw.playback.cue_id &&
        motion.audio.playback.volume_percent == raw.playback.volume_percent;
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
// account: Receives the applicable canonical synthetic account-owner projection.
// Exception: Preserves owner scope/aging errors and retains the pilot fallback when unsupported.
esp_err_t DeviceSnapshotService::readCodexAccount(CodexAccountSnapshot &account) const
{
    // The statistics helper ages its own canonical records against presence and
    // therefore must run before acquiring the device snapshot owner lock.
    const auto studio_error = CODY::host_studio_statistics::read_account(account);
    if (studio_error != ESP_ERR_NOT_SUPPORTED) return studio_error;
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
// vouchers: Receives the current synthetic canonical voucher-owner projection.
// Exception: Preserves unavailable, stale-source and scope errors from that owner.
esp_err_t DeviceSnapshotService::readCodexVouchers(CodexVoucherSnapshot &vouchers) const
{ return CODY::host_studio_statistics::read_vouchers(vouchers); }
esp_err_t DeviceSnapshotService::readCodexSaved(CodexSavedSnapshot &) const { return ESP_ERR_NOT_SUPPORTED; }
// intervals: Receives the bounded synthetic canonical history-owner projection.
// Exception: Preserves unavailable, stale-source and scope errors from that owner.
esp_err_t DeviceSnapshotService::readCodexTokenIntervals(CodexTokenIntervalsSnapshot &intervals) const
{ return CODY::host_studio_statistics::read_intervals(intervals); }
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
    copy.voice = s_studio_voice_active ? s_studio_voice : s_snapshot.voice;
    if (s_studio_voice_active) {
        // This fixture explicitly has no microphone: preserve the canonical
        // host sample and unrelated playback, but never publish it as current.
        copy.audio.microphone = {};
        copy.audio.microphone.last_error = ESP_ERR_NOT_SUPPORTED;
    }
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
    auto copy = s_studio_voice_active ? s_studio_voice : s_snapshot.voice;
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
CODY_HOST_SNAPSHOT_READ(readCodexInboxObservation, CodexInboxSnapshot,
    s_studio_unavailable_inbox ? CodexInboxSnapshot{} : s_snapshot.codex_inbox)
CODY_HOST_SNAPSHOT_READ(readDisplayFrameObservation, DisplayFrameSnapshot, s_snapshot.ui.rendering)
CODY_HOST_SNAPSHOT_READ(readPowerState, PowerManagementSnapshot, s_snapshot.power)
CODY_HOST_SNAPSHOT_READ(readCodyIdleObservation, CodyIdleObservation, s_snapshot.cody_idle)
CODY_HOST_SNAPSHOT_READ(readCodyEventVisualObservation, CodyEventVisualObservation, s_snapshot.cody_event_visual)
CODY_HOST_SNAPSHOT_READ(readCodyParallaxObservation, CodyParallaxObservation, s_snapshot.cody_parallax)
CODY_HOST_SNAPSHOT_READ(readCodyCharacterThemeObservation, CodyCharacterThemeObservation, s_snapshot.cody_character_theme)
#undef CODY_HOST_SNAPSHOT_READ

// output: Receives the canonical host event only after a successful owner read.
// Exception: Returns the scenario's explicit read error without changing output.
esp_err_t DeviceSnapshotService::readCodexEventObservation(CodexEventSnapshot &output) const
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_seeded) return ESP_ERR_NOT_FOUND;
    if (s_studio_event_read_error != ESP_OK) return s_studio_event_read_error;
    output = s_snapshot.codex_event;
    return ESP_OK;
}

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
    const auto now = esp_timer_get_time();
    codex_presence::age(copy.link.app_presence, now);
    age_codex_technical(copy.technical, now);
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
