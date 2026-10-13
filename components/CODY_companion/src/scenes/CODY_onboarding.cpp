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
 *
 * VBWR E */

#include "CODY_onboarding.hpp"
#include "CODY_activity_description.hpp"
#include "CODY_activity_identity.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_observation.hpp"
#include "CODY_codex_event_presentation.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

#include "CODY2D_controls.hpp"
#include "CODY_jelly.hpp"
#include "CODY_look_screen.hpp"
#include "CODY_settings.hpp"
#include "CODY_codex_audio.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "scene_data.hpp"
#include "navigation_data.hpp"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "qrcode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace CODY {
// scene: Worker-owned setup destination selected from canonical radio state.
// changed: Receives whether the startup decision requires a new frame.
// Exception: Returns the original snapshot, route or admission failure.
esp_err_t evaluate_saved_setup(SceneFrame &scene, bool &changed);

namespace {
constexpr char kTag[] = "CODY_onboarding";
constexpr char kInstallUrl[] = "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr uint16_t kQrModulesMax = 57; // Version 10; bounded independently of the framebuffer.
constexpr uint16_t kQrBytes = (kQrModulesMax * kQrModulesMax + 7U) / 8U;
constexpr uint16_t kQrQuietModules = 4;
constexpr CODY2D::Rect kInstallQrBounds{113, 140, 240, 240};
constexpr CODY2D::Rect kSessionQrBounds{131, 182, 204, 204};
constexpr uint16_t kSessionCaptionTop = 156;
constexpr CODY2D::Rect kFooterBounds{123, 390, 220, 44};
constexpr CODY2D::Rect kSingleActionBounds{113, 352, 240, 72};
constexpr CODY2D::Rect kLeftCardBounds{73, 260, 154, 104};
constexpr CODY2D::Rect kRightCardBounds{239, 260, 154, 104};
constexpr CODY2D::Rect kOptionsBounds{153, 376, 160, 52};
// The 40-pixel title ends at 186; five rows end at 376 before the 390 footer.
constexpr uint16_t kPreferenceTop = 190;
constexpr uint16_t kPreferenceHeight = 34;
constexpr uint16_t kPreferenceStep = 38;
constexpr uint16_t kEventCaptionWidth = 300;
constexpr uint16_t kCaptionWidth = 232;
// The invitation fits the 232-pixel body slot without implying a selected chat.
constexpr char kCodexInvitationCaption[] = "Ready to team up";
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
constexpr uint16_t kDebugTextWidth = 386;
constexpr uint16_t kDebugTextColor = 0xF7BE; // CSS whitesmoke (#F5F5F5) in native RGB565.
constexpr uint8_t kPairingDebugLineCount = 6;
#endif
constexpr uint8_t kContentLineCapacity = 8;
constexpr size_t kDebugLineBytes = 64;
constexpr CODY2D::Rect kForwardBounds{396, 203, 60, 60};
constexpr CODY2D::Rect kBackBounds{10, 203, 60, 60};
constexpr int64_t kHelloReadableHoldUs = 1000000;
constexpr int64_t kStartupWifiGraceUs = 15LL * 1000000LL;
constexpr bool kPairingDebugEnabled = LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled;
constexpr bool kCodexIntegrationEnabled = CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX;
using PairState = LUNASHELL::PairingSessionState;
using CodexState = LUNASHELL::CodexLinkState;

using event_presentation::Route;
using event_presentation::EventStage;
using event_presentation::EventRetry;
using event_presentation::is_codex_event_route;
enum class Work : uint8_t {
    InstallQr,
    SessionQr,
    Start,
    Retry,
    Cancel,
    SaveLook,
    CompleteFirstBoot,
    CodexStart,
    CodexReplace,
    CodexCopyCode,
    CodexDiscover,
    CodexCancel,
    CodexForget,
    SavePreference,
};
enum class NavigationDirection : uint8_t { Previous, Next };
enum class CodexConnectionIntent : uint8_t { Setup, Retry, ConfirmReplacement };
enum class CodexCleanup : uint8_t { None, Cancel, Forget };
enum class OnboardingAction : uint8_t {
    None, ShowQr, PairCody, Cancel, Retry, Continue, Next, Done, LetsGo,
    AcceptCodex, DeclineCodex, ConnectCodex, NetworkSetup, Later, Options, KeepPairing, ReplacePairing,
    ForgetPairing, ConfirmForget, KeepSaved,
    OpenEvent = 64, ViewFailures = 65, SendReply = 66, DismissEvent = 67, RetryEvent = 68
};
enum class GestureAxis : uint8_t { Undecided, Horizontal, Vertical };

struct ScreenInactivityPolicy {
    Route route;
    int64_t timeout_us;
    Route destination;
};
constexpr ScreenInactivityPolicy kScreenInactivityPolicies[] = {
    {Route::Invitation05, 30LL * 1000000LL, Route::Hello04},
    {Route::Install06, 30LL * 1000000LL, Route::Hello04},
    {Route::Pairing07, 90LL * 1000000LL, Route::Hello04},
};
struct ScreenActivity {
    Route route = Route::None;
    int64_t last_activity_us = -1;
};
struct PairingProgress {
    bool valid = false;
    PairState state = PairState::Idle;
    LUNASHELL::ProvisioningState provisioning_state = LUNASHELL::ProvisioningState::Idle;
    uint32_t provisioning_events = 0;
    uint32_t ble_connections = 0;
    bool ble_connected = false;
    bool wifi_connected = false;
    bool wifi_has_ip = false;
};
struct TimeoutCleanup {
    bool pending = false;
    bool awaiting_start_result = false;
    bool cancel_requested = false;
    uint32_t retired_generation = 0;
    uint32_t attempt = 0;
};
struct QrModules {
    uint8_t bits[kQrBytes]{};
    uint16_t side = 0;
};
struct Command {
    Work work = Work::InstallQr;
    uint32_t generation = 0;
    uint32_t attempt = 0;
    uint32_t session = 0;
    LUNASHELL::CharacterLookSaveSnapshot look_save{};
    PreferenceId preference = PreferenceId::ShakeWake;
    uint32_t preference_value = 0;
};
struct Result {
    Command command{};
    esp_err_t error = ESP_OK;
    esp_err_t publication_error = ESP_OK;
    QrModules qr{};
    char codex_code[7]{};
};
struct PreferenceControl {
    PreferenceId identity = PreferenceId::ShakeWake;
    CODY2D::CapsuleControl control{};
    CODY2D::TextLine value{};
    // Include the terminator for every value of the canonical uint16_t angle.
    char value_text[6]{};
    uint32_t requested_value = 0;
};
struct EventChoiceControl {
    char id[13]{};
    CODY2D::CapsuleControl control{};
    CODY2D::TextLine second{};
};

struct ContentView {
    CODY2D::TextLine lines[kContentLineCapacity]{};
    uint8_t line_count = 0;
    uint16_t visible_characters[kContentLineCapacity]{};
    bool instant_lines[kContentLineCapacity]{};
    CODY2D::CapsuleControl action{};
    bool action_visible = false;
    bool actions_visible = false;
    uint16_t action_visible_characters = 0;
    OnboardingAction action_id = OnboardingAction::None;
    CODY2D::CapsuleControl alternative{};
    uint16_t alternative_visible_characters = 0;
    OnboardingAction alternative_id = OnboardingAction::None;
    CODY2D::TextLine action_second_line{};
    CODY2D::TextLine alternative_second_line{};
    uint16_t action_second_visible = 0;
    uint16_t alternative_second_visible = 0;
    CODY2D::CapsuleControl options{};
    OnboardingAction options_id = OnboardingAction::None;
    uint16_t options_visible_characters = 0;
    PreferenceControl preferences[5]{};
    uint8_t preference_count = 0;
    EventChoiceControl event_choices[3]{};
    uint8_t event_choice_count = 0;
    QrModules qr{};
    CODY2D::Rect qr_bounds{};
    char error_text[32]{};
    char debug_text[kContentLineCapacity][kDebugLineBytes]{};
    // Serialized scene-owned metadata and one bounded font window, outside the UI task stack.
    ActivityDescriptionState activity_description{};
    ActivityIdentityState activity_identity{};
};
#ifdef ESP_PLATFORM
// IDF constructs the default view after PSRAM startup; host fixtures use ordinary RAM.
alignas(ContentView) EXT_RAM_BSS_ATTR uint8_t s_view_storage[sizeof(ContentView)]{};
ContentView &s_view = *new (s_view_storage) ContentView{};
#else
ContentView s_view;
#endif
#ifdef ESP_PLATFORM
// One canonical observation stays outside the nested display worker call chain.
alignas(LUNASHELL::CodexTechnicalObservation) EXT_RAM_BSS_ATTR
    uint8_t s_technical_observation_storage[sizeof(LUNASHELL::CodexTechnicalObservation)]{};
LUNASHELL::CodexTechnicalObservation &s_technical_observation =
    *new (s_technical_observation_storage) LUNASHELL::CodexTechnicalObservation{};
#else
LUNASHELL::CodexTechnicalObservation s_technical_observation{};
#endif
LUNASHELL::CodexCompactionState s_presented_compaction = LUNASHELL::CodexCompactionState::None;
bool s_presented_compaction_feedback = false;
Route s_route = Route::None;
bool s_options_from_setup = false;
esp_err_t s_preference_error = ESP_OK;
PreferenceId s_requested_preference = PreferenceId::ShakeWake;
uint32_t s_requested_preference_value = 0;
LUNASHELL::CodyOptionsObservation s_presented_options{};
bool s_setup_returns_to_companion = false;
bool s_companion_seen_selection = false;
int64_t s_session_ended_at_us = -1;
LUNASHELL::CodexActivitySnapshot s_presented_activity{};
CommandHintsState s_command_hints{};
HomeReadyDwell s_home_ready_dwell{};
HomeReplyDwell s_home_reply_dwell{};
uint32_t s_activity_description_revision = 0;
uint32_t s_activity_identity_revision = 0;
ActivityObservationState s_activity_observation{};
LUNASHELL::CodexActivityState s_presented_work_phase = LUNASHELL::CodexActivityState::Working;
LUNASHELL::CodexEventSnapshot s_presented_event{};
EventStage s_event_stage = EventStage::Notice;
EventRetry s_event_retry = EventRetry::None;
esp_err_t s_event_ui_error = ESP_OK;
esp_err_t s_event_observation_error = ESP_OK;
bool s_event_online = false;
bool s_event_reply_admitted = false;
bool s_event_dismiss_admitted = false;
char s_event_selected_id[13]{};
uint8_t s_event_failure_page = 0;
// Display-owner buffer avoids putting the complete presentation plan on its task stack.
event_presentation::Plan s_event_plan{};
int64_t s_checks_passed_readable_at_us = -1;
char s_last_event_audio_request[33]{};
char s_last_delivered_audio_request[33]{};
// Cue history survives Setup re-entry; hidden status is never replayed as a new action.
char s_last_audio_chat[65]{};
LUNASHELL::CodexActivityState s_last_audio_phase = LUNASHELL::CodexActivityState::None;
bool s_startup_pending = false;
esp_err_t s_startup_wifi_error = ESP_OK;
int64_t s_startup_wifi_wait_started_us = -1;
bool s_startup_network_passive = false;
bool s_startup_network_presented = false;
uint32_t s_generation = 1;
std::atomic<uint32_t> s_presentation_generation{1};
uint32_t s_look_request_id = 0; // Never reset with a route; late writes keep their identity.
uint32_t s_revision = 0;
uint32_t s_navigation_revision = 0;
uint32_t s_text_generation = 0;
uint32_t s_attempt = 0;
uint32_t s_session = 0;
bool s_command_pending = false;
bool s_cancel_to_install = false;
bool s_retry_to_invitation = false; // 08 requested cleanup; only confirmed Cancelled may enter 05.
bool s_pairing_owned = false;
bool s_online_link = false;
bool s_online_presentation_current = false;
bool s_first_boot_persisted = false;
bool s_pairing_debug_dismissed = false;
int64_t s_hello_completed_at_us = -1;
int64_t s_last_pairing_change_us = -1;
ScreenActivity s_screen_activity{};
PairingProgress s_pairing_progress{};
TimeoutCleanup s_timeout_cleanup{};
bool s_hello_waits_for_touch = false;
bool s_activity_touch_pressed = false;
CODY2D::Point s_activity_touch_point{};
uint32_t s_codex_attempt = 0;
int64_t s_last_codex_change_us = -1;
bool s_codex_owned = false;
uint32_t s_codex_replacement_attempt = 0;
bool s_codex_manage_pending = false;
bool s_codex_manage_repair = false;
CodexCleanup s_codex_cleanup = CodexCleanup::None;
bool s_codex_cleanup_admitted = false;
bool s_codex_error_visible = false;
bool s_codex_error_allows_setup = false;
esp_err_t s_codex_visible_error = ESP_OK;
char s_codex_code[7]{};
esp_err_t s_visible_error = ESP_OK;
Work s_failed_work = Work::Start;
bool s_local_failure = false;
bool s_swipe_active = false;
CODY2D::Point s_swipe_start{};
CODY2D::Point s_swipe_last{};
Route s_swipe_route = Route::None;
uint16_t s_swipe_rotation = 0;
GestureAxis s_swipe_axis = GestureAxis::Undecided;

// A single-slot mailbox keeps blocking radio admission and QR encoding off the
// display worker. Release/acquire transfers ownership; no per-frame queue allocation.
TaskHandle_t s_worker = nullptr;
StaticTask_t s_worker_control{};
#ifdef ESP_PLATFORM
// Actions request radio work and delegate all persisted writes to the internal NVS executor.
EXT_RAM_BSS_ATTR StackType_t s_worker_stack[4096]{};
#else
StackType_t s_worker_stack[4096]{};
#endif
std::atomic<bool> s_work_busy{false};
std::atomic<bool> s_result_ready{false};
Command s_command{};
#ifdef ESP_PLATFORM
alignas(Result) EXT_RAM_BSS_ATTR uint8_t s_result_storage[sizeof(Result)]{};
Result &s_result = *new (s_result_storage) Result{};
#else
Result s_result{};
#endif
QrModules *s_encoding_target = nullptr; // Only the action worker calls the QR library.

// route: Stable route identity whose bounded local inactivity policy is requested.
const ScreenInactivityPolicy *screen_inactivity_policy(Route route)
{
    for (const auto &policy : kScreenInactivityPolicies) {
        if (policy.route == route) {
            return &policy;
        }
    }
    return nullptr;
}

// active_time_us: Awake route clock, including animation pause and excluding standby.
// Exception: Rejects a negative or regressing clock rather than inventing elapsed time.
esp_err_t synchronize_screen_activity(int64_t active_time_us)
{
    if (active_time_us < 0 || (s_screen_activity.last_activity_us >= 0 &&
        active_time_us < s_screen_activity.last_activity_us)) {
        ESP_LOGE(kTag, "Screen activity clock rejected: current=%lld previous=%lld (%s)",
            static_cast<long long>(active_time_us),
            static_cast<long long>(s_screen_activity.last_activity_us),
            esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_screen_activity.route != s_route) {
        s_screen_activity = {s_route, active_time_us};
        s_pairing_progress = {};
    }
    return ESP_OK;
}

// active_time_us: Real interaction or canonical progress time on the awake route clock.
// Exception: Returns the original clock error without refreshing inactivity.
esp_err_t refresh_screen_activity(int64_t active_time_us)
{
    const esp_err_t error = synchronize_screen_activity(active_time_us);
    if (error != ESP_OK) {
        return error;
    }
    if (screen_inactivity_policy(s_route) != nullptr) {
        s_screen_activity.last_activity_us = active_time_us;
    }
    return ESP_OK;
}

// inactivity_policy: Current screen 07 policy that must match its authored scene interval.
// Exception: Returns the original scene or power error; interval disagreement is invalid state.
esp_err_t refresh_pairing_standby(const ScreenInactivityPolicy &inactivity_policy)
{
    const SceneDefinition *definition = nullptr;
    const esp_err_t scene_error = find_scene(SceneId::Pairing, definition);
    if (scene_error != ESP_OK) {
        return scene_error;
    }
    if (definition == nullptr ||
        static_cast<int64_t>(definition->standby_override_seconds) * 1000000LL !=
            inactivity_policy.timeout_us) {
        ESP_LOGE(kTag, "Pairing standby and inactivity intervals disagree (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // Only canonical progress calls this: real Touch already extends the power
    // owner's standby reference and must not republish/log every dragged point.
    return LUNASHELL_power_manager_set_scene_standby_override(
        definition->standby_override_seconds);
}

// observation: One canonical connectivity snapshot; no radio or sensor reads occur here.
// active_time_us: Awake route clock on which genuine progress refreshes the policy.
// Exception: Returns the original activity error without consuming a failed progress refresh.
esp_err_t observe_pairing_progress(const LUNASHELL::ConnectivityObservation &observation,
                                   int64_t active_time_us)
{
    const auto *inactivity_policy = screen_inactivity_policy(s_route);
    if (inactivity_policy == nullptr) {
        return ESP_OK;
    }
    const auto &pairing = observation.radios.pairing;
    const bool owned_attempt = s_pairing_owned && pairing.attempt_id == s_attempt;
    const bool admitted_pending_start = s_command_pending && s_command.generation == s_generation &&
        (s_command.work == Work::Start || s_command.work == Work::Retry) &&
        s_command.attempt < UINT32_MAX && pairing.attempt_id == s_command.attempt + 1U;
    if ((!owned_attempt && !admitted_pending_start) || !pairing.data_valid || pairing.data_stale) {
        return ESP_OK;
    }
    const bool progressing_state = pairing.state == PairState::Starting ||
        pairing.state == PairState::ReadyToScan || pairing.state == PairState::Connecting ||
        pairing.state == PairState::Completing || pairing.state == PairState::Online;
    if (!progressing_state) {
        return ESP_OK;
    }
    // Keep only the last observed transition signature when an optional field is
    // unavailable. Recovery of validity alone must not invent pairing progress.
    PairingProgress current = s_pairing_progress;
    current.valid = true;
    current.state = pairing.state;
    const auto &provisioning = observation.radios.provisioning;
    if (provisioning.data_valid && !provisioning.data_stale &&
        provisioning.session_id == pairing.provisioning_session_id) {
        current.provisioning_state = provisioning.state;
        current.provisioning_events = provisioning.event_sequence;
    }
    const auto &ble = observation.radios.ble;
    if (ble.data_valid && !ble.data_stale) {
        current.ble_connected = ble.connected;
        current.ble_connections = ble.connection_count;
    }
    const auto &wifi = observation.radios.wifi;
    if (wifi.data_valid && !wifi.data_stale && wifi.last_error == ESP_OK) {
        current.wifi_connected = wifi.connected;
        current.wifi_has_ip = wifi.ip_acquired && wifi.ip_address[0] != '\0';
    }
    const bool credential_progress = current.provisioning_state == LUNASHELL::ProvisioningState::CredentialsReceived ||
        current.provisioning_state == LUNASHELL::ProvisioningState::Connecting ||
        current.provisioning_state == LUNASHELL::ProvisioningState::Success;
    const bool progressed = !s_pairing_progress.valid || current.state != s_pairing_progress.state ||
        (credential_progress && (current.provisioning_state != s_pairing_progress.provisioning_state ||
            current.provisioning_events != s_pairing_progress.provisioning_events)) ||
        (current.ble_connected && (!s_pairing_progress.ble_connected ||
            current.ble_connections != s_pairing_progress.ble_connections)) ||
        (current.wifi_connected && !s_pairing_progress.wifi_connected) ||
        (current.wifi_has_ip && !s_pairing_progress.wifi_has_ip);
    if (progressed) {
        esp_err_t error = synchronize_screen_activity(active_time_us);
        if (error == ESP_OK && s_route == Route::Pairing07) {
            error = refresh_pairing_standby(*inactivity_policy);
        }
        if (error == ESP_OK) {
            error = refresh_screen_activity(active_time_us);
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    s_pairing_progress = current;
    return ESP_OK;
}

// bytes: Owned storage that may contain a proof-of-possession-derived QR.
// size: Exact writable byte count; volatile stores are not optimized away.
void wipe(void *bytes, size_t size)
{
    volatile uint8_t *cursor = static_cast<volatile uint8_t *>(bytes);
    while (size-- != 0) {
        *cursor++ = 0;
    }
}

// handle: Library-owned QR, borrowed only for this synchronous callback.
void copy_qr_modules(esp_qrcode_handle_t handle)
{
    if (handle == nullptr || s_encoding_target == nullptr) {
        return;
    }
    const int side = esp_qrcode_get_size(handle);
    if (side < 21 || side > kQrModulesMax) {
        return;
    }
    s_encoding_target->side = static_cast<uint16_t>(side);
    for (uint16_t row = 0; row < side; ++row) {
        for (uint16_t column = 0; column < side; ++column) {
            if (esp_qrcode_get_module(handle, column, row)) {
                const uint32_t bit = row * side + column;
                s_encoding_target->bits[bit / 8U] |= 1U << (bit % 8U);
            }
        }
    }
}

// payload: Counted by the underlying library; never logged or published in snapshots.
// qr: Receives compact modules, never a full RGB framebuffer.
esp_err_t encode_qr(const char *payload, QrModules &qr)
{
    wipe(&qr, sizeof(qr));
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    config.display_func = copy_qr_modules; // Never use the console QR callback.
    s_encoding_target = &qr;
    const esp_err_t error = esp_qrcode_generate(&config, payload);
    s_encoding_target = nullptr;
    if (error != ESP_OK) {
        return error;
    }
    return qr.side != 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// command: Typed action and attempt/session identities acquired by the route owner.
// result: Original operation result, separate snapshot publication error and optional QR.
// Exception: The result retains every persistence, snapshot and radio failure.
void execute_command(const Command &command, Result &result)
{
    result.command = command;
    if (command.work == Work::CompleteFirstBoot) {
        LUNASHELL::ConnectivityObservation observation;
        result.error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (result.error != ESP_OK) {
            return;
        }
        const auto &pairing = observation.radios.pairing;
        const auto &wifi = observation.radios.wifi;
        if (!pairing.data_valid || pairing.data_stale || pairing.state != PairState::Online ||
            pairing.attempt_id != command.attempt || !wifi.data_valid || wifi.data_stale ||
            wifi.health != LUNASHELL::DeviceHealth::Ready || wifi.last_error != ESP_OK ||
            !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0') {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag,
                "First-boot completion rejected: pairing-valid=%u stale=%u state=%u attempt=%lu/%lu "
                "wifi-valid=%u stale=%u health=%u error=%s connected=%u ip=%u (%s)",
                pairing.data_valid, pairing.data_stale, to_index(pairing.state),
                static_cast<unsigned long>(pairing.attempt_id),
                static_cast<unsigned long>(command.attempt), wifi.data_valid, wifi.data_stale,
                to_index(wifi.health), esp_err_to_name(wifi.last_error), wifi.connected,
                wifi.ip_acquired, esp_err_to_name(result.error));
            return;
        }
        // A queued write belongs to its admitted presentation generation. Once
        // NVS starts it keeps ownership; retirement can only reject it beforehand.
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGW(kTag, "First-boot write retired before persistence: attempt=%lu generation=%lu (%s)",
                static_cast<unsigned long>(command.attempt),
                static_cast<unsigned long>(command.generation), esp_err_to_name(result.error));
            return;
        }
        result.error = persist_first_boot(false);
        return;
    }
    if (command.work == Work::SavePreference) {
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "Preference write retired before persistence (%s)", esp_err_to_name(result.error));
            return;
        }
        result.error = persist_companion_preference(command.preference, command.preference_value);
        return;
    }
    if (command.work == Work::SaveLook) {
#if CODY_ENABLE_PEARL_JELLY
        result.error = persist_jelly_skin(command.look_save.requested_id);
#else
        result.error = ESP_ERR_NOT_SUPPORTED;
#endif
        auto completed = command.look_save;
        completed.state = result.error == ESP_OK
            ? LUNASHELL::CharacterLookSaveSnapshot::State::Saved
            : LUNASHELL::CharacterLookSaveSnapshot::State::Failed;
        completed.error = result.error;
        completed.completed_at_us = esp_timer_get_time();
        result.publication_error =
            LUNASHELL::DeviceSnapshotService::instance().reportCharacterLookSave(completed);
        return;
    }
    if (command.work == Work::InstallQr) {
        result.error = encode_qr(kInstallUrl, result.qr);
        return;
    }
    if (command.work == Work::SessionQr) {
        LUNASHELL::ConnectivityObservation observation;
        result.error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (result.error != ESP_OK) {
            return;
        }
        const auto &pairing = observation.radios.pairing;
        if (!pairing.data_valid || pairing.data_stale || pairing.state != PairState::ReadyToScan ||
            pairing.attempt_id != command.attempt || pairing.provisioning_session_id != command.session) {
            result.error = ESP_ERR_INVALID_STATE;
            return;
        }
        char payload[192]{};
        result.error = LUNASHELL_provisioning_copy_qr(payload, sizeof(payload), command.session);
        if (result.error == ESP_OK) {
            result.error = encode_qr(payload, result.qr);
        }
        wipe(payload, sizeof(payload));
        return;
    }
    if (command.work == Work::CodexCopyCode) {
        result.error = LUNASHELL_codex_link_copy_pairing_code(
            result.codex_code,
            sizeof(result.codex_code),
            command.attempt
        );
        return;
    }
    if (command.work == Work::CodexStart || command.work == Work::CodexReplace ||
        command.work == Work::CodexDiscover || command.work == Work::CodexCancel ||
        command.work == Work::CodexForget) {
        LUNASHELL_codex_link_action_t action = LUNASHELL_CODEX_LINK_ACTION_START_PAIRING;
        switch (command.work) {
        case Work::CodexReplace: action = LUNASHELL_CODEX_LINK_ACTION_REPLACE_PAIRING; break;
        case Work::CodexDiscover: action = LUNASHELL_CODEX_LINK_ACTION_START_DISCOVERY; break;
        case Work::CodexCancel: action = LUNASHELL_CODEX_LINK_ACTION_CANCEL; break;
        case Work::CodexForget: action = LUNASHELL_CODEX_LINK_ACTION_FORGET_PAIRING; break;
        default: break;
        }
        result.error = LUNASHELL_codex_link_request(action, command.attempt);
        return;
    }
    LUNASHELL_pairing_action_t action;
    switch (command.work) {
    case Work::Start:
        action = LUNASHELL_PAIRING_ACTION_START;
        break;
    case Work::Retry:
        action = LUNASHELL_PAIRING_ACTION_RETRY;
        break;
    case Work::Cancel:
        action = LUNASHELL_PAIRING_ACTION_CANCEL;
        break;
    default:
        result.error = ESP_ERR_INVALID_ARG;
        return;
    }
    result.error = LUNASHELL_pairing_request(action, command.attempt);
}

// context: Unused; one worker owns blocking radio, QR and preference operations.
// Exception: Operation and publication errors remain separate in the result mailbox.
void action_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // The acquire pairs with enqueue's release, before reading its mailbox.
        if (!s_work_busy.load(std::memory_order_acquire)) {
            continue;
        }
        Result completed{};
        execute_command(s_command, completed);
        if (completed.error != ESP_OK || completed.publication_error != ESP_OK) {
            ESP_LOGE(kTag, "Action %u attempt=%lu session=%lu failed: operation=%s publication=%s",
                to_index(completed.command.work), static_cast<unsigned long>(completed.command.attempt),
                static_cast<unsigned long>(completed.command.session), esp_err_to_name(completed.error),
                esp_err_to_name(completed.publication_error));
        }
        s_result = completed;
        wipe(&completed, sizeof(completed));
        s_result_ready.store(true, std::memory_order_release);
    }
}

// work: Explicit radio, Codex-link or QR preparation action.
// attempt: Current canonical attempt identity; never an inferred UI list position.
// session: Current canonical provisioning-session identity, or zero for other actions.
esp_err_t enqueue(Work work, uint32_t attempt, uint32_t session = 0)
{
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Action mailbox unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_command = {work, s_generation, attempt, session};
    s_command_pending = true;
    ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// preference: Stable identity of the actual row pressed, never a row index.
// value: Exact user-selected target, also retained for an explicit Retry.
// Exception: Returns mailbox admission failure without starting a write.
esp_err_t enqueue_preference(PreferenceId preference, uint32_t value)
{
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Preference mailbox unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_command = {};
    s_command.work = Work::SavePreference;
    s_command.generation = s_generation;
    s_command.preference = preference;
    s_command.preference_value = value;
    s_requested_preference = preference;
    s_requested_preference_value = value;
    s_command_pending = true;
    ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// scene: Active selector whose previous selection stays visible during the write.
// id: Flash-owned catalog ID from a real tap; copied into bounded request storage.
// Exception: Returns the real admission, preparation or snapshot publication error.
esp_err_t enqueue_look_save(SceneFrame &scene, const char *id)
{
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire) ||
        s_look_request_id == UINT32_MAX || id == nullptr) {
        ESP_LOGE(kTag, "Look worker admission rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    Command command;
    command.work = Work::SaveLook;
    command.generation = s_generation;
    auto &operation = command.look_save;
    const size_t length = std::strlen(id);
    if (length == 0 || length >= sizeof(operation.requested_id)) {
        ESP_LOGE(kTag, "Look ID length rejected: %u (%s)",
                 static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(operation.requested_id, id, length + 1U);
    operation.request_id = s_look_request_id + 1U;
    operation.state = LUNASHELL::CharacterLookSaveSnapshot::State::Saving;
    operation.started_at_us = esp_timer_get_time();
    esp_err_t error = begin_look_save(scene, id, operation.request_id);
    if (error != ESP_OK) {
        return error;
    }
    error = LUNASHELL::DeviceSnapshotService::instance().reportCharacterLookSave(operation);
    if (error != ESP_OK) {
        return error;
    }
    s_look_request_id = operation.request_id;
    s_command = command;
    s_command_pending = true;
    scene.content.control_revision = ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// text: Bounded immutable English text, retained through frame retirement.
// top: Logical caption baseline container, entirely inside the shared content chord.
// line: Receives the prepared flash-font layout.
// Exception: Returns ESP_ERR_INVALID_SIZE for empty, unsupported or out-of-canvas text.
esp_err_t prepare_caption(const char *text, uint16_t top, CODY2D::TextLine &line)
{
    const auto &font = SceneData::body_font;
    const size_t length = std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length > 31 || width > kCaptionWidth || top + font.height > 466) {
        ESP_LOGE(kTag, "Caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)}, 0xFFFF, line);
}

// line: Receives the complete six-character code in the existing larger title font.
// Exception: Returns the real code-length, font or layout error without logging the code.
esp_err_t prepare_codex_code(CODY2D::TextLine &line)
{
    const auto &font = SceneData::title_font;
    const size_t length = std::strlen(s_codex_code);
    if (length != 6) {
        ESP_LOGE(kTag, "CCP display code rejected: length=%u (%s)",
            static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t width = (length - 1U) * font.advance + font.width;
    if (width > kCaptionWidth || 338U + font.height > 393U) {
        ESP_LOGE(kTag, "CCP display code geometry rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {s_codex_code, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), 338}, 0x07FF, line);
}

// destination: Fixed route-owned buffer retaining the formatted text through frame retirement.
// capacity: Exact writable capacity including the null terminator.
// format: Printable-ASCII format containing exactly one string conversion.
// value: Canonical snapshot value copied into the debug record.
// Exception: Returns ESP_ERR_INVALID_SIZE rather than truncating a diagnostic value.
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
esp_err_t format_debug_line(char *destination, size_t capacity, const char *format, const char *value)
{
    if (destination == nullptr || capacity == 0 || format == nullptr || value == nullptr) {
        ESP_LOGE(kTag, "Debug line format rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const int length = std::snprintf(destination, capacity, format, value);
    if (length <= 0 || static_cast<size_t>(length) >= capacity) {
        ESP_LOGE(kTag, "Debug line would be truncated: length=%d capacity=%u (%s)",
                 length, static_cast<unsigned>(capacity), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// text: Bounded printable-ASCII line retained in ContentView.
// top: Logical top edge selected against the circle chord at this row.
// line: Receives the centered whitesmoke text geometry.
// Exception: Returns the real font or circular-layout preparation error.
esp_err_t prepare_debug_caption(const char *text, uint16_t top, CODY2D::TextLine &line)
{
    const auto &font = SceneData::body_font;
    const size_t length = text == nullptr ? 0 : std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length >= kDebugLineBytes || width > kDebugTextWidth ||
        top + font.height > 356) {
        ESP_LOGE(kTag, "Debug caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)},
        kDebugTextColor, line);
}
#endif

// No parameters: primary setup actions use a captioned footer, not lateral arrows.
bool uses_footer_action()
{
    return s_route == Route::Invitation05 || s_route == Route::Install06 ||
        s_route == Route::Pairing07 || s_route == Route::Failed08 || s_route == Route::Online09 ||
        s_route == Route::MakeItYours10 || s_route == Route::Settings36 ||
        s_route == Route::ConnectCodex12 || s_route == Route::SetupComplete13 ||
        s_route == Route::CodexChoice || s_route == Route::SetupMenu || s_route == Route::NetworkWait ||
        s_route == Route::CodexReplace || s_route == Route::CodexForget || s_route == Route::CodexForgotten ||
        is_codex_event_route(s_route);
}

// No parameters: QR pages and dense options retain clearance above their compact action.
CODY2D::Rect single_action_bounds()
{
    return s_route == Route::Install06 || s_route == Route::Pairing07 ||
        s_route == Route::MakeItYours10 ||
        (s_route == Route::ConnectCodex12 && !s_codex_error_visible) ?
        kFooterBounds : kSingleActionBounds;
}

// action: Stable action identity; the returned English label has flash lifetime.
// label: Receives the primary capsule caption, or null for no action.
// Exception: Unknown identities are rejected rather than inferred from route order.
esp_err_t action_label(OnboardingAction action, const char *&label)
{
    switch (action) {
    case OnboardingAction::None:
        label = nullptr;
        break;
    case OnboardingAction::ShowQr:
        label = "Show QR";
        break;
    case OnboardingAction::PairCody:
        label = "Pair Cody";
        break;
    case OnboardingAction::Cancel:
        label = "Cancel";
        break;
    case OnboardingAction::Retry:
        label = "Try again";
        break;
    case OnboardingAction::Continue:
        label = "Continue";
        break;
    case OnboardingAction::Next:
        label = "Next";
        break;
    case OnboardingAction::Done:
        label = "Done";
        break;
    case OnboardingAction::LetsGo:
        label = "Let's go";
        break;
    case OnboardingAction::AcceptCodex:
        label = "Let's connect";
        break;
    case OnboardingAction::DeclineCodex:
        label = "Not now";
        break;
    case OnboardingAction::ConnectCodex:
        label = "Connect Codex";
        break;
    case OnboardingAction::NetworkSetup:
        label = "Set up Wi-Fi";
        break;
    case OnboardingAction::Later:
        label = "Later";
        break;
    case OnboardingAction::Options:
        label = "Options";
        break;
    case OnboardingAction::KeepPairing:
        label = "Keep link";
        break;
    case OnboardingAction::ReplacePairing:
        label = "Pair again";
        break;
    case OnboardingAction::ForgetPairing:
    case OnboardingAction::ConfirmForget:
        label = "Forget link";
        break;
    case OnboardingAction::KeepSaved:
        label = "Keep link";
        break;
    case OnboardingAction::OpenEvent: label = "Open"; break;
    case OnboardingAction::ViewFailures: label = "View failures"; break;
    case OnboardingAction::SendReply: label = "Send"; break;
    case OnboardingAction::DismissEvent: label = "Done"; break;
    case OnboardingAction::RetryEvent: label = "Try again"; break;
    default:
        ESP_LOGE(kTag, "Unknown navigation action %u (%s)", to_index(action), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// action: Typed semantic purpose, independent of caption and storage order.
// control: Receives native dark gradients, border and the matching action icon.
void style_action(OnboardingAction action, CODY2D::CapsuleControl &control)
{
    control.top_color = 0x1147;
    control.bottom_color = 0x08C4;
    control.border_color = 0x2B4F;
    control.icon_color = 0x8F1D;
    control.icon = CODY2D::ActionIcon::Arrow;
    switch (action) {
    case OnboardingAction::AcceptCodex:
    case OnboardingAction::KeepPairing:
    case OnboardingAction::KeepSaved:
    case OnboardingAction::Done:
    case OnboardingAction::DismissEvent:
        control.icon = CODY2D::ActionIcon::Check;
        break;
    case OnboardingAction::ConnectCodex:
    case OnboardingAction::PairCody:
    case OnboardingAction::SendReply:
        control.icon = CODY2D::ActionIcon::Link;
        break;
    case OnboardingAction::NetworkSetup:
    case OnboardingAction::ShowQr:
        control.top_color = 0x1106;
        control.bottom_color = 0x08A4;
        control.border_color = 0x3291;
        control.icon_color = 0xA71F;
        control.icon = action == OnboardingAction::ShowQr ? CODY2D::ActionIcon::Qr : CODY2D::ActionIcon::Wifi;
        break;
    case OnboardingAction::DeclineCodex:
    case OnboardingAction::Later:
    case OnboardingAction::Options:
        control.top_color = 0x18E5;
        control.bottom_color = 0x10A3;
        control.border_color = 0x4A8C;
        control.icon_color = 0xBDF7;
        control.icon = action == OnboardingAction::Options ? CODY2D::ActionIcon::Gear : CODY2D::ActionIcon::Close;
        break;
    case OnboardingAction::Cancel:
    case OnboardingAction::Retry:
    case OnboardingAction::RetryEvent:
    case OnboardingAction::ViewFailures:
    case OnboardingAction::ReplacePairing:
        control.top_color = 0x2924;
        control.bottom_color = 0x18C3;
        control.border_color = 0x7BEF;
        control.icon_color = 0xE5F1;
        control.icon = action == OnboardingAction::Cancel ? CODY2D::ActionIcon::Close :
            action == OnboardingAction::ViewFailures ? CODY2D::ActionIcon::Arrow : CODY2D::ActionIcon::Retry;
        break;
    case OnboardingAction::ForgetPairing:
    case OnboardingAction::ConfirmForget:
        control.top_color = 0x28A4;
        control.bottom_color = 0x1883;
        control.border_color = 0x796B;
        control.icon_color = 0xED53;
        control.icon = CODY2D::ActionIcon::Close;
        break;
    default:
        break;
    }
}

// action: Validated stable action identity.
// label: Printable primary caption with an optional split at its first space.
// bounds: Shared logical raster and hit rectangle.
// enabled: Backend admission state.
// card: True for the two-column icon-above-caption layout.
// control: Receives the rounded gradient button and first caption line.
// second: Receives a second line only for a two-word card caption.
// Exception: Returns the real caption layout failure.
esp_err_t prepare_button(OnboardingAction action, const char *label, CODY2D::Rect bounds,
                         bool enabled, bool card, CODY2D::CapsuleControl &control,
                         CODY2D::TextLine &second)
{
    control = {bounds, {}, 0, 0, enabled};
    second = {};
    style_action(action, control);
    control.corner_radius = card ? 28 : 0;
    // Cards keep the approved icon-above-caption arrangement.
    control.icon_center = {static_cast<int16_t>(card ? bounds.x + bounds.width / 2U : bounds.x + 27U),
        static_cast<int16_t>(card ? bounds.y + 33U : bounds.y + bounds.height / 2U)};
    if (label == nullptr) {
        control.icon = CODY2D::ActionIcon::None;
        return ESP_OK;
    }
    const auto &font = SceneData::body_font;
    const char *space = card ? std::strchr(label, ' ') : nullptr;
    if (space == label || (space != nullptr && space[1] == '\0')) {
        space = nullptr;
    }
    const uint16_t length = static_cast<uint16_t>(space != nullptr ? space - label : std::strlen(label));
    const uint16_t text_left = card ? bounds.x : bounds.x + 47U;
    const uint16_t text_width = card ? bounds.width : bounds.width - 56U;
    const uint16_t width = (length - 1U) * font.advance + font.width;
    if (length == 0 || width > text_width || font.height > bounds.height) {
        ESP_LOGE(kTag, "Action caption does not fit: %s (%s)", label, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    // The lower icon must remain separate from both caption lines on a card.
    const uint16_t top = card ? bounds.y + (space != nullptr ? 48U : 57U) :
        bounds.y + (bounds.height - font.height) / 2U;
    const uint16_t caption_left = card ? text_left + (text_width - width) / 2U :
        bounds.x + (bounds.width - width - 36U) / 2U + 36U;
    if (!card) {
        // Center the icon and label as one group, aligned to the font container.
        control.icon_center.x = static_cast<int16_t>(caption_left - 24U);
        control.icon_center.y = static_cast<int16_t>(top + font.height / 2U);
    }
    esp_err_t error = CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>(caption_left), static_cast<int16_t>(top)},
        0xFFFF, control.caption);
    if (error == ESP_OK && space != nullptr) {
        const char *text = space + 1;
        const uint16_t count = static_cast<uint16_t>(std::strlen(text));
        const uint16_t line_width = (count - 1U) * font.advance + font.width;
        if (line_width > bounds.width) {
            ESP_LOGE(kTag, "Second action line does not fit: %s (%s)", text, esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        error = CODY2D::prepare_text_line(font, {text, count},
            {static_cast<int16_t>(bounds.x + (bounds.width - line_width) / 2U),
             static_cast<int16_t>(bounds.y + 75U)}, 0xFFFF, second);
    }
    return error;
}

// action: Explicit route action; never inferred from an English label.
// enabled: True only when the route has an admissible backend or navigation action.
// Exception: Rejects an unknown action identity without changing the view.
esp_err_t prepare_action(OnboardingAction action, bool enabled)
{
    const char *label = nullptr;
    esp_err_t error = action_label(action, label);
    if (error != ESP_OK) {
        return error;
    }
    if (s_view.action_id != action || s_view.action.enabled != enabled) {
        ++s_navigation_revision;
    }
    if (s_view.action_id != action) {
        ++s_text_generation;
    }
    s_view.action_id = action;
    s_view.action_visible = action != OnboardingAction::None;
    const bool card = s_route == Route::CodexChoice || s_route == Route::SetupMenu ||
        s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget;
    const auto bounds = card ? kRightCardBounds : uses_footer_action() ? single_action_bounds() :
        action == OnboardingAction::Cancel ? kBackBounds : kForwardBounds;
    if (uses_footer_action()) {
        return prepare_button(action, label, bounds, enabled, card,
            s_view.action, s_view.action_second_line);
    }
    s_view.action = {bounds, {}, NavigationData::top, NavigationData::bottom, enabled};
    return error;
}

// action: Explicit second choice; no action leaves the secondary capsule hidden.
// enabled: Admission state for this choice, independent of its label.
// Exception: Returns label or text-layout errors without inventing a usable control.
esp_err_t prepare_alternative(OnboardingAction action, bool enabled)
{
    const char *label = nullptr;
    esp_err_t error = action_label(action, label);
    if (error != ESP_OK) {
        return error;
    }
    s_view.alternative_id = action;
    return prepare_button(action, label, kLeftCardBounds, enabled, true,
        s_view.alternative, s_view.alternative_second_line);
}

// qr: Compact immutable module map from the encoder.
// bounds: Route-owned square viewport; quiet zones and integer modules stay intact.
// strip: Already clipped caller-owned content tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Rejects unsupported QR geometry before grid arithmetic or pixel writes.
esp_err_t draw_qr_rows(const QrModules &qr, CODY2D::Rect bounds, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    if (qr.side == 0) {
        return ESP_OK;
    }
    if (qr.side < 21 || qr.side > kQrModulesMax || (qr.side - 21U) % 4U != 0 ||
        bounds.width != bounds.height || bounds.x + bounds.width > 466 || bounds.y + bounds.height > 466) {
        ESP_LOGE(kTag, "QR module extent rejected: side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t grid_side = qr.side + 2U * kQrQuietModules;
    const uint16_t scale = bounds.width / grid_side;
    if (scale == 0) {
        ESP_LOGE(kTag, "QR viewport cannot fit side=%u (%s)", qr.side, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t inset = (bounds.width - grid_side * scale) / 2U;
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    for (uint16_t row = top; row < bottom; ++row) {
        for (uint16_t column = left; column < right; ++column) {
            const int module_x = (static_cast<int>(column) - bounds.x - inset) / scale - kQrQuietModules;
            const int module_y = (static_cast<int>(row) - bounds.y - inset) / scale - kQrQuietModules;
            const bool inside = column >= bounds.x + inset && row >= bounds.y + inset &&
                module_x >= 0 && module_y >= 0 && module_x < qr.side && module_y < qr.side;
            const uint32_t bit = inside ? module_y * qr.side + module_x : 0;
            const bool dark = inside && (qr.bits[bit / 8U] & (1U << (bit % 8U))) != 0;
            strip.pixels[(row - first_row) * strip.stride + column - first_column] =
                CODY2D::rgb565_native_to_stored(dark ? 0 : 0xFFFF, strip.byte_order);
        }
    }
    return ESP_OK;
}

// state: Immutable content owned by this route through frame retirement.
// strip: Valid content-clipped tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the first real QR, capsule or text drawing error.
esp_err_t draw_content(const void *state, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    const auto &view = *static_cast<const ContentView *>(state);
    esp_err_t error = draw_qr_rows(view.qr, view.qr_bounds, strip, first_row, first_column);
    for (uint8_t index = 0; error == ESP_OK && index < view.line_count; ++index) {
        error = CODY2D::draw_text_rows(view.lines[index], view.visible_characters[index],
            strip, first_row, first_column);
    }
    if (error == ESP_OK && view.actions_visible && view.action_visible && view.action.caption.valid) {
        // Reveal the complete action atomically after the main line finishes.
        CODY2D::CapsuleControl capsule = view.action;
        capsule.caption = {};
        error = CODY2D::draw_capsule_rows(capsule, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.action.caption, view.action_visible_characters,
                strip, first_row, first_column);
        }
        if (error == ESP_OK && view.action_second_line.valid) {
            error = CODY2D::draw_text_rows(view.action_second_line, view.action_second_visible,
                strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && view.actions_visible && view.alternative_id != OnboardingAction::None) {
        CODY2D::CapsuleControl capsule = view.alternative;
        capsule.caption = {};
        error = CODY2D::draw_capsule_rows(capsule, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.alternative.caption, view.alternative_visible_characters,
                strip, first_row, first_column);
        }
        if (error == ESP_OK && view.alternative_second_line.valid) {
            error = CODY2D::draw_text_rows(view.alternative_second_line, view.alternative_second_visible,
                strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && view.actions_visible && view.options.caption.valid) {
        CODY2D::CapsuleControl control = view.options;
        control.caption = {};
        error = CODY2D::draw_capsule_rows(control, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.options.caption, view.options_visible_characters,
                strip, first_row, first_column);
        }
    }
    for (uint8_t index = 0; error == ESP_OK && view.actions_visible && index < view.preference_count; ++index) {
        const auto &preference = view.preferences[index];
        error = CODY2D::draw_capsule_rows(preference.control, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(preference.value, preference.value.text.length,
                strip, first_row, first_column);
        }
    }
    for (uint8_t index = 0; error == ESP_OK && view.actions_visible && index < view.event_choice_count; ++index) {
        const auto &choice = view.event_choices[index];
        error = CODY2D::draw_capsule_rows(choice.control, strip, first_row, first_column);
        if (error == ESP_OK && choice.second.valid) {
            error = CODY2D::draw_text_rows(choice.second, choice.second.text.length,
                strip, first_row, first_column);
        }
    }
    return error;
}

// scene: Valid route whose title and narrative define button admission; event summaries finish in full.
bool actions_ready(const SceneFrame &scene)
{
    const uint16_t title_length = scene.definition->title_visible ? scene.title.text.length : 0;
    if (scene.definition->title_visible && scene.visible_characters < title_length) {
        return false;
    }
    if (s_view.line_count == 0) {
        return true;
    }
    if (is_codex_event_route(s_route)) {
        uint16_t narrative = title_length;
        for (uint8_t index = 0; index < s_event_plan.line_count; ++index) {
            if (!s_event_plan.lines[index].instant) {
                narrative += static_cast<uint16_t>(std::strlen(s_event_plan.lines[index].text));
            }
        }
        return event_presentation::controls_ready(s_event_plan,
            scene_content_characters(scene, narrative, 0), title_length);
    }
    const uint16_t length = s_view.lines[0].text.length;
    return scene_content_characters(scene, length, title_length) == length;
}

// scene: Valid prepared route with the shared narrative typing clock.
// index: Bounded storage position in the route-owned content lines.
// preceding: Number of earlier narrative characters, excluding instant values.
uint16_t line_visible_characters(const SceneFrame &scene, uint8_t index, uint16_t preceding)
{
    const uint16_t length = s_view.lines[index].text.length;
    if (!s_view.instant_lines[index]) {
        return scene_content_characters(scene, length, preceding);
    }
    // A code is one complete value, revealed after its instruction finishes.
    return scene_content_characters(scene, preceding, 0) == preceding ? length : 0;
}

// scene: Valid route receiving the current content revision.
// Exception: Returns the scene-content validation error without replacing the frame.
esp_err_t attach_content(SceneFrame &scene)
{
    if (s_route == Route::Look11) {
        return attach_look_content(scene, s_navigation_revision);
    }
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        s_view.visible_characters[index] = line_visible_characters(scene, index, preceding);
        if (!s_view.instant_lines[index]) {
            preceding += s_view.lines[index].text.length;
        }
    }
    uint16_t content_characters = 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        if (!s_view.instant_lines[index]) {
            content_characters += s_view.visible_characters[index];
        }
    }
    s_view.actions_visible = actions_ready(scene);
    s_view.action_visible_characters = s_view.actions_visible ? s_view.action.caption.text.length : 0;
    s_view.action_second_visible = s_view.actions_visible ? s_view.action_second_line.text.length : 0;
    s_view.alternative_visible_characters = s_view.actions_visible ? s_view.alternative.caption.text.length : 0;
    s_view.alternative_second_visible = s_view.actions_visible ? s_view.alternative_second_line.text.length : 0;
    s_view.options_visible_characters = s_view.actions_visible ? s_view.options.caption.text.length : 0;
    // Button captions never contribute to typewriter cadence or its audio counts.
    const bool choice = s_view.alternative_id != OnboardingAction::None;
    const uint16_t top = s_route == Route::DebugText ? 130 :
        s_route == Route::MakeItYours10 ? kPreferenceTop : s_route == Route::Settings36 ? 204 :
        s_route == Route::Install06 ? kInstallQrBounds.y :
        s_route == Route::Pairing07 ? kSessionCaptionTop :
        s_route == Route::Invitation05 ? 230 : 300;
    SceneContent content;
    content.bottom_action_visible = uses_footer_action() && s_view.action_visible;
    // The debug page uses the wider middle chord; normal setup routes keep the
    // QR/footer union used by their existing controls.
    content.bounds = s_route == Route::DebugText
        ? DamageRect{40, top, 426, 356}
        : choice ? DamageRect{73, 260, 393, 364}
        : DamageRect{113, top, 353, static_cast<uint16_t>(content.bottom_action_visible ? s_view.action.bounds.y : 430)};
    scene.activity_description = s_view.activity_description.length > 0 ? &s_view.activity_description : nullptr;
    scene.activity_description_revision = s_activity_description_revision;
    capture_scene_activity_identity(scene,
        s_view.activity_identity.project_line.valid ? &s_view.activity_identity : nullptr,
        s_activity_identity_revision);
    if (s_route == Route::SetupMenu || s_route == Route::CodexReplace) {
        content.bottom_action_bounds = {153, 376, 313, 428};
    } else if (s_route == Route::CodexForget) {
        // Narrative below the two cards uses its narrower circular chord.
        // Only the actual card rectangles remain interactive.
        content.bottom_action_bounds = {113, 376, 353, 416};
    } else if (content.bottom_action_visible && !choice) {
        const auto &bounds = s_view.action.bounds;
        content.bottom_action_bounds = {bounds.x, bounds.y,
            static_cast<uint16_t>(bounds.x + bounds.width), static_cast<uint16_t>(bounds.y + bounds.height)};
    }
    content.state = &s_view;
    content.revision = ++s_revision;
    content.control_revision = s_navigation_revision;
    content.visible_characters = content_characters;
    content.text_generation = s_text_generation;
    if (s_route == Route::DebugText) {
        // Full-screen dismissal belongs to the route owner, not an invisible control.
    } else if (uses_footer_action() && s_view.action_visible) {
        if (s_view.alternative_id != OnboardingAction::None) {
            content.control_bounds[0] = {73, 260, 227, 364};
        }
        if (s_route == Route::SetupMenu || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
            (s_options_from_setup &&
            (s_route == Route::MakeItYours10 || s_route == Route::Settings36))) {
            content.control_bounds[0] = {10, 203, 70, 263};
        }
        content.control_bounds[1] = choice ? DamageRect{239, 260, 393, 364} : content.bottom_action_bounds;
    } else {
        content.control_bounds[0] = {10, 203, 70, 263};
        content.control_bounds[1] = {396, 203, 456, 263};
    }
    content.draw_rows = draw_content;
    if (is_codex_event_route(s_route)) {
        // The broad narrative/card region ends before the narrower bottom chord.
        content.bounds = {73, 196, 393, 380};
        content.control_bounds[0] = {};
        content.control_bounds[1] = {};
        content.bottom_action_bounds = {};
        content.bottom_action_visible = false;
        if (s_view.event_choice_count != 0) {
            content.bounds.bottom = 364;
            if (s_view.event_choice_count == 3) {
                content.bottom_action_visible = true;
                content.bottom_action_bounds = {113, 376, 353, 428};
            }
        } else if (s_view.action_visible) {
            content.bounds.bottom = kSingleActionBounds.y;
            content.bottom_action_visible = true;
            content.bottom_action_bounds = {113, 352, 353, 424};
        }
        if (s_route == Route::DecisionChoices27 || s_route == Route::DecisionConfirm27 ||
            s_route == Route::CheckFailures24) {
            content.control_bounds[0] = {10, 203, 70, 263};
        }
        content.control_bounds[1] = content.bottom_action_bounds;
    }
    return set_scene_content(scene, content);
}

// observation: One coherent canonical Wi-Fi/BLE/pairing snapshot after successful handoff.
// scene: Receives the reusable micro-cloud debug layout and six immutable text rows.
// Exception: Rejects unavailable identities, truncation or layout failures without inventing values.
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
esp_err_t enter_pairing_debug(const LUNASHELL::ConnectivityObservation &observation, SceneFrame &scene)
{
    const auto &wifi = observation.radios.wifi;
    const auto &ble = observation.radios.ble;
    if (wifi.ssid[0] == '\0' || wifi.ip_address[0] == '\0' || ble.device_name[0] == '\0') {
        ESP_LOGE(kTag, "Pairing debug rejected incomplete identities (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const SceneDefinition *definition = nullptr;
    esp_err_t error = find_scene(SceneId::DebugText, definition);
    SceneFrame prepared;
    if (error == ESP_OK) {
        error = begin_scene(*definition, prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    wipe(&s_view, sizeof(s_view));
    reset_look_screen();
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    s_route = Route::DebugText;
    s_view.line_count = kPairingDebugLineCount;
    error = format_debug_line(s_view.debug_text[0], kDebugLineBytes,
        "1. Wi-Fi connected to `%s`", wifi.ssid);
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[1], kDebugLineBytes,
            "2. IP obtained: `%s`", wifi.ip_address);
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[2], kDebugLineBytes,
            "3. `%s`", "STA Got IP");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[3], kDebugLineBytes,
            "4. `%s`", "Provisioning stopped");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[4], kDebugLineBytes,
            "5. Entered `%s` screen", "online");
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[5], kDebugLineBytes,
            "6. BLE restarted as `%s`", ble.device_name);
    }
    for (uint8_t index = 0; error == ESP_OK && index < s_view.line_count; ++index) {
        error = prepare_debug_caption(s_view.debug_text[index],
            static_cast<uint16_t>(132U + index * 38U), s_view.lines[index]);
    }
    if (error == ESP_OK) {
        error = attach_content(prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    scene = prepared;
    ESP_LOGI(kTag, "Reusable debug-text route entered after pairing handoff");
    return ESP_OK;
}
#endif

// visible: Whether the selected-chat presentation can request new semantic cues.
void set_companion_audio_visible(bool visible)
{
    const esp_err_t error = set_codex_audio_suspended(!visible);
    if (error != ESP_OK) {
        // Audio is optional at this UI orchestration boundary. Its acquisition
        // owner retains the real failure in the canonical playback snapshot.
        ESP_LOGE(kTag, "Optional CCP audio visibility failed (%s)", esp_err_to_name(error));
    }
}

// previous: Previously presented route, used to recognize real connection recovery.
// destination: Newly accepted presentation; unavailable activity never creates a work cue.
// phase: Authenticated semantic phase from the canonical snapshot.
// chat_id: Bounded authenticated chat identity, used only to suppress duplicate cues.
void notify_companion_transition(Route previous, Route destination,
                                 LUNASHELL::CodexActivityState phase, const char *chat_id)
{
    using Activity = LUNASHELL::CodexActivityState;
    CodexAudioCue cue = CodexAudioCue::None;
    if (destination == Route::Lost32) {
        cue = previous == Route::Lost32 ? CodexAudioCue::None : CodexAudioCue::ConnectionLost;
    } else if (previous == Route::Lost32) {
        cue = CodexAudioCue::ConnectionRestored;
    } else if (destination != Route::ActivityUnavailable && destination != Route::CompanionIdle16) {
        if (phase == s_last_audio_phase && std::strcmp(chat_id, s_last_audio_chat) == 0) {
            return;
        }
        switch (phase) {
        case Activity::Working: cue = CodexAudioCue::Working; break;
        case Activity::Editing: cue = CodexAudioCue::Editing; break;
        case Activity::Running: cue = CodexAudioCue::Running; break;
        case Activity::ResponseReady: cue = CodexAudioCue::ResponseReady; break;
        case Activity::Paused: cue = CodexAudioCue::Paused; break;
        case Activity::Interrupted: cue = CodexAudioCue::Interrupted; break;
        case Activity::Ended: cue = CodexAudioCue::SessionEnded; break;
        case Activity::None:
        case Activity::Active:
        case Activity::Unavailable:
            break;
        }
    }
    if (destination != Route::Lost32 && destination != Route::ActivityUnavailable) {
        s_last_audio_phase = phase;
        std::memcpy(s_last_audio_chat, chat_id, sizeof(s_last_audio_chat));
    }
    if (cue == CodexAudioCue::None) {
        return;
    }
    CodexAudioStatus status{};
    const esp_err_t error = notify_codex_audio(cue, status);
    if (error != ESP_OK) {
        // The audio component publishes its failure; a missed optional sound
        // must not prevent the user from seeing the original chat state.
        ESP_LOGE(kTag, "Optional CCP transition audio failed: cue=%u (%s)",
            static_cast<unsigned>(cue), esp_err_to_name(error));
    }
}

// scene: Screen 10 receiving actual canonical values or a visible write failure.
// Exception: Returns the original snapshot or text preparation failure.
esp_err_t prepare_options_content(SceneFrame &scene)
{
    s_view.line_count = 0;
    s_view.preference_count = 0;
    const char *title = s_preference_error == ESP_OK ? "Make it yours" : "Save failed";
    const uint16_t title_length = static_cast<uint16_t>(std::strlen(title));
    esp_err_t error = ESP_OK;
    if (scene.title.text.length != title_length ||
        std::memcmp(scene.title.text.characters, title, title_length) != 0) {
        ++s_text_generation;
        error = set_scene_title(scene, {title, title_length});
    }
    if (error != ESP_OK) {
        return error;
    }
    if (s_preference_error != ESP_OK) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(s_preference_error));
        error = prepare_caption(s_view.error_text, 260, s_view.lines[0]);
        s_view.line_count = 1;
        return error == ESP_OK ? prepare_action(OnboardingAction::Retry, !s_command_pending) : error;
    }
    LUNASHELL::CodyOptionsObservation observation;
    error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(observation);
    if (error != ESP_OK) {
        return error;
    }
    struct Row {
        PreferenceId identity;
        const char *label;
        CODY2D::ActionIcon icon;
    };
    constexpr Row rows[] = {
        {PreferenceId::ShakeWake, "Shake to wake", CODY2D::ActionIcon::Motion},
        {PreferenceId::AutoRotate, "Auto-rotate", CODY2D::ActionIcon::Rotate},
        {PreferenceId::Sounds, "Sounds", CODY2D::ActionIcon::Sound},
        {PreferenceId::Orientation, "Orientation", CODY2D::ActionIcon::Orientation},
        {PreferenceId::UsageMode, "Usage", CODY2D::ActionIcon::Motion},
    };
    static_assert(std::size(rows) <= std::size(s_view.preferences), "Options rows exceed their persistent control owner");
    const auto &font = SceneData::body_font;
    const auto &preferences = observation.preferences;
    const auto &orientation = observation.orientation;
    s_presented_options = observation;
    for (const auto &row : rows) {
        auto &target = s_view.preferences[s_view.preference_count];
        target = {};
        target.identity = row.identity;
        const uint16_t top = kPreferenceTop + s_view.preference_count * kPreferenceStep;
        bool available = preferences.data_valid && !preferences.data_stale;
        bool active = false;
        const char *value = "N/A";
        switch (row.identity) {
        case PreferenceId::ShakeWake:
            active = preferences.shake_to_wake;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
            available = false;
#endif
            break;
        case PreferenceId::Sounds:
            active = preferences.sounds_enabled;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO || !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
            available = false;
#endif
            break;
        case PreferenceId::AutoRotate:
        case PreferenceId::Orientation:
            available = orientation.data_valid && !orientation.data_stale && !orientation.rotation_pending;
            active = orientation.auto_rotation_enabled;
            break;
        case PreferenceId::UsageMode:
            available = preferences.usage_mode_valid && preferences.usage_mode_error == ESP_OK;
            active = preferences.usage_mode == LUNASHELL::CodyUsageMode::Away;
            break;
        }
        target.requested_value = active ? 0U : 1U;
        if (available) {
            value = active ? "On" : "Off";
            if (row.identity == PreferenceId::UsageMode) {
                value = active ? "Away" : "Desk";
            }
            if (row.identity == PreferenceId::Orientation) {
                if (orientation.auto_rotation_enabled) {
                    value = "Auto";
                } else {
                    std::snprintf(target.value_text, sizeof(target.value_text), "%u",
                        static_cast<unsigned>(orientation.rendering_rotation_degrees));
                    value = target.value_text;
                }
                target.requested_value = orientation.rendering_rotation_degrees == 0U ? 180U : 0U;
            }
        }
        const bool manual_locked = row.identity == PreferenceId::Orientation && orientation.auto_rotation_enabled;
        target.control = {{113, top, 240, kPreferenceHeight}, {},
            static_cast<uint16_t>(active && available ? 0x1147 : 0x18E5),
            static_cast<uint16_t>(active && available ? 0x08C4 : 0x10A3),
            available && !manual_locked && !s_command_pending};
        target.control.corner_radius = 12;
        target.control.border_color = active && available ? 0x2B4F : 0x4A8C;
        target.control.icon = row.icon;
        target.control.icon_center = {130, static_cast<int16_t>(top + 17U)};
        target.control.icon_color = active && available ? 0x8F1D : 0xBDF7;
        const int16_t text_top = static_cast<int16_t>(top + (kPreferenceHeight - font.height) / 2U);
        error = CODY2D::prepare_text_line(font, {row.label, static_cast<uint16_t>(std::strlen(row.label))},
            {148, text_top}, 0xFFFF, target.control.caption);
        const uint16_t count = static_cast<uint16_t>(std::strlen(value));
        const uint16_t value_width = (count - 1U) * font.advance + font.width;
        if (error == ESP_OK) {
            error = CODY2D::prepare_text_line(font, {value, count},
                {static_cast<int16_t>(345U - value_width), text_top},
                available ? 0xFFFF : 0xBDF7, target.value);
        }
        if (error != ESP_OK) {
            return error;
        }
        ++s_view.preference_count;
    }
    return prepare_action(OnboardingAction::Next, !s_command_pending);
}

// line: Fully wrapped shared-plan line, copied into presentation-owned storage before drawing.
// Exception: Returns exact native text/capacity/geometry errors without changing the shared plan.
esp_err_t append_event_line(const event_presentation::Line &line)
{
    const auto &font = SceneData::body_font;
    const size_t count = std::strlen(line.text);
    if (count == 0 || count >= kDebugLineBytes || s_view.line_count >= kContentLineCapacity ||
        line.top + font.height > 380) {
        ESP_LOGE(kTag, "Shared event line bounds rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t index = s_view.line_count;
    auto &buffer = s_view.debug_text[index];
    std::memcpy(buffer, line.text, count + 1U);
    const uint16_t width = static_cast<uint16_t>((count - 1U) * font.advance + font.width);
    const esp_err_t error = CODY2D::prepare_text_line(font, {buffer, static_cast<uint16_t>(count)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(line.top)},
        line.instant ? 0xBDF7 : 0xFFFF, s_view.lines[index]);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Shared event line geometry failed (%s)", esp_err_to_name(error));
        return error;
    }
    s_view.instant_lines[index] = line.instant;
    ++s_view.line_count;
    return ESP_OK;
}

// No parameters: returns reversible navigation without transport or sensor access.
event_presentation::Navigation event_navigation()
{
    event_presentation::Navigation navigation;
    navigation.stage = s_event_stage;
    std::memcpy(navigation.selected_id, s_event_selected_id, sizeof(navigation.selected_id));
    navigation.failure_page = s_event_failure_page;
    return navigation;
}

// No parameters: projects actual canonical observation and previous owner admissions.
event_presentation::Context event_context()
{
    return {s_event_online, s_event_reply_admitted, s_event_dismiss_admitted,
        s_event_observation_error, s_event_ui_error, s_event_retry};
}

// navigation: Local reversible presentation state accepted by the shared model.
// effect: Accepted model intent; reversible navigation clears only local review errors.
void retain_event_navigation(const event_presentation::Navigation &navigation,
                             event_presentation::Effect effect = event_presentation::Effect::None)
{
    s_event_stage = navigation.stage;
    std::memcpy(s_event_selected_id, navigation.selected_id, sizeof(s_event_selected_id));
    s_event_failure_page = navigation.failure_page;
    auto context = event_context();
    event_presentation::accept_navigation(context, effect);
    s_event_ui_error = context.ui_error;
    s_event_retry = context.retry;
}

// id: Shared semantic action identity independent of label or card ordering.
OnboardingAction event_action_identity(event_presentation::ActionId id)
{
    using Id = event_presentation::ActionId;
    switch (id) {
    case Id::Open: return OnboardingAction::OpenEvent;
    case Id::Send: return OnboardingAction::SendReply;
    case Id::Dismiss: return OnboardingAction::DismissEvent;
    case Id::Retry: return OnboardingAction::RetryEvent;
    case Id::Failures: return OnboardingAction::ViewFailures;
    default: return OnboardingAction::None;
    }
}

// scene: Event-owned frame drawing the same bounded semantic plan as the native host.
// Exception: Returns original shared data/layout or native preparation failure.
esp_err_t prepare_event_content(SceneFrame &scene)
{
    const auto &font = SceneData::body_font;
    const size_t capacity = (kEventCaptionWidth - font.width) / font.advance + 1U;
    esp_err_t error = event_presentation::prepare(s_presented_event, event_navigation(),
        event_context(), capacity, s_event_plan);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Shared event presentation rejected (%s)", esp_err_to_name(error));
        return error;
    }
    for (uint8_t index = 0; error == ESP_OK && index < s_event_plan.line_count; ++index) {
        error = append_event_line(s_event_plan.lines[index]);
    }
    s_view.action_visible = false;
    for (uint8_t index = 0; error == ESP_OK && index < s_event_plan.action_count; ++index) {
        const auto &action = s_event_plan.actions[index];
        if (action.id == event_presentation::ActionId::Choose) {
            auto &choice = s_view.event_choices[s_view.event_choice_count];
            std::memcpy(choice.id, action.choice_id, sizeof(choice.id));
            error = event_presentation::prepare_control(action, font, choice.control, choice.second);
            if (error == ESP_OK) { ++s_view.event_choice_count; }
        } else {
            const auto action_id = event_action_identity(action.id);
            if (s_view.action_id != action_id || s_view.action.enabled != action.enabled) { ++s_navigation_revision; }
            if (s_view.action_id != action_id) { ++s_text_generation; }
            s_view.action_id = action_id;
            s_view.action_visible = true;
            error = event_presentation::prepare_control(action, font, s_view.action, s_view.action_second_line);
        }
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Shared event action geometry failed (%s)", esp_err_to_name(error));
        }
    }
    if (error == ESP_OK && s_event_plan.title[0] != '\0') {
        error = set_scene_title(scene, {s_event_plan.title,
            static_cast<uint16_t>(std::strlen(s_event_plan.title))});
    }
    return error;
}

// route: Explicit atlas identity; no navigation is inferred from array position.
// scene: Receives a fully validated immutable layout and caller-owned content.
// Exception: Returns the real scene, catalog, text or content preparation error.
esp_err_t enter_route(Route route, SceneFrame &scene)
{
    SceneId identity;
    switch (route) {
    case Route::Hello04:
        identity = SceneId::Greeting;
        break;
    case Route::Invitation05:
        identity = SceneId::PairInvitation;
        break;
    case Route::Install06:
        identity = SceneId::InstallApp;
        break;
    case Route::Pairing07:
        identity = SceneId::Pairing;
        break;
    case Route::Failed08:
        identity = SceneId::PairingFailed;
        break;
    case Route::Online09:
        identity = SceneId::Online;
        break;
    case Route::MakeItYours10:
        identity = SceneId::MakeItYours;
        break;
    case Route::Settings36:
        identity = SceneId::Settings;
        break;
    case Route::ConnectCodex12:
    case Route::CodexCleanup:
        identity = SceneId::ConnectCodex;
        break;
    case Route::SetupComplete13:
    case Route::CodexForgotten:
        identity = SceneId::SetupComplete;
        break;
    case Route::LookingForPc14:
        identity = SceneId::LookingForPc;
        break;
    case Route::Connected15:
        identity = SceneId::Connected;
        break;
    case Route::Project17: identity = SceneId::ProjectActive; break;
    case Route::Working18: identity = SceneId::Working; break;
    case Route::Response19: identity = SceneId::ResponseReady; break;
    case Route::Paused20: identity = SceneId::SessionPaused; break;
    case Route::Ended20: identity = SceneId::SessionEnded; break;
    case Route::Lost32: identity = SceneId::ConnectionLost; break;
    case Route::CompanionIdle16: identity = SceneId::CompanionIdle; break;
    case Route::ActivityUnavailable: identity = SceneId::ActivityUnavailable; break;
    case Route::PrPending21: identity = SceneId::PrPending; break;
    case Route::ChecksRunning22: identity = SceneId::ChecksRunning; break;
    case Route::ChecksFailed23: identity = SceneId::ChecksFailed; break;
    case Route::CheckFailures24: identity = SceneId::CheckFailures; break;
    case Route::DecisionNotice26: identity = SceneId::DecisionNotice; break;
    case Route::DecisionChoices27: identity = SceneId::DecisionChoices; break;
    case Route::DecisionConfirm27: identity = SceneId::DecisionConfirm; break;
    case Route::DecisionWaiting: identity = SceneId::DecisionWaiting; break;
    case Route::ReplySent31: identity = SceneId::ReplySent; break;
    case Route::CodexNotice: identity = SceneId::CodexNotice; break;
    case Route::ApprovalOnPc: identity = SceneId::ApprovalOnPc; break;
    case Route::EventUnavailable: identity = SceneId::EventUnavailable; break;
    case Route::CodexChoice:
    case Route::CodexReplace:
    case Route::CodexForget:
        identity = SceneId::CodexChoice;
        break;
    case Route::SetupMenu:
        identity = SceneId::SetupMenu;
        break;
    case Route::NetworkWait:
        // Reuse the existing curious character geometry for passive startup,
        // while logical routing and backend admission remain NetworkWait.
        identity = s_startup_network_passive ? SceneId::LookingForPc : SceneId::NetworkWait;
        break;
    default:
        ESP_LOGE(kTag, "Unknown content route %u (%s)", to_index(route), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const SceneDefinition *definition = nullptr;
    esp_err_t error = find_scene(identity, definition);
    SceneFrame prepared;
    if (error == ESP_OK) {
        error = begin_scene(*definition, prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    // QR state is retired on every departure, including cancellation and failure.
    wipe(&s_view, sizeof(s_view));
    reset_look_screen();
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    s_route = route;
    if (route == Route::SetupMenu) {
        s_options_from_setup = false;
    }
    if (route == Route::Hello04) {
        s_hello_completed_at_us = -1;
        scene = prepared;
        ESP_LOGI(kTag, "Atlas route entered: %s", definition->name);
        return ESP_OK;
    }
    s_hello_waits_for_touch = false;
    if (is_codex_event_route(route)) {
        error = prepare_event_content(prepared);
    } else if (route == Route::Invitation05) {
        constexpr const char *captions[] = {"Let's get connected.", "Scan the QR to get", "the setup app."};
        for (const char *text : captions) {
            error = prepare_caption(text, 242 + s_view.line_count * 26U, s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        error = prepare_action(OnboardingAction::ShowQr, true);
    } else if (route == Route::Install06) {
        s_view.qr_bounds = kInstallQrBounds;
        error = prepare_action(OnboardingAction::PairCody, !s_command_pending);
    } else if (route == Route::Pairing07) {
        s_view.qr_bounds = kSessionQrBounds;
        error = prepare_caption("Starting Bluetooth", 222, s_view.lines[0]);
        s_view.lines[0].color = prepared.definition->title_color;
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Cancel, s_pairing_owned && !s_command_pending);
        }
    } else if (route == Route::Failed08) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(s_visible_error));
        error = prepare_caption(s_view.error_text, 300, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Retry, false); // The current observation gates retry or cleanup.
        }
    } else if (route == Route::Online09) {
        // Returning from a child must re-evaluate the canonical connection,
        // even if it was already offline before the child was opened.
        s_online_presentation_current = false;
        error = prepare_caption("Wi-Fi connected", 300, s_view.lines[0]);
        s_view.line_count = 1;
#if CODY_ENABLE_PEARL_JELLY
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Continue, true);
        }
#endif
    } else if (route == Route::MakeItYours10) {
        s_preference_error = ESP_OK;
        error = prepare_options_content(prepared);
    } else if (route == Route::Settings36) {
        constexpr const char *captions[] = {"A little more you", "Choose Cody's look"};
        for (const char *text : captions) {
            error = prepare_caption(text, 238 + s_view.line_count * 30U,
                                    s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        error = prepare_action(OnboardingAction::Continue, true);
    } else if (route == Route::CodexReplace) {
        const char *title = s_codex_manage_repair ? "Pairing error" :
            s_codex_manage_pending ? "Pairing pending" : "PC remembered";
        error = set_scene_title(prepared, {title, static_cast<uint16_t>(std::strlen(title))});
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::ReplacePairing, !s_codex_manage_repair);
        }
        if (error == ESP_OK) {
            error = prepare_alternative(OnboardingAction::KeepPairing, !s_codex_manage_repair);
        }
        if (error == ESP_OK) {
            CODY2D::TextLine unused;
            s_view.options_id = OnboardingAction::ForgetPairing;
            error = prepare_button(s_view.options_id, "Forget", kOptionsBounds, true, false, s_view.options, unused);
        }
    } else if (route == Route::CodexForget) {
        static constexpr char title[] = "Forget this PC?";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        if (error == ESP_OK) {
            error = prepare_caption("Wi-Fi stays saved", 384, s_view.lines[0]);
            s_view.line_count = 1;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::ConfirmForget, true);
        }
        if (error == ESP_OK) {
            error = prepare_alternative(OnboardingAction::KeepSaved, true);
        }
    } else if (route == Route::NetworkWait && s_startup_network_passive) {
        // The cleared view already has no action identities or touch targets.
        // Prepare only the passive content, never temporary recovery buttons.
        static constexpr char title[] = "Please wait";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        if (error == ESP_OK) {
            error = prepare_caption("Connecting to Wi-Fi", 300, s_view.lines[0]);
            s_view.line_count = 1;
        }
    } else if (route == Route::CodexChoice || route == Route::SetupMenu || route == Route::NetworkWait) {
        const auto primary = route == Route::CodexChoice ? OnboardingAction::DeclineCodex :
            route == Route::SetupMenu ? OnboardingAction::NetworkSetup : OnboardingAction::Later;
        const auto alternative = route == Route::CodexChoice ? OnboardingAction::AcceptCodex :
            route == Route::SetupMenu ? OnboardingAction::ConnectCodex : OnboardingAction::NetworkSetup;
        error = prepare_action(primary, true);
        if (error == ESP_OK) {
            error = prepare_alternative(alternative,
                alternative != OnboardingAction::ConnectCodex || kCodexIntegrationEnabled);
        }
        if (error == ESP_OK && route == Route::SetupMenu) {
            CODY2D::TextLine unused;
            s_view.options_id = OnboardingAction::Options;
            error = prepare_button(OnboardingAction::Options, "Options", kOptionsBounds,
                true, false, s_view.options, unused);
        }
    } else if (route == Route::ConnectCodex12) {
        const char *message = s_codex_code[0] == '\0'
            ? "Creating your code"
            : "Share code in Codex";
        error = prepare_caption(message, 300, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK && s_codex_code[0] != '\0') {
            error = prepare_codex_code(s_view.lines[1]);
            s_view.instant_lines[1] = true;
            s_view.line_count = 2;
        } else if (error == ESP_OK) {
            error = prepare_caption("One moment", 327, s_view.lines[1]);
            if (error == ESP_OK) {
                error = prepare_caption("About 30 seconds", 354, s_view.lines[2]);
            }
            s_view.line_count = 3;
        }
        if (error == ESP_OK) {
            error = prepare_action(s_codex_code[0] != '\0' ? OnboardingAction::Cancel : OnboardingAction::None,
                                   s_codex_code[0] != '\0');
        }
    } else if (route == Route::CodexCleanup || route == Route::CodexForgotten) {
        const char *title = route == Route::CodexForgotten ? "Link removed" : "One moment";
        error = set_scene_title(prepared, {title, static_cast<uint16_t>(std::strlen(title))});
        if (error == ESP_OK) {
            const char *message = route == Route::CodexForgotten ? "Ready for a new PC" :
                s_codex_cleanup == CodexCleanup::Forget ? "Removing saved link" : "Closing pairing";
            error = prepare_caption(message, 300, s_view.lines[0]);
            s_view.line_count = 1;
        }
        if (error == ESP_OK) {
            error = prepare_action(route == Route::CodexForgotten ? OnboardingAction::Done : OnboardingAction::None,
                                   route == Route::CodexForgotten);
        }
    } else if (route == Route::SetupComplete13) {
        error = prepare_caption("Your setup is saved", 300, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::LetsGo, true);
        }
    } else if (route == Route::LookingForPc14) {
        error = prepare_caption("Finding your PC", 300, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_caption("Retrying connection", 327, s_view.lines[1]);
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
    } else if (route == Route::Connected15) {
        error = prepare_caption(kCodexInvitationCaption, 300, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_caption("Start a chat in Codex", 327, s_view.lines[1]);
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::None, false);
        }
    }
    if (error == ESP_OK) {
        error = attach_content(prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    scene = prepared;
    ESP_LOGI(kTag, "Atlas route entered: %s", definition->name);
    return ESP_OK;
}

// route: Semantic route whose content follows the selected chat or its connection.
bool is_companion_route(Route route)
{
    return route == Route::Connected15 || route == Route::LookingForPc14 ||
        route == Route::Project17 || route == Route::Working18 || route == Route::Response19 ||
        route == Route::Paused20 || route == Route::Ended20 || route == Route::Lost32 ||
        route == Route::CompanionIdle16 || route == Route::ActivityUnavailable || is_codex_event_route(route);
}

// No parameters: local navigation only; no cancellation, dismissal or automatic reply is sent.
void reset_event_presentation()
{
    s_presented_event = {};
    s_event_stage = EventStage::Notice;
    s_event_retry = EventRetry::None;
    s_event_ui_error = ESP_OK;
    s_event_observation_error = ESP_OK;
    s_event_online = false;
    s_event_reply_admitted = false;
    s_event_dismiss_admitted = false;
    s_event_selected_id[0] = '\0';
    s_event_failure_page = 0;
    s_checks_passed_readable_at_us = -1;
}

// No parameters: projects the retained owner snapshot and reversible navigation into a route.
Route codex_event_destination()
{
    return event_presentation::destination(s_presented_event, s_event_stage,
                                           s_event_reply_admitted, s_event_observation_error);
}

// scene: Display-owned frame rebuilt from the retained canonical event, never from transport I/O.
// preserve_typing: Keeps completed narrative visible for progress/offline changes on the same route.
// Exception: Returns original route/layout errors.
esp_err_t render_codex_event(SceneFrame &scene, bool preserve_typing = false)
{
    // A real question/notification owns this view; its dwell cannot be charged as observed work.
    pause_activity_observation(s_activity_observation);
    const Route destination = codex_event_destination();
    const bool keep_time = preserve_typing && destination == s_route;
    const SceneFrame previous = scene;
    esp_err_t error = enter_route(destination, scene);
    if (error == ESP_OK && keep_time) {
        scene.visible_characters = std::min(previous.visible_characters, scene.title.text.length);
        scene.character_remainder_us = previous.character_remainder_us;
        scene.text_elapsed_us = previous.text_elapsed_us;
        scene.intro_elapsed_us = previous.intro_elapsed_us;
        scene.entrance_elapsed_us = previous.entrance_elapsed_us;
        scene.intro_phase = previous.intro_phase;
        s_text_generation = previous.content.text_generation;
        error = attach_content(scene);
    }
    if (error == ESP_OK) {
        set_companion_audio_visible(true);
    }
    return error;
}

// cue: Existing semantic sound governed by the shared master/quiet/standby audio owner.
// Exception: Logs optional audio failures; the owner retains their canonical playback result.
void notify_event_audio(CodexAudioCue cue)
{
    CodexAudioStatus status;
    const esp_err_t error = notify_codex_audio(cue, status);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Optional Codex event cue failed (%s)", esp_err_to_name(error));
    }
}

// scene: Readonly checks result; the retained question path never calls this helper.
// active_time_us: Existing pausable UI clock; the result gets three readable seconds.
// changed: Receives whether an acknowledgement error needs an explicit Retry control.
// Exception: Returns layout errors; optional dismissal errors keep their cause and visible Retry.
esp_err_t advance_checks_passed_dwell(SceneFrame &scene, int64_t active_time_us, bool &changed)
{
    if (!event_presentation::checks_passed_dismiss_due(s_presented_event, event_context(),
        s_view.actions_visible, active_time_us, s_checks_passed_readable_at_us)) {
        return ESP_OK;
    }
    s_event_ui_error = LUNASHELL_codex_event_dismiss(s_presented_event.request_id);
    s_event_retry = EventRetry::Dismiss;
    s_event_dismiss_admitted = s_event_ui_error == ESP_OK;
    if (s_event_ui_error != ESP_OK) {
        ESP_LOGE(kTag, "Readonly checks result dismissal failed; explicit Retry available (%s)",
            esp_err_to_name(s_event_ui_error));
        changed = true;
        return render_codex_event(scene, true);
    }
    return ESP_OK;
}

// link: Canonical connection validity used only to gate an explicit new Send.
// scene: Current companion frame, retained through disconnects and activity/battery transitions.
// active_time_us: Existing pausable clock, used only for the brief readonly checks-passed result.
// changed: Receives whether the event presentation needs rendering.
// handled: True while an event or explicit event-read error owns the current screen.
// Exception: Returns presentation errors; an optional snapshot failure has a visible Retry path.
esp_err_t present_codex_event(const LUNASHELL::CodexLinkSnapshot &link, SceneFrame &scene,
                             int64_t active_time_us, bool &changed, bool &handled)
{
    handled = false;
    if (!is_companion_route(s_route)) {
        return ESP_OK;
    }
    if (s_event_observation_error != ESP_OK) {
        handled = true; // Only the visible Retry requests another failed observation.
        return ESP_OK;
    }
    LUNASHELL::CodexEventSnapshot observed;
    const esp_err_t read_error =
        LUNASHELL::DeviceSnapshotService::instance().readCodexEventObservation(observed);
    if (read_error != ESP_OK) {
        s_event_observation_error = read_error;
        s_event_online = false;
        handled = true;
        changed = true;
        ESP_LOGE(kTag, "Optional Codex event observation failed; explicit Retry available (%s)",
            esp_err_to_name(read_error));
        return render_codex_event(scene);
    }
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    const bool have_event = observed.kind != Kind::None && observed.request_id[0] != '\0';
    if (!have_event) {
        const bool retained_question = s_presented_event.kind == Kind::Question && !s_event_dismiss_admitted;
        if (retained_question) {
            // EVENT none cannot answer, cancel or acknowledge a question, including
            // a terminal outcome the user has not dismissed. Keep it explicitly stale.
            handled = true;
            if (s_event_online) {
                s_event_online = false;
                changed = true;
                return render_codex_event(scene, true);
            }
            return ESP_OK;
        }
        if (is_codex_event_route(s_route)) {
            reset_event_presentation();
            changed = true;
            return enter_route(Route::Connected15, scene);
        }
        return ESP_OK;
    }
    handled = true;
    const bool same_request = std::strcmp(observed.request_id, s_presented_event.request_id) == 0;
    const bool online = link.data_valid && !link.data_stale && link.state == CodexState::Connected &&
        observed.data_valid && !observed.data_stale;
    if (same_request && observed.revision == s_presented_event.revision &&
        online == s_event_online && s_route == codex_event_destination()) {
        return advance_checks_passed_dwell(scene, active_time_us, changed);
    }
    const State previous_state = s_presented_event.state;
    if (!same_request) {
        reset_event_presentation();
    }
    s_presented_event = observed;
    s_event_online = online;
    s_companion_seen_selection = true;
    if (!same_request || (observed.state != previous_state && LUNASHELL::codexEventIsTerminal(observed.state))) {
        // A terminal owner result retires any failed local admission; Retry must
        // never resurrect an expired/rejected response or replace Done with Send.
        s_event_ui_error = ESP_OK;
        s_event_retry = EventRetry::None;
        s_event_dismiss_admitted = false;
    }
    if (observed.state == State::Sending || observed.state == State::Received) {
        s_event_reply_admitted = true;
        s_event_stage = EventStage::Waiting;
        s_event_ui_error = ESP_OK;
        s_event_retry = EventRetry::None;
        std::memcpy(s_event_selected_id, observed.selected_id, sizeof(s_event_selected_id));
    }
    changed = true;
    const esp_err_t error = render_codex_event(scene, same_request);
    if (error != ESP_OK) {
        return error;
    }
    if (s_route == Route::ReplySent31) {
        if (std::strcmp(s_last_delivered_audio_request, observed.request_id) != 0) {
            std::memcpy(s_last_delivered_audio_request, observed.request_id, sizeof(s_last_delivered_audio_request));
            notify_event_audio(CodexAudioCue::AnswerReceived);
        }
        std::memcpy(s_last_event_audio_request, observed.request_id, sizeof(s_last_event_audio_request));
    } else if (std::strcmp(s_last_event_audio_request, observed.request_id) != 0) {
        std::memcpy(s_last_event_audio_request, observed.request_id, sizeof(s_last_event_audio_request));
        // Each sound names an observed event. A completed response is not proof
        // of passed checks, and a permission request is still handled on the PC.
        switch (observed.kind) {
        case Kind::Question: notify_event_audio(CodexAudioCue::Question); break;
        case Kind::ApprovalOnPc: notify_event_audio(CodexAudioCue::ApprovalRequired); break;
        case Kind::ChecksFailed: notify_event_audio(CodexAudioCue::ChecksFailed); break;
        case Kind::ChecksPassed: notify_event_audio(CodexAudioCue::ChecksPassed); break;
        case Kind::ChecksRunning: notify_event_audio(CodexAudioCue::Running); break;
        case Kind::PrPending:
        case Kind::Notice: notify_event_audio(CodexAudioCue::ResponseReady); break;
        case Kind::None: break;
        }
    }
    return advance_checks_passed_dwell(scene, active_time_us, changed);
}

// scene: Current event view; only a visible Send/Retry can call the reply owner.
// action: Explicit button identity, never inferred from a caption or choice order.
// Exception: Returns layout errors; admission failures retain their original error and visible Retry.
esp_err_t activate_codex_event(SceneFrame &scene, OnboardingAction action)
{
    const event_presentation::Action *selected = nullptr;
    for (uint8_t index = 0; index < s_event_plan.action_count; ++index) {
        if (event_action_identity(s_event_plan.actions[index].id) == action) {
            selected = &s_event_plan.actions[index];
            break;
        }
    }
    if (selected == nullptr) {
        ESP_LOGE(kTag, "Unknown event action rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    auto navigation = event_navigation();
    event_presentation::Effect effect;
    const esp_err_t decision_error = event_presentation::activate(s_presented_event,
        event_context(), *selected, navigation, effect);
    if (decision_error != ESP_OK) {
        ESP_LOGE(kTag, "Event admission rejected (%s)", esp_err_to_name(decision_error));
        return decision_error;
    }
    retain_event_navigation(navigation, effect);
    if (effect == event_presentation::Effect::Render) { return render_codex_event(scene); }
    if (effect == event_presentation::Effect::Observe) {
        s_event_observation_error = ESP_OK;
        s_view.action.enabled = false;
        ++s_navigation_revision;
        return attach_content(scene);
    }
    const bool reply = effect == event_presentation::Effect::Reply;
    if (!reply && effect != event_presentation::Effect::Dismiss) { return ESP_ERR_INVALID_STATE; }
    const esp_err_t error = reply
        ? LUNASHELL_codex_event_reply(s_presented_event.request_id, s_event_selected_id)
        : LUNASHELL_codex_event_dismiss(s_presented_event.request_id);
    s_event_ui_error = error;
    s_event_retry = reply ? EventRetry::Reply : EventRetry::Dismiss;
    if (error == ESP_OK) {
        if (reply) {
            s_event_reply_admitted = true;
            s_event_stage = EventStage::Waiting;
        } else {
            s_event_dismiss_admitted = true;
        }
    } else {
        ESP_LOGE(kTag, "Codex event %s admission failed; explicit Retry available (%s)",
            reply ? "reply" : "dismiss", esp_err_to_name(error));
    }
    return render_codex_event(scene, true);
}

// message: Genuine hook phase caption used when no current explicit purpose is available.
// description: Bounded authenticated ASCII purpose, empty when absent or expired.
// active_time_us: Pausable scene clock used only for local readable marquee dwell.
// Exception: Returns rejected metadata size or the original native text layout error.
esp_err_t prepare_activity_detail(const char *message, const char *description, int64_t active_time_us)
{
    // Purpose and fallback captions share the real 320-pixel native viewport.
    // Reusing unchanged text retains its marquee clock across admitted phase changes.
    const char *effective_description = description[0] == '\0' ? message : description;
    const bool text_changed = std::strcmp(s_view.activity_description.text, effective_description) != 0;
    s_view.line_count = 0;
    const esp_err_t description_error = prepare_activity_description(
        s_view.activity_description, SceneData::body_font, effective_description, active_time_us);
    if (description_error != ESP_OK) {
        return description_error;
    }
    if (text_changed) {
        ++s_activity_description_revision;
    }
    return ESP_OK;
}

// active_time_us: Pausable scene clock; this animation never extends real activity freshness.
// scene: Current frame whose description-only damage is refreshed at each visible pixel step.
// changed: Receives a native render request only when the measured text offset changes.
// Exception: Returns the original native text or content preparation error.
esp_err_t advance_activity_description(int64_t active_time_us, SceneFrame &scene, bool &changed)
{
    bool moved = false;
    const esp_err_t error = CODY::advance_activity_description(
        s_view.activity_description, SceneData::body_font, active_time_us, moved);
    if (error != ESP_OK) {
        return error;
    }
    if (moved) {
        ++s_activity_description_revision;
        scene.activity_description_revision = s_activity_description_revision;
        changed = true;
    }
    return ESP_OK;
}

// active_time_us: Pausable UI clock used only for session dwell and description scrolling.
// scene: Current frame replaced only when visible state or selected identity changes.
// changed: Receives whether scene content needs a new native render.
// Exception: Returns the original layout/text error without disguising it as connection success.
esp_err_t present_companion_activity_content(int64_t active_time_us, SceneFrame &scene, bool &changed)
{
    if (!is_companion_route(s_route)) {
        return ESP_OK;
    }
    const esp_err_t technical_error = LUNASHELL::DeviceSnapshotService::instance().readCodexTechnicalObservation(
        s_technical_observation);
    if (technical_error != ESP_OK) {
        ESP_LOGE(kTag, "Companion technical observation failed (%s)", esp_err_to_name(technical_error));
        return technical_error;
    }
    // Both activity and lifecycle now come from the same canonical mutex acquisition.
    const auto &link = s_technical_observation.link;
    const auto &technical = s_technical_observation.technical;
    using Activity = LUNASHELL::CodexActivityState;
    using Compaction = LUNASHELL::CodexCompactionState;
    const auto &activity = link.activity;
    const bool current_link = link.data_valid && !link.data_stale && link.state == CodexState::Connected;
    const bool reconnecting = home_is_reconnecting(link);
    const bool technical_owner = technical.data_valid && technical.compaction_valid && technical.chat_id[0] != '\0' &&
        std::strcmp(technical.chat_id, activity.chat_id) == 0;
    // ACTIVITY and TECH can arrive separately. A known lifecycle remains explicitly
    // stale until the matching revision arrives; it cannot become a current new-turn fact.
    const bool coherent_technical = technical_owner && activity.data_valid && !activity.data_stale &&
        technical.revision == activity.revision;
    Compaction compaction = technical_owner ? technical.compaction : Compaction::None;
    if ((!current_link || technical.data_stale || !coherent_technical) && compaction == Compaction::Active) { compaction = Compaction::Stale; }
    const bool compacting = compaction == Compaction::Active && current_link && !technical.data_stale;
    const int64_t now_us = esp_timer_get_time();
    const uint64_t receipt_age_us = technical.received_at_us > 0 && now_us >= technical.received_at_us ?
        static_cast<uint64_t>(now_us - technical.received_at_us) : 0;
    const bool refreshed_feedback = current_link && coherent_technical && compaction == Compaction::Refreshed && !technical.data_stale &&
        technical.compaction_age_valid && technical.compaction_age_at_receive_us < 3000000 &&
        receipt_age_us < 3000000 - technical.compaction_age_at_receive_us;
    const bool compaction_changed = compaction != s_presented_compaction || refreshed_feedback != s_presented_compaction_feedback;
    const bool current_work = current_link && activity.data_valid && !activity.data_stale &&
        (activity.state == Activity::Working || activity.state == Activity::Editing || activity.state == Activity::Running);
    const bool native_attention = current_link && activity.data_valid && !activity.data_stale &&
        activity.state == Activity::Paused;
    const char *description = current_work && link.state == LUNASHELL::CodexLinkState::Connected ?
        activity.description : "";
    if (native_attention) { description = "Approve on your PC"; }
    else if (compacting) { description = "Compacting context; please wait"; }
    else if (refreshed_feedback) { description = technical.context_valid && !technical.context_stale ?
        "Context refreshed" : "Context refreshed; counters pending"; }
    else if (compaction == Compaction::Orphaned) { description = "Compaction not fully observed"; }
    else if (compaction == Compaction::Interrupted) { description = "Compaction interrupted"; }
    else if (compaction == Compaction::Stale) { description = "Compaction updates are STALE"; }
    bool reply_introduction = false;
    const esp_err_t reply_error = advance_home_reply_dwell(s_home_reply_dwell,
        current_link && activity.data_valid && !activity.data_stale && activity.state == Activity::ResponseReady,
        activity.chat_id, active_time_us, reply_introduction);
    if (reply_error != ESP_OK) {
        return reply_error;
    }
    ActivityOverlayState overlay = ActivityOverlayState::Hidden;
    if (current_link && activity.data_valid && !activity.data_stale) {
        if (current_work) {
            overlay = ActivityOverlayState::Working;
        } else if (activity.state == Activity::Paused) {
            overlay = ActivityOverlayState::Waiting;
        } else if (activity.state == Activity::ResponseReady && reply_introduction) {
            overlay = ActivityOverlayState::ResponseReady;
        }
    }
    if (compacting) { overlay = ActivityOverlayState::Compacting; }
    const bool overlay_changed = scene.activity_overlay.state != overlay;
    const esp_err_t overlay_error = set_scene_activity_overlay(scene, overlay);
    if (overlay_error != ESP_OK) {
        return overlay_error;
    }
    changed = changed || overlay_changed;
    const bool same_chat = std::strcmp(activity.chat_id, s_presented_activity.chat_id) == 0;
    const bool attention = compacting || (current_link && activity.data_valid && !activity.data_stale && activity.state == Activity::Paused);
    const esp_err_t observation_error = advance_activity_observation(s_activity_observation, activity.state,
        current_link && activity.data_valid && !activity.data_stale, activity.chat_id, active_time_us);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    const Activity work_phase = s_activity_observation.presented_phase;
    // Canonical Active compaction already passed source/receipt TTLs, so its elapsed sum is bounded.
    const int64_t displayed_elapsed_us = compacting && technical.compaction_age_valid ?
        static_cast<int64_t>(technical.compaction_age_at_receive_us + receipt_age_us) :
        current_work ? s_activity_observation.operation_elapsed_us : -1;
    s_view.activity_identity.compaction_timer = compacting;
    Route destination = Route::Connected15;
    const char *message = kCodexInvitationCaption;
    if (!link.data_valid || link.data_stale || link.state != CodexState::Connected) {
        if (!s_companion_seen_selection && !reconnecting) {
            return ESP_OK;
        }
        destination = reconnecting ? Route::LookingForPc14 : Route::Lost32;
        message = reconnecting ? "Retrying PC connection" : "PC connection unavailable";
    } else if (!activity.data_valid || activity.data_stale) {
        // The initial READY can precede ACTIVITY. Until the first activity reply,
        // screen 15 claims PC presence only and never invents a chat state.
        if (activity.last_attempt_at_us == 0 && !s_companion_seen_selection) {
            return ESP_OK;
        }
        destination = Route::ActivityUnavailable;
        message = "Activity unavailable";
    } else {
        switch (activity.state) {
        case Activity::None:
            destination = s_companion_seen_selection ? Route::CompanionIdle16 : Route::Connected15;
            break;
        case Activity::Active:
            destination = Route::Project17;
            message = "Ready when you are";
            break;
        case Activity::Working:
            destination = Route::Working18;
            message = "Processing your request";
            break;
        case Activity::Editing:
            destination = Route::Working18;
            message = work_phase == Activity::Editing ? "Editing your code" : "Processing your request";
            break;
        case Activity::Running:
            destination = Route::Working18;
            message = work_phase == Activity::Running ? "Running a command" : "Processing your request";
            break;
        case Activity::ResponseReady:
            destination = reply_introduction ? Route::Response19 : Route::CompanionIdle16;
            message = reply_introduction ? "Open Codex to review" : kCodexInvitationCaption;
            break;
        case Activity::Paused:
            destination = Route::Paused20;
            message = "Approve on your PC";
            break;
        case Activity::Interrupted:
            destination = Route::Paused20;
            message = "Paused in Codex";
            break;
        case Activity::Ended:
            if (!same_chat || s_session_ended_at_us < 0) {
                s_session_ended_at_us = active_time_us;
            }
            destination = active_time_us - s_session_ended_at_us < 5LL * 1000000LL
                ? Route::Ended20 : Route::CompanionIdle16;
            message = kCodexInvitationCaption;
            break;
        case Activity::Unavailable:
            destination = Route::ActivityUnavailable;
            message = "Activity unavailable";
            break;
        }
        if (activity.state != Activity::Ended) {
            s_session_ended_at_us = -1;
        }
        if (activity.state != Activity::None) {
            s_companion_seen_selection = true;
        }
    }
    if (compacting && !native_attention) {
        destination = Route::Working18;
        message = "Compacting context";
    }
    // Model/cost changes update Home without re-entering the route or replaying speech/title reveal.
    const auto model_cost = observed_model_cost(link, technical);
    const bool home_identity = destination != Route::Connected15 && destination != Route::LookingForPc14;
    const char *home_model = home_identity ? model_cost.model : "";
    const auto home_cost = home_identity ? model_cost.band : LUNASHELL::CodexCostBand::Unknown;
    bool model_cost_changed = false;
    const esp_err_t model_error = set_activity_model_cost(s_view.activity_identity, SceneData::body_font,
        home_model, home_cost, active_time_us, model_cost_changed);
    if (model_error != ESP_OK) { return model_error; }
    if (model_cost_changed) { ++s_activity_identity_revision; }
    if (!compaction_changed && s_route == destination && same_chat && activity.state == s_presented_activity.state &&
        std::strcmp(activity.project, s_presented_activity.project) == 0 &&
        std::strcmp(activity.branch, s_presented_activity.branch) == 0 &&
        std::strcmp(activity.description, s_presented_activity.description) == 0 &&
        work_phase == s_presented_work_phase &&
        activity.data_valid == s_presented_activity.data_valid &&
        activity.data_stale == s_presented_activity.data_stale) {
        esp_err_t error = advance_activity_description(active_time_us, scene, changed);
        bool identity_changed = false;
        if (error == ESP_OK) {
            error = advance_activity_identity(s_view.activity_identity, SceneData::body_font, active_time_us,
                attention, displayed_elapsed_us, identity_changed);
        }
        if (identity_changed || model_cost_changed) {
            capture_scene_activity_identity(scene, &s_view.activity_identity, s_activity_identity_revision);
            changed = true;
        }
        return error;
    }
    const bool only_work_detail_changed = s_route == Route::Working18 && destination == s_route &&
        same_chat && std::strcmp(activity.project, s_presented_activity.project) == 0 &&
        std::strcmp(activity.branch, s_presented_activity.branch) == 0 &&
        activity.data_valid && s_presented_activity.data_valid &&
        !activity.data_stale && !s_presented_activity.data_stale && s_view.activity_identity.project_line.valid;
    const Route previous_route = s_route;
    const bool headline_changed = work_phase != s_presented_work_phase || compaction_changed;
    const char *headline = native_attention ? "Waiting for you" : compacting ? "Compacting" : reconnecting ? "Connecting" : !current_link ? "Disconnected" : !activity.data_valid || activity.data_stale ? nullptr : current_work ?
        (work_phase == Activity::Editing ? "Editing code" : work_phase == Activity::Running ? "Running command" : "Working") :
        activity.state == Activity::Paused ? "Waiting for you" :
        activity.state == Activity::ResponseReady && reply_introduction ? "Reply ready" :
        activity.state == Activity::Interrupted ? "Interrupted" : nullptr;
    s_presented_activity = activity;
    s_presented_work_phase = work_phase;
    s_presented_compaction = compaction;
    s_presented_compaction_feedback = refreshed_feedback;
    if (only_work_detail_changed) {
        // Only an admitted subtype changes the headline; purpose updates preserve its reveal clock.
        esp_err_t error = prepare_activity_detail(message, description, active_time_us);
        if (error == ESP_OK) {
            bool identity_changed = false;
            error = advance_activity_identity(s_view.activity_identity, SceneData::body_font, active_time_us,
                attention, displayed_elapsed_us, identity_changed);
        }
        if (error == ESP_OK && headline_changed && headline != nullptr) {
            ++s_text_generation;
            error = set_scene_title(scene, {headline, static_cast<uint16_t>(std::strlen(headline))});
        }
        changed = true;
        if (error == ESP_OK) {
            error = attach_content(scene);
        }
        if (error == ESP_OK && (!compacting || native_attention) && (!compaction_changed || native_attention)) {
            notify_companion_transition(previous_route, destination, activity.state, activity.chat_id);
        }
        return error;
    }
    esp_err_t error = enter_route(destination, scene);
    if (error != ESP_OK) {
        changed = true;
        return error;
    }
    error = set_scene_activity_overlay(scene, overlay);
    if (error != ESP_OK) {
        return error;
    }
    if (headline != nullptr) {
        ++s_text_generation;
        error = set_scene_title(scene, {headline, static_cast<uint16_t>(std::strlen(headline))});
        if (error != ESP_OK) {
            return error;
        }
    }
    if (destination == Route::Connected15) {
        changed = true;
        return error;
    }
    // The shared native identity layer measures long labels and owns project/branch/model rotation.
    // Its fixed row replaces both legacy project-copy slots and their small debug buffers.
    const char *project = current_link && activity.data_valid && !activity.data_stale ? activity.project : "";
    const char *branch = current_link && activity.data_valid && !activity.data_stale ? activity.branch : "";
    error = prepare_activity_identity(s_view.activity_identity, SceneData::body_font,
        project, branch, active_time_us);
    if (error == ESP_OK) {
        error = set_activity_model_cost(s_view.activity_identity, SceneData::body_font,
            home_model, home_cost, active_time_us, model_cost_changed);
    }
    if (error == ESP_OK) {
        bool identity_changed = false;
        s_view.activity_identity.compaction_timer = compacting;
        error = advance_activity_identity(s_view.activity_identity, SceneData::body_font, active_time_us,
            attention, displayed_elapsed_us, identity_changed);
        ++s_activity_identity_revision;
    }
    s_view.line_count = 0;
    if (error == ESP_OK) {
        error = prepare_activity_detail(message, description, active_time_us);
    }
    if (error == ESP_OK) {
        error = attach_content(scene);
    }
    if (error == ESP_OK) {
        set_companion_audio_visible(true);
        if ((!compacting || native_attention) && (!compaction_changed || native_attention)) {
            notify_companion_transition(previous_route, destination, activity.state, activity.chat_id);
        }
    }
    changed = true;
    return error;
}

// active_time_us: Existing pausable display clock, never a new periodic worker.
// scene: Home frame borrowing stable owner captions until retirement.
// changed: Accumulates native scene damage and coalesced command page changes.
// Exception: Returns original content, token or native glyph preparation failures.
esp_err_t present_companion_activity(int64_t active_time_us, SceneFrame &scene, bool &changed)
{
    esp_err_t error = present_companion_activity_content(active_time_us, scene, changed);
    if (error != ESP_OK) { return error; }
    const auto &link = s_technical_observation.link;
    const auto &technical = s_technical_observation.technical;
    const bool connected = link.data_valid && !link.data_stale &&
        link.health == LUNASHELL::DeviceHealth::Ready && link.last_error == ESP_OK &&
        link.state == CodexState::Connected;
    bool calm_ready = false;
    error = advance_home_ready_dwell(s_home_ready_dwell,
        connected && s_route == Route::CompanionIdle16, link.activity.chat_id,
        link.activity.revision, active_time_us, calm_ready);
    if (error != ESP_OK) { return error; }
    if (connected && s_route == Route::CompanionIdle16) {
        // Keep the idle route and canonical completion phase; only the introductory title settles.
        const char *title = calm_ready ? "Ready to team up" : "Ready for more";
        const size_t length = std::strlen(title);
        if (scene.title.text.length != length || std::strncmp(scene.title.text.characters, title, length) != 0) {
            error = set_scene_title(scene, {title, static_cast<uint16_t>(length)});
            if (error != ESP_OK) { return error; }
            ++s_text_generation;
            changed = true;
        }
    }
    bool tokens_changed = false;
    error = update_home_tokens(s_view.activity_identity, SceneData::body_font,
        link, technical, tokens_changed);
    if (error != ESP_OK) { return error; }
    if (tokens_changed) {
        ++s_activity_identity_revision;
        scene.activity_identity_revision = s_activity_identity_revision;
        changed = true;
    }
    if (!connected && scene.activity_identity != nullptr) {
        scene.activity_identity = nullptr;
        changed = true;
    }
    const uint16_t title_color = s_view.activity_identity.compaction_timer &&
        !s_technical_observation.link.activity.data_stale &&
        link.activity.state != LUNASHELL::CodexActivityState::Paused
        ? 0xFD20 : scene.definition->title_color;
    changed = changed || scene.title.color != title_color;
    scene.title.color = title_color;
    const uint16_t detail_color = title_color == 0xFD20 && s_view.activity_identity.compaction_timer
        ? 0xFD20 : 0xFFFF;
    if (s_view.activity_description.window.valid && s_view.activity_description.window.color != detail_color) {
        s_view.activity_description.window.color = detail_color;
        scene.activity_description_revision = ++s_activity_description_revision;
        changed = true;
    }
    const bool eligible = is_companion_route(s_route) &&
        !s_view.activity_identity.attention_active && !s_view.activity_identity.compaction_timer &&
        (s_presented_compaction == LUNASHELL::CodexCompactionState::None ||
         (s_presented_compaction == LUNASHELL::CodexCompactionState::Refreshed && !s_presented_compaction_feedback)) &&
        link.activity.state != LUNASHELL::CodexActivityState::Paused;
    bool hints_changed = false;
    error = update_command_hints(s_command_hints, SceneData::body_font, connected, eligible,
        link.activity.data_valid && !link.activity.data_stale ? link.activity.chat_id : "",
        active_time_us, hints_changed, s_route == Route::CompanionIdle16
            ? CommandHintsMode::ReadyForMore : CommandHintsMode::Introduction);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Home command hints failed: %s", esp_err_to_name(error));
        return error;
    }
    const auto *hints = s_command_hints.visible ? &s_command_hints : nullptr;
    changed = changed || hints_changed || scene.command_hints != hints;
    scene.command_hints = hints;
    scene.command_hints_revision = s_command_hints.revision;
    return ESP_OK;
}

// scene: Current screen replaced by a truthful retry view on screen 12.
// error: Original CCP1 operation or canonical-state failure.
// Exception: Returns content preparation errors without replacing the real CCP1 error.
esp_err_t show_codex_error(SceneFrame &scene, esp_err_t error)
{
    s_codex_error_visible = true;
    s_codex_visible_error = error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    wipe(s_codex_code, sizeof(s_codex_code));
    esp_err_t presentation_error = enter_route(Route::ConnectCodex12, scene);
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    static constexpr char title[] = "Try again";
    presentation_error = set_scene_title(scene, {title, sizeof(title) - 1U});
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    wipe(&s_view, sizeof(s_view));
    std::snprintf(
        s_view.error_text,
        sizeof(s_view.error_text),
        "Error: 0x%lx",
        static_cast<unsigned long>(s_codex_visible_error)
    );
    presentation_error = prepare_caption(s_view.error_text, 300, s_view.lines[0]);
    s_view.line_count = 1;
    if (presentation_error == ESP_OK) {
        presentation_error = prepare_action(OnboardingAction::Retry, true);
    }
    return presentation_error == ESP_OK ? attach_content(scene) : presentation_error;
}

// scene: Current frame receiving the authenticated connection or recovery view.
// link: Fresh canonical retained association, already checked by the caller.
// Exception: Returns presentation and discovery-admission errors through the retry view.
esp_err_t resume_codex_connection(SceneFrame &scene, const LUNASHELL::CodexLinkSnapshot &link)
{
    s_codex_attempt = link.attempt_id;
    s_codex_owned = true;
    s_codex_error_visible = false;
    s_codex_error_allows_setup = false;
    s_last_codex_change_us = -1;
    // A current READY or an already running discovery needs no restart.
    const bool connected = link.state == CodexState::Connected;
    esp_err_t error = enter_route(connected ? Route::Connected15 : Route::LookingForPc14, scene);
    if (error == ESP_OK && !connected && link.state != CodexState::Discovering) {
        error = enqueue(Work::CodexDiscover, link.attempt_id);
    }
    return error == ESP_OK ? ESP_OK : show_codex_error(scene, error);
}

// scene: Host frame for pairing, explicit replacement confirmation or retained-link recovery.
// intent: User setup, connection retry, or confirmation bound to the displayed association.
// Exception: Returns canonical snapshot, mailbox or presentation errors unchanged.
esp_err_t begin_codex_connection(SceneFrame &scene, CodexConnectionIntent intent)
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance()
        .readConnectivityObservation(observation);
    if (error != ESP_OK) {
        return show_codex_error(scene, error);
    }
    const auto &codex_link = observation.radios.codex_link;
    if (!codex_link.data_valid || codex_link.data_stale) {
        return show_codex_error(
            scene,
            codex_link.last_error != ESP_OK ? codex_link.last_error : ESP_ERR_INVALID_STATE
        );
    }
    if (intent == CodexConnectionIntent::ConfirmReplacement &&
        (s_route != Route::CodexReplace ||
         (!codex_link.retained_link_present && !codex_link.pending_pairing_present) ||
         codex_link.attempt_id != s_codex_replacement_attempt)) {
        ESP_LOGE(kTag, "CCP replacement confirmation no longer matches the saved association (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return show_codex_error(scene, ESP_ERR_INVALID_STATE);
    }
    if (intent == CodexConnectionIntent::Setup &&
        (codex_link.retained_link_present || codex_link.pending_pairing_present || codex_link.pairing_repair_required)) {
        // Saved identity can be managed even while the LAN or PC is unavailable.
        s_codex_owned = false;
        s_codex_replacement_attempt = codex_link.attempt_id;
        s_codex_manage_pending = codex_link.pending_pairing_present;
        s_codex_manage_repair = codex_link.pairing_repair_required;
        wipe(s_codex_code, sizeof(s_codex_code));
        return enter_route(Route::CodexReplace, scene);
    }
    s_codex_error_visible = false;
    s_codex_error_allows_setup = false;
    wipe(s_codex_code, sizeof(s_codex_code));
    if ((codex_link.retained_link_present || codex_link.pending_pairing_present) &&
        !codex_link.pairing_repair_required && intent == CodexConnectionIntent::Retry) {
        // Retry never rotates a retained identity or opens another code window.
        return resume_codex_connection(scene, codex_link);
    }
    const auto &wifi = observation.radios.wifi;
    if (!wifi.data_valid || wifi.data_stale || !wifi.connected ||
        !wifi.ip_acquired || wifi.ip_address[0] == '\0') {
        return show_codex_error(scene, wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE);
    }
    error = enter_route(Route::ConnectCodex12, scene);
    if (error == ESP_OK) {
        const Work work = intent == CodexConnectionIntent::ConfirmReplacement
            ? Work::CodexReplace : Work::CodexStart;
        error = enqueue(work, codex_link.attempt_id);
    }
    return error == ESP_OK ? ESP_OK : show_codex_error(scene, error);
}

// scene: Current code or explicit Forget-confirmation view.
// cleanup: Explicit cancellation or confirmed removal of the CCP records only.
// Exception: Returns canonical/admission failures; completion is observed separately.
esp_err_t begin_codex_cleanup(SceneFrame &scene, CodexCleanup cleanup)
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        return show_codex_error(scene, error);
    }
    const auto &link = observation.radios.codex_link;
    if (!link.data_valid || link.data_stale || link.command_pending ||
        (cleanup == CodexCleanup::Forget &&
         (s_route != Route::CodexForget || link.attempt_id != s_codex_replacement_attempt))) {
        ESP_LOGE(kTag, "CCP cleanup rejected: state or confirmation changed (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return show_codex_error(scene, ESP_ERR_INVALID_STATE);
    }
    s_codex_cleanup = cleanup;
    s_codex_cleanup_admitted = false;
    s_codex_attempt = link.attempt_id;
    s_codex_owned = false;
    wipe(s_codex_code, sizeof(s_codex_code));
    error = enter_route(Route::CodexCleanup, scene);
    if (error == ESP_OK) {
        error = enqueue(cleanup == CodexCleanup::Forget ? Work::CodexForget : Work::CodexCancel, link.attempt_id);
    }
    if (error != ESP_OK) {
        s_codex_cleanup = CodexCleanup::None;
        return show_codex_error(scene, error);
    }
    return ESP_OK;
}

// scene: Active host frame, replaced only after the reusable selector is prepared.
// navigation: Caller and Done targets chosen by this host, not by the selector.
// Exception: Returns the original selector or navigation-control preparation error.
esp_err_t enter_look_route(SceneFrame &scene, const LookNavigationContext &navigation)
{
    esp_err_t error = open_look_screen(scene, navigation);
    if (error != ESP_OK) {
        return error;
    }
    wipe(&s_view, sizeof(s_view));
    s_route = Route::Look11;
    s_swipe_active = false;
    ++s_navigation_revision;
    error = prepare_action(OnboardingAction::None, false);
    if (error == ESP_OK) {
        error = attach_content(scene);
    }
    return error;
}

// destination: Explicit scene identity returned by a child screen.
// scene: Host-owned frame to replace through its normal route initialization.
// Exception: Rejects destinations this host does not implement; never guesses a route.
esp_err_t enter_host_destination(SceneId destination, SceneFrame &scene)
{
    Route route;
    switch (destination) {
    case SceneId::PairInvitation:
        route = Route::Invitation05;
        break;
    case SceneId::InstallApp:
        route = Route::Install06;
        break;
    case SceneId::Pairing:
        route = Route::Pairing07;
        break;
    case SceneId::PairingFailed:
        route = Route::Failed08;
        break;
    case SceneId::Online:
        route = Route::Online09;
        break;
    case SceneId::MakeItYours:
        route = Route::MakeItYours10;
        break;
    case SceneId::Settings:
        route = Route::Settings36;
        break;
    case SceneId::ConnectCodex:
        route = Route::ConnectCodex12;
        break;
    case SceneId::CodexChoice:
        route = Route::CodexChoice;
        break;
    case SceneId::SetupMenu:
        route = Route::SetupMenu;
        break;
    case SceneId::SetupComplete:
        route = Route::SetupComplete13;
        break;
    case SceneId::LookingForPc:
        route = Route::LookingForPc14;
        break;
    case SceneId::Connected:
        route = Route::Connected15;
        break;
    case SceneId::ProjectActive: route = Route::Project17; break;
    case SceneId::Working: route = Route::Working18; break;
    case SceneId::ResponseReady: route = Route::Response19; break;
    case SceneId::SessionPaused: route = Route::Paused20; break;
    case SceneId::SessionEnded: route = Route::Ended20; break;
    case SceneId::ConnectionLost: route = Route::Lost32; break;
    case SceneId::CompanionIdle: route = Route::CompanionIdle16; break;
    case SceneId::ActivityUnavailable: route = Route::ActivityUnavailable; break;
    default:
        ESP_LOGE(kTag, "Unsupported host destination %u (%s)",
                 static_cast<unsigned>(destination), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (route == Route::ConnectCodex12) {
        return begin_codex_connection(scene, CodexConnectionIntent::Setup);
    }
    if (route == Route::CodexChoice) {
        request_saved_setup_startup();
        bool changed = false;
        return evaluate_saved_setup(scene, changed);
    }
    return enter_route(route, scene);
}

// scene: Current route that displays a real local action/preparation error.
// error: Original operation error, not substituted by a successful presentation.
esp_err_t show_error(SceneFrame &scene, esp_err_t error)
{
    s_visible_error = error;
    return enter_route(Route::Failed08, scene);
}

// scene: Screen 07 remains active while its real backend state changes.
// message: Flash-owned status text, retained through complete frame retirement.
// Exception: Returns the original title, caption or content preparation error.
esp_err_t show_pairing_progress(SceneFrame &scene, const char *message)
{
    static constexpr char title[] = "Pairing...";
    ++s_text_generation;
    wipe(&s_view.qr, sizeof(s_view.qr));
    s_view.qr_bounds = kSessionQrBounds;
    esp_err_t error = set_scene_title(scene, {title, sizeof(title) - 1U});
    if (error == ESP_OK) {
        error = prepare_caption(message, 222, s_view.lines[0]);
        s_view.lines[0].color = scene.definition->title_color;
        s_view.line_count = 1;
    }
    return error == ESP_OK ? attach_content(scene) : error;
}

// policy: Bounded route policy selecting the actual Welcome scene as destination.
// scene: Current display-owned frame, replaced without restarting the runtime.
// Exception: Returns the real destination preparation error; cancellation remains owned.
esp_err_t begin_inactivity_return(const ScreenInactivityPolicy &policy, SceneFrame &scene)
{
    const bool pending_start = s_command_pending && s_command.generation == s_generation &&
        (s_command.work == Work::Start || s_command.work == Work::Retry);
    s_timeout_cleanup = {};
    s_timeout_cleanup.pending = s_pairing_owned || pending_start;
    s_timeout_cleanup.awaiting_start_result = pending_start;
    s_timeout_cleanup.retired_generation = s_generation;
    s_timeout_cleanup.attempt = pending_start ? s_command.attempt : s_attempt;
    // Retire presentation work immediately. Only its admitted attempt identity may
    // be recovered for cancellation; no old result may restore QR or success UI.
    ++s_generation;
    s_presentation_generation.store(s_generation, std::memory_order_release);
    s_command_pending = false;
    s_pairing_owned = false;
    s_cancel_to_install = false;
    s_retry_to_invitation = false;
    s_session = 0;
    s_last_pairing_change_us = -1;
    s_local_failure = false;
    s_hello_waits_for_touch = policy.destination == Route::Hello04;
    s_activity_touch_pressed = false;
    const esp_err_t error = enter_route(policy.destination, scene);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(kTag, "Screen inactivity returned to Welcome; pairing cleanup pending=%u",
        s_timeout_cleanup.pending);
    return ESP_OK;
}

// scene: Current frame replaced by an explicit error view if cleanup cannot finish.
// error: Original cancellation admission or canonical cleanup error.
// changed: Receives the visible error transition; no successful closure is fabricated.
// Exception: Returns the real error-view preparation error while preserving the cause.
esp_err_t show_timeout_cleanup_error(SceneFrame &scene, esp_err_t error, bool &changed)
{
    s_attempt = s_timeout_cleanup.attempt;
    s_pairing_owned = true;
    s_timeout_cleanup.pending = false;
    s_command_pending = false;
    s_local_failure = true;
    s_failed_work = Work::Cancel;
    ESP_LOGE(kTag, "Inactivity pairing cleanup failed: attempt=%lu (%s)",
        static_cast<unsigned long>(s_attempt), esp_err_to_name(error));
    changed = true;
    esp_err_t presentation_error = show_error(scene, error);
    if (presentation_error == ESP_OK) {
        presentation_error = prepare_action(OnboardingAction::Retry, true);
    }
    return presentation_error == ESP_OK ? attach_content(scene) : presentation_error;
}

// scene: Retained Welcome or a user-selected route; clean closure never promotes it.
// observation: One coherent canonical backend observation for the owned old attempt.
// result: Acquired action result, wiped once detached cleanup consumes it.
// have_result: Cleared only when cleanup consumes the result instead of route presentation.
// changed: Receives only an actual cleanup-error presentation change.
// Exception: Returns canonical identity or presentation errors; cleanup failures remain visible.
esp_err_t update_timeout_cleanup(SceneFrame &scene,
                                 const LUNASHELL::ConnectivityObservation &observation,
                                 Result &result, bool &have_result, bool &changed)
{
    if (!s_timeout_cleanup.pending) {
        return ESP_OK;
    }
    if (have_result && result.command.generation == s_timeout_cleanup.retired_generation &&
        (result.command.work == Work::Start || result.command.work == Work::Retry)) {
        s_timeout_cleanup.awaiting_start_result = false;
        if (result.error == ESP_OK) {
            s_timeout_cleanup.attempt = result.command.attempt + 1U;
        } else {
            // The rejected start acquired no attempt. Its real error was logged
            // by the worker; the retired presentation must remain on Welcome.
            s_timeout_cleanup.pending = false;
        }
        wipe(&result, sizeof(result));
        have_result = false;
    }
    if (have_result && result.command.generation == s_generation &&
        result.command.work == Work::Cancel) {
        s_command_pending = false;
        const esp_err_t cancellation_error = result.error;
        wipe(&result, sizeof(result));
        have_result = false;
        if (cancellation_error != ESP_OK) {
            return show_timeout_cleanup_error(scene, cancellation_error, changed);
        }
    }
    if (!s_timeout_cleanup.pending || s_timeout_cleanup.awaiting_start_result) {
        return ESP_OK;
    }
    const auto &pairing = observation.radios.pairing;
    if (!pairing.data_valid || pairing.data_stale ||
        pairing.attempt_id != s_timeout_cleanup.attempt) {
        const esp_err_t error = pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Inactivity cleanup observation rejected: valid=%u stale=%u attempt=%lu/%lu (%s)",
            pairing.data_valid, pairing.data_stale,
            static_cast<unsigned long>(pairing.attempt_id),
            static_cast<unsigned long>(s_timeout_cleanup.attempt), esp_err_to_name(error));
        return error;
    }
    const bool clean_terminal = pairing.state == PairState::Idle || pairing.state == PairState::Cancelled ||
        pairing.state == PairState::Failed || pairing.state == PairState::TimedOut || pairing.state == PairState::Online;
    if (clean_terminal && pairing.cleanup_error == ESP_OK && !pairing.command_pending) {
        s_timeout_cleanup.pending = false;
        s_command_pending = false;
        ESP_LOGI(kTag, "Inactivity pairing cleanup confirmed: attempt=%lu state=%u",
            static_cast<unsigned long>(pairing.attempt_id), to_index(pairing.state));
        return ESP_OK;
    }
    if (s_timeout_cleanup.cancel_requested && pairing.state == PairState::Failed &&
        pairing.cleanup_error != ESP_OK) {
        return show_timeout_cleanup_error(scene, pairing.cleanup_error, changed);
    }
    if (pairing.state == PairState::Cancelling || s_timeout_cleanup.cancel_requested ||
        s_work_busy.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    const bool cancellable = pairing.state == PairState::Starting || pairing.state == PairState::ReadyToScan ||
        pairing.state == PairState::Connecting || pairing.state == PairState::Completing ||
        (pairing.state == PairState::Failed && pairing.cleanup_error != ESP_OK);
    if (!cancellable) {
        const esp_err_t error = pairing.cleanup_error != ESP_OK ? pairing.cleanup_error : ESP_ERR_INVALID_STATE;
        return show_timeout_cleanup_error(scene, error, changed);
    }
    const esp_err_t error = enqueue(Work::Cancel, pairing.attempt_id);
    if (error != ESP_OK) {
        return show_timeout_cleanup_error(scene, error, changed);
    }
    s_timeout_cleanup.cancel_requested = true;
    return ESP_OK;
}

} // namespace

// No parameters: all action storage is static; no framebuffer or per-frame allocation.
esp_err_t onboarding_service_init()
{
    if (s_worker != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_worker = xTaskCreateStatic(action_worker, "CODY_actions", sizeof(s_worker_stack), nullptr,
        3, s_worker_stack, &s_worker_control);
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "Action worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// No parameters: cancel gesture recognition without synthesizing an acquired release.
void cancel_onboarding_touch()
{
    s_swipe_active = false;
    s_activity_touch_pressed = false;
    cancel_look_touch();
}

// No parameters: retire routes and their QR state without erasing saved settings.
void reset_onboarding()
{
    set_companion_audio_visible(false);
    reset_event_presentation();
    s_options_from_setup = false;
    s_preference_error = ESP_OK;
    s_setup_returns_to_companion = false;
    s_companion_seen_selection = false;
    s_session_ended_at_us = -1;
    s_presented_activity = {};
    s_command_hints = {};
    s_home_ready_dwell = {};
    s_home_reply_dwell = {};
    s_activity_observation = {};
    s_presented_work_phase = LUNASHELL::CodexActivityState::Working;
    ++s_generation;
    s_presentation_generation.store(s_generation, std::memory_order_release);
    s_route = Route::None;
    s_startup_pending = false;
    s_startup_wifi_error = ESP_OK;
    s_startup_wifi_wait_started_us = -1;
    s_startup_network_passive = false;
    s_startup_network_presented = false;
    s_command_pending = false;
    s_pairing_owned = false;
    s_cancel_to_install = false;
    s_retry_to_invitation = false;
    s_attempt = 0;
    s_session = 0;
    s_last_pairing_change_us = -1;
    s_screen_activity = {};
    s_pairing_progress = {};
    s_timeout_cleanup = {};
    s_hello_waits_for_touch = false;
    s_activity_touch_pressed = false;
    s_activity_touch_point = {};
    s_codex_attempt = 0;
    s_last_codex_change_us = -1;
    s_codex_owned = false;
    s_codex_error_visible = false;
    s_codex_replacement_attempt = 0;
    s_codex_manage_pending = false;
    s_codex_manage_repair = false;
    s_codex_cleanup = CodexCleanup::None;
    s_codex_cleanup_admitted = false;
    s_codex_error_allows_setup = false;
    s_codex_visible_error = ESP_OK;
    wipe(s_codex_code, sizeof(s_codex_code));
    s_hello_completed_at_us = -1;
    s_local_failure = false;
    s_online_link = false;
    s_online_presentation_current = false;
    s_first_boot_persisted = false;
    // Release builds never admit the temporary diagnostic route.
    s_pairing_debug_dismissed = !kPairingDebugEnabled;
    reset_look_screen();
    s_swipe_active = false;
    s_swipe_route = Route::None;
    s_swipe_axis = GestureAxis::Undecided;
    wipe(&s_view, sizeof(s_view));
}

// No parameters: this is first setup, not a clock routine greeting.
void begin_onboarding()
{
    reset_onboarding();
    s_route = Route::Hello04;
}

// No parameters: returning devices still need a saved-link decision after startup.
void request_saved_setup_startup()
{
    s_startup_pending = true;
    // Boot branding runs after this request. The visible evaluation, not boot
    // allocation/animation time, starts the bounded network grace.
    s_startup_wifi_wait_started_us = -1;
    s_startup_network_passive = false;
    s_startup_network_presented = false;
}

// wifi: Canonical retained credential knowledge; connection intent may arrive later.
// now_us: Monotonic time of the first eligible startup view or a subsequent update.
bool saved_wifi_startup_grace(const LUNASHELL::WifiSnapshot &wifi, int64_t now_us)
{
    if (!wifi.credentials_configured) {
        return false;
    }
    if (s_startup_wifi_wait_started_us < 0) {
        s_startup_wifi_wait_started_us = now_us;
    }
    return now_us - s_startup_wifi_wait_started_us < kStartupWifiGraceUs;
}

// No parameters: resting setup pages may reopen choices without retiring live work.
bool can_open_setup_menu()
{
    return !s_startup_pending && !s_timeout_cleanup.pending && !s_command_pending &&
        !s_work_busy.load(std::memory_order_acquire) && !s_pairing_owned &&
        (s_route == Route::None || s_route == Route::LookingForPc14 ||
         (s_route == Route::Hello04 && s_hello_waits_for_touch) ||
         s_route == Route::Invitation05 || s_route == Route::Install06 ||
         is_companion_route(s_route) ||
         (s_route == Route::ConnectCodex12 && s_codex_error_visible && s_codex_error_allows_setup));
}

// scene: Stable frame whose previous journey has no pending UI-owned work.
// Exception: Rejects busy routes and preserves presentation errors.
esp_err_t open_setup_menu(SceneFrame &scene)
{
    if (!can_open_setup_menu()) {
        ESP_LOGE(kTag, "Setup menu rejected while route busy (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // The backend keeps its retained association; opening this view starts no radio work.
    s_setup_returns_to_companion = is_companion_route(s_route) || s_route == Route::LookingForPc14;
    set_companion_audio_visible(false);
    s_codex_owned = false;
    wipe(s_codex_code, sizeof(s_codex_code));
    return enter_route(Route::SetupMenu, scene);
}

// scene: Visible reconnect page, without interpreting unavailable data as missing credentials.
// error: Actual canonical Wi-Fi observation failure, or ESP_OK for a known disconnected state.
// changed: Receives whether presentation changed since the previous observation.
// passive: Hide all recovery actions while the saved-network grace is pending.
// Exception: Returns presentation failures; the optional radio observation error stays canonical.
esp_err_t present_network_wait(SceneFrame &scene, esp_err_t error, bool &changed, bool passive)
{
    const bool same_view = s_startup_network_presented && s_route == Route::NetworkWait &&
        s_startup_network_passive == passive &&
        (passive || s_startup_wifi_error == error);
    if (error != ESP_OK && s_startup_wifi_error != error) {
        ESP_LOGE(kTag, "Wi-Fi startup failed (%s)", esp_err_to_name(error));
    }
    s_startup_wifi_error = error;
    if (same_view) {
        return ESP_OK;
    }
    s_startup_network_presented = false;
    s_startup_network_passive = passive;
    changed = true;
    esp_err_t presentation_error = enter_route(Route::NetworkWait, scene);
    if (presentation_error == ESP_OK && !passive && error != ESP_OK) {
        static constexpr char title[] = "UNAVAILABLE";
        presentation_error = set_scene_title(scene, {title, sizeof(title) - 1U});
        if (presentation_error == ESP_OK) {
            presentation_error = attach_content(scene);
        }
    }
    s_startup_network_presented = presentation_error == ESP_OK;
    if (!s_startup_network_presented) {
        s_view.actions_visible = false;
    }
    return presentation_error;
}

// scene: Startup destination, chosen only from the coherent canonical radio snapshot.
// changed: Receives whether a new route requires rendering.
// Exception: Returns read/presentation failures; optional CCP faults stay visible, never absence.
esp_err_t evaluate_saved_setup(SceneFrame &scene, bool &changed)
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        return error;
    }
    const auto &wifi = observation.radios.wifi;
    if (!wifi.data_valid || wifi.data_stale) {
        error = wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE;
        return present_network_wait(scene, error, changed,
            saved_wifi_startup_grace(wifi, esp_timer_get_time()));
    }
    if (!wifi.credentials_configured) {
        if (wifi.last_error != ESP_OK && wifi.last_error != ESP_ERR_NOT_FOUND) {
            ESP_LOGE(kTag, "Startup Wi-Fi profile failed (%s)", esp_err_to_name(wifi.last_error));
            return wifi.last_error;
        }
        s_startup_pending = false;
        changed = true;
        return enter_route(Route::Hello04, scene);
    }
    // A saved profile does not prove connectivity; only the current successful
    // owner observation may leave the passive wait early.
    if (wifi.health != LUNASHELL::DeviceHealth::Ready || wifi.last_error != ESP_OK ||
        !wifi.started || !wifi.connected || !wifi.ip_acquired || wifi.ip_address[0] == '\0') {
        return present_network_wait(scene, wifi.last_error, changed,
            saved_wifi_startup_grace(wifi, esp_timer_get_time()));
    }
    if (!kCodexIntegrationEnabled) {
        s_startup_pending = false;
        scene = {};
        s_route = Route::None;
        changed = true;
        return ESP_OK;
    }
    const auto &link = observation.radios.codex_link;
    if (!link.data_valid || link.data_stale || link.pairing_repair_required ||
        (!link.retained_link_present && !link.pending_pairing_present && link.last_error != ESP_OK)) {
        error = link.last_error != ESP_OK ? link.last_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Startup CCP snapshot unavailable (%s)", esp_err_to_name(error));
        s_startup_pending = false;
        changed = true;
        // CCP is optional at startup. Its canonical fault must remain visible
        // without taking the display/touch owner down with it.
        return show_codex_error(scene, error);
    }
    s_startup_pending = false;
    changed = true;
    if (!link.retained_link_present && !link.pending_pairing_present) {
        return enter_route(Route::CodexChoice, scene);
    }
    // A retained pairing is an identity, not evidence that the PC is currently ready.
    return resume_codex_connection(scene, link);
}

// scene: Current first-setup scene, always owned by the existing display worker.
// completed_scene: Last fully retired text; never advance from a failed/partial frame.
// active_time_us: Existing animation time, excluding standby and pause.
// inactivity_active_time_us: Awake route time, including animation pause and excluding standby.
// changed: Receives whether scene geometry or content needs a new frame.
// Exception: Returns infrastructure/route errors; optional look-write failures remain
// explicit operation results in the canonical snapshot and the reusable error view.
esp_err_t update_onboarding(SceneFrame &scene, const SceneFrame &completed_scene,
                            int64_t active_time_us, int64_t inactivity_active_time_us, bool &changed)
{
    changed = false;
    const esp_err_t activity_error = synchronize_screen_activity(inactivity_active_time_us);
    if (activity_error != ESP_OK) {
        return activity_error;
    }
    Result result;
    bool have_result = s_result_ready.load(std::memory_order_acquire);
    if (have_result) {
        result = s_result;
        wipe(&s_result, sizeof(s_result));
        s_result_ready.store(false, std::memory_order_release);
        s_work_busy.store(false, std::memory_order_release);
    }
    if (have_result && result.command.work == Work::SaveLook) {
        if (result.publication_error != ESP_OK) {
            return result.publication_error;
        }
        // Persistence is optional at this orchestrator boundary. The worker's real
        // write result is already canonical; failed publication is still fatal.
        if (result.error == ESP_OK) {
#if CODY_ENABLE_PEARL_JELLY
            const esp_err_t selection_error = select_jelly_skin(result.command.look_save.requested_id);
            if (selection_error != ESP_OK) {
                return selection_error;
            }
#else
            ESP_LOGE(kTag, "Look completion without a renderer (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return ESP_ERR_NOT_SUPPORTED;
#endif
            changed = true;
        } else {
            ESP_LOGE(kTag, "Optional look write failed; display continues: request=%lu (%s)",
                     static_cast<unsigned long>(result.command.look_save.request_id), esp_err_to_name(result.error));
        }
        if (result.command.generation == s_generation && s_route == Route::Look11) {
            s_command_pending = false;
            scene.content.control_revision = ++s_navigation_revision;
            const auto completion = complete_look_save(scene, result.command.look_save.request_id, result.error);
            if (completion.presentation_error != ESP_OK) {
                return completion.presentation_error;
            }
            const esp_err_t content_error = attach_look_content(scene, s_navigation_revision);
            if (content_error != ESP_OK) {
                return content_error;
            }
            changed = true;
        }
        // A reset may retire the view while NVS is busy. The copied request and
        // canonical outcome survive it; never resurrect a retired screen.
        wipe(&result, sizeof(result));
        return ESP_OK;
    }
    if (s_timeout_cleanup.pending) {
        LUNASHELL::ConnectivityObservation observation{};
        const esp_err_t observation_error = LUNASHELL::DeviceSnapshotService::instance()
            .readConnectivityObservation(observation);
        if (observation_error != ESP_OK) {
            wipe(&result, sizeof(result));
            return observation_error;
        }
        const esp_err_t cleanup_error = update_timeout_cleanup(
            scene, observation, result, have_result, changed);
        if (cleanup_error != ESP_OK) {
            wipe(&result, sizeof(result));
            return cleanup_error;
        }
    }
    if (s_startup_pending) {
        wipe(&result, sizeof(result));
        const esp_err_t startup_error = evaluate_saved_setup(scene, changed);
        if (startup_error != ESP_OK || changed || s_route == Route::None) {
            return startup_error;
        }
        // A pending reconnect still owns a visible, normally typed decision page.
    }
    if (s_route == Route::None) {
        wipe(&result, sizeof(result));
        return ESP_OK;
    }
    if (s_route == Route::Hello04) {
        wipe(&result, sizeof(result));
        if (s_hello_waits_for_touch) {
            return ESP_OK;
        }
        const bool hello_retired = scene.valid && scene.definition != nullptr &&
            scene.visible_characters == scene.title.text.length &&
            completed_scene.valid && completed_scene.definition == scene.definition &&
            completed_scene.definition->id == SceneId::Greeting &&
            completed_scene.visible_characters == completed_scene.title.text.length;
        if (!hello_retired) {
            return ESP_OK;
        }
        if (s_hello_completed_at_us < 0) {
            s_hello_completed_at_us = active_time_us;
        }
        if (active_time_us - s_hello_completed_at_us < kHelloReadableHoldUs) {
            return ESP_OK;
        }
        changed = true;
        return enter_route(Route::Invitation05, scene);
    }
    if (!scene.valid || scene.definition == nullptr) {
        ESP_LOGE(kTag, "Active route has no prepared scene (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (scene.content.control_revision != s_navigation_revision) {
        scene.content.control_revision = s_navigation_revision;
        changed = true;
    }
    if (s_route == Route::Look11) {
        wipe(&result, sizeof(result));
        return update_look_text(scene, s_navigation_revision, changed);
    }
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    bool caption_changed = false;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        const uint16_t visible = line_visible_characters(scene, index, preceding);
        caption_changed = caption_changed || visible != s_view.visible_characters[index];
        if (!s_view.instant_lines[index]) {
            preceding += s_view.lines[index].text.length;
        }
    }
    caption_changed = caption_changed || actions_ready(scene) != s_view.actions_visible;
    if (caption_changed) {
        const esp_err_t caption_error = attach_content(scene);
        if (caption_error != ESP_OK) {
            return caption_error;
        }
        changed = true;
    }
    if (s_route == Route::MakeItYours10 && !s_command_pending && !have_result && s_preference_error == ESP_OK) {
        LUNASHELL::CodyOptionsObservation options;
        const esp_err_t options_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
        if (options_error != ESP_OK) {
            wipe(&result, sizeof(result));
            return options_error;
        }
        const auto &current = options.orientation;
        const auto &previous = s_presented_options.orientation;
        const bool preference_changed = options.preferences.last_attempt_at_us !=
            s_presented_options.preferences.last_attempt_at_us ||
            options.preferences.usage_mode != s_presented_options.preferences.usage_mode ||
            options.preferences.usage_mode_valid != s_presented_options.preferences.usage_mode_valid ||
            options.preferences.usage_mode_error != s_presented_options.preferences.usage_mode_error;
        if (preference_changed || current.auto_rotation_enabled != previous.auto_rotation_enabled ||
            current.rotation_pending != previous.rotation_pending || current.data_valid != previous.data_valid ||
            current.data_stale != previous.data_stale ||
            current.rendering_rotation_degrees != previous.rendering_rotation_degrees) {
            esp_err_t refresh_error = prepare_options_content(scene);
            if (refresh_error == ESP_OK) {
                refresh_error = attach_content(scene);
            }
            if (refresh_error != ESP_OK) {
                wipe(&result, sizeof(result));
                return refresh_error;
            }
            changed = true;
        }
    }
    LUNASHELL::ConnectivityObservation observation;
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        wipe(&result, sizeof(result));
        return error;
    }
    const auto &pairing = observation.radios.pairing;
    const auto &codex_link = observation.radios.codex_link;
    // A local failure can happen before commissioning starts, or while it is
    // still live. Reopen Setup only when the owner confirms no active code work.
    s_codex_error_allows_setup = codex_link.data_valid && !codex_link.data_stale &&
        !codex_link.command_pending && !codex_link.pairing_window_active;
    error = observe_pairing_progress(observation, inactivity_active_time_us);
    if (error != ESP_OK) {
        wipe(&result, sizeof(result));
        return error;
    }
    const auto *inactivity_policy = screen_inactivity_policy(s_route);
    const bool current_pairing_failure = s_pairing_owned && pairing.data_valid && !pairing.data_stale &&
        pairing.attempt_id == s_attempt && !s_command_pending &&
        (pairing.state == PairState::Failed || pairing.state == PairState::TimedOut);
    if (inactivity_policy != nullptr && !current_pairing_failure &&
        inactivity_active_time_us - s_screen_activity.last_activity_us >= inactivity_policy->timeout_us) {
        changed = true;
        error = begin_inactivity_return(*inactivity_policy, scene);
        if (error == ESP_OK) {
            error = update_timeout_cleanup(scene, observation, result, have_result, changed);
        }
        wipe(&result, sizeof(result));
        return error;
    }
    if (have_result && result.command.generation == s_generation) {
        s_command_pending = false;
        scene.content.control_revision = ++s_navigation_revision;
        changed = true;
        if (result.command.work == Work::SavePreference && s_route == Route::MakeItYours10) {
            s_preference_error = result.error;
            error = prepare_options_content(scene);
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        } else if (result.error != ESP_OK) {
            if (result.command.work == Work::Cancel && s_retry_to_invitation) {
                // Admission failed, so a requested return to 05 has no confirmed cleanup.
                s_retry_to_invitation = false;
                s_cancel_to_install = false;
            }
            const bool codex_work = result.command.work == Work::CodexStart ||
                result.command.work == Work::CodexReplace ||
                result.command.work == Work::CodexCancel || result.command.work == Work::CodexForget ||
                result.command.work == Work::CodexCopyCode ||
                result.command.work == Work::CodexDiscover;
            const bool expired_codex_code = result.command.work == Work::CodexCopyCode &&
                result.error == ESP_ERR_INVALID_STATE && codex_link.data_valid &&
                codex_link.attempt_id == result.command.attempt &&
                codex_link.state == CodexState::Paired;
            // A QR copy can race credentials/cancellation: follow the newer real
            // state instead of treating an expired session as a renderer crash.
            const bool expired_qr = result.command.work == Work::SessionQr &&
                result.error == ESP_ERR_INVALID_STATE && pairing.data_valid &&
                (pairing.state != PairState::ReadyToScan || pairing.attempt_id != result.command.attempt ||
                 pairing.provisioning_session_id != result.command.session);
            if (codex_work && !expired_codex_code) {
                s_codex_cleanup = CodexCleanup::None;
                error = show_codex_error(scene, result.error);
            } else if (!expired_qr && !expired_codex_code) {
                s_failed_work = result.command.work;
                s_local_failure = true;
                error = show_error(scene, result.error);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::Retry, true);
                    if (error == ESP_OK) {
                        error = attach_content(scene);
                    }
                }
            }
        } else if (result.command.work == Work::InstallQr && s_route == Route::Install06) {
            s_local_failure = false;
            s_view.qr = result.qr;
            error = prepare_action(OnboardingAction::PairCody, true);
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::SessionQr && s_pairing_owned && pairing.data_valid &&
                   pairing.attempt_id == s_attempt && pairing.state == PairState::ReadyToScan &&
                   pairing.provisioning_session_id == result.command.session) {
            // The session QR now belongs to screen 07, not an intermediate route.
            static constexpr char title[] = "Scan to pair";
            // A retried QR copy may complete while the local error scene is active.
            error = s_route == Route::Pairing07 ? ESP_OK : enter_route(Route::Pairing07, scene);
            if (error == ESP_OK) {
                ++s_text_generation;
                error = set_scene_title(scene, {title, sizeof(title) - 1U});
            }
            if (error == ESP_OK) {
                error = prepare_caption("Scan with the app", kSessionCaptionTop, s_view.lines[0]);
            }
            if (error == ESP_OK) {
                s_local_failure = false;
                s_view.qr = result.qr;
                s_view.qr_bounds = kSessionQrBounds;
                // The session caption shares the QR route's black-on-white theme.
                s_view.lines[0].color = scene.definition->title_color;
                s_view.line_count = 1;
                s_session = result.command.session;
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::Start || result.command.work == Work::Retry) {
            s_local_failure = false;
            s_attempt = result.command.attempt + 1U; // Backend admission increments exactly once.
            s_pairing_owned = true;
            s_last_pairing_change_us = -1;
        } else if (result.command.work == Work::CompleteFirstBoot) {
            s_local_failure = false;
            s_first_boot_persisted = true;
        } else if (result.command.work == Work::CodexStart || result.command.work == Work::CodexReplace) {
            s_codex_error_visible = false;
            s_codex_attempt = result.command.attempt + 1U;
            s_codex_owned = true;
            s_last_codex_change_us = -1;
            wipe(s_codex_code, sizeof(s_codex_code));
        } else if (result.command.work == Work::CodexCopyCode &&
                   codex_link.data_valid && !codex_link.data_stale &&
                   codex_link.attempt_id == result.command.attempt &&
                   codex_link.state == CodexState::Pairing) {
            std::memcpy(s_codex_code, result.codex_code, sizeof(s_codex_code));
            error = enter_route(Route::ConnectCodex12, scene);
            if (error == ESP_OK) {
                // The same scene definition stays active. Refresh its anchor
                // once on canonical code delivery, giving the full reading interval.
                error = LUNASHELL_power_manager_set_scene_standby_override(
                    scene.definition->standby_override_seconds);
            }
        } else if (result.command.work == Work::CodexDiscover) {
            s_codex_error_visible = false;
        } else if (result.command.work == Work::CodexCancel || result.command.work == Work::CodexForget) {
            s_codex_cleanup_admitted = true;
        }
        wipe(&result, sizeof(result));
        if (error != ESP_OK) {
            return error;
        }
    } else {
        wipe(&result, sizeof(result));
    }
    if (s_codex_cleanup != CodexCleanup::None) {
        if (!s_codex_cleanup_admitted || codex_link.command_pending) {
            return ESP_OK;
        }
        const CodexCleanup cleanup = s_codex_cleanup;
        s_codex_cleanup = CodexCleanup::None;
        changed = true;
        const bool closed = cleanup == CodexCleanup::Forget
            ? codex_link.state == CodexState::Idle && !codex_link.retained_link_present
            : codex_link.state == CodexState::Idle || codex_link.state == CodexState::Paired;
        if (!codex_link.data_valid || codex_link.data_stale || !closed ||
            codex_link.attempt_id != s_codex_attempt || codex_link.pending_pairing_present ||
            codex_link.pairing_repair_required || codex_link.last_error != ESP_OK) {
            return show_codex_error(scene, codex_link.last_error != ESP_OK ? codex_link.last_error : ESP_ERR_INVALID_STATE);
        }
        return enter_route(cleanup == CodexCleanup::Forget ? Route::CodexForgotten : Route::SetupMenu, scene);
    }
    if (is_companion_route(s_route)) {
        bool event_handled = false;
        const esp_err_t event_error = present_codex_event(codex_link, scene, active_time_us, changed, event_handled);
        if (event_error != ESP_OK || event_handled) {
            return event_error;
        }
    }
    if (s_codex_owned) {
        if (s_command_pending && codex_link.data_valid &&
            codex_link.attempt_id != s_codex_attempt) {
            return ESP_OK;
        }
        if (!codex_link.data_valid || codex_link.data_stale ||
            codex_link.attempt_id != s_codex_attempt) {
            changed = true;
            return show_codex_error(
                scene,
                codex_link.last_error != ESP_OK
                    ? codex_link.last_error
                    : ESP_ERR_INVALID_STATE
            );
        }
        if (codex_link.changed_at_us != s_last_codex_change_us) {
            changed = true;
            s_last_codex_change_us = codex_link.changed_at_us;
            if (codex_link.state == CodexState::Paired &&
                s_route == Route::ConnectCodex12) {
                s_codex_error_visible = false;
                error = enter_route(Route::SetupComplete13, scene);
            } else if (codex_link.state == CodexState::Connected &&
                       s_route == Route::LookingForPc14) {
                error = enter_route(Route::Connected15, scene);
            } else if (codex_link.state == CodexState::Discovering &&
                       (s_route == Route::Connected15 || s_route == Route::ConnectCodex12)) {
                s_codex_error_visible = false;
                wipe(s_codex_code, sizeof(s_codex_code));
                error = enter_route(Route::LookingForPc14, scene);
            } else if ((codex_link.state == CodexState::Failed ||
                        codex_link.state == CodexState::TimedOut) &&
                       !s_codex_error_visible) {
                error = show_codex_error(
                    scene,
                    codex_link.last_error != ESP_OK
                        ? codex_link.last_error
                        : ESP_ERR_INVALID_STATE
                );
            } else if (codex_link.state == CodexState::Idle) {
                error = show_codex_error(scene, ESP_ERR_INVALID_STATE);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
        if (codex_link.state == CodexState::Pairing &&
            s_codex_code[0] == '\0' && !s_command_pending) {
            error = enqueue(Work::CodexCopyCode, codex_link.attempt_id);
            if (error != ESP_OK) {
                changed = true;
                return show_codex_error(scene, error);
            }
        }
        if (s_route == Route::LookingForPc14 && codex_link.state == CodexState::Discovering) {
            const auto &wifi = observation.radios.wifi;
            const bool lan_ready = wifi.data_valid && !wifi.data_stale && wifi.last_error == ESP_OK &&
                wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
            const char *caption = !lan_ready ? "Wi-Fi unavailable" :
                codex_link.pending_pairing_present ? "Finishing pairing" : "Finding your PC";
            const char *detail = lan_ready ? "Open Codex to rejoin" : "Your link is saved";
            const uint16_t length = static_cast<uint16_t>(std::strlen(caption));
            if (s_view.lines[0].text.length != length ||
                std::memcmp(s_view.lines[0].text.characters, caption, length) != 0) {
                error = prepare_caption(caption, 300, s_view.lines[0]);
                if (error == ESP_OK) {
                    error = prepare_caption(detail, 327, s_view.lines[1]);
                }
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
                if (error != ESP_OK) {
                    return error;
                }
                changed = true;
            }
        }
        if (s_route == Route::ConnectCodex12 ||
            s_route == Route::SetupComplete13 ||
            s_route == Route::LookingForPc14 ||
            is_companion_route(s_route)) {
            if (is_companion_route(s_route)) {
                return present_companion_activity(active_time_us, scene, changed);
            }
            return ESP_OK;
        }
    }
    if (!s_pairing_owned) {
        return ESP_OK;
    }
    if (!pairing.data_valid || pairing.data_stale || pairing.attempt_id != s_attempt) {
        if (s_route != Route::Failed08) {
            changed = true;
            return show_error(scene, pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE);
        }
        return ESP_OK;
    }
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::Cancel) {
        // Keep the rejected cleanup visible until another explicit Retry is pressed.
        return ESP_OK;
    }
    if (pairing.changed_at_us != s_last_pairing_change_us) {
        changed = true;
        s_last_pairing_change_us = pairing.changed_at_us;
        if (pairing.state == PairState::Cancelled) {
            const bool return_to_invitation = s_retry_to_invitation;
            s_pairing_owned = false;
            s_cancel_to_install = false;
            s_retry_to_invitation = false;
            error = enter_route(return_to_invitation ? Route::Invitation05 : Route::Install06, scene);
            if (error == ESP_OK && !return_to_invitation) {
                error = enqueue(Work::InstallQr, 0);
            }
        } else if ((pairing.state == PairState::Failed || pairing.state == PairState::TimedOut) &&
                   !s_command_pending) {
            s_retry_to_invitation = false;
            s_cancel_to_install = false;
            s_local_failure = false;
            error = show_error(scene, pairing.cleanup_error != ESP_OK ? pairing.cleanup_error : pairing.last_error);
            if (error == ESP_OK) {
                // Retry returns to 05; failed cleanup first retries the typed Cancel operation.
                const bool can_return_or_close = pairing.cleanup_error == ESP_OK ||
                    pairing.state == PairState::Failed;
                error = prepare_action(OnboardingAction::Retry, can_return_or_close);
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            }
        } else if (pairing.state == PairState::Connecting || pairing.state == PairState::Completing ||
                    pairing.state == PairState::Cancelling || pairing.state == PairState::Starting) {
            if (s_retry_to_invitation) {
                // Keep the failure view until the canonical owner confirms Cancelled.
                error = prepare_action(OnboardingAction::Retry, false);
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            } else {
                if (s_route != Route::Pairing07) {
                    error = enter_route(Route::Pairing07, scene);
                }
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::Cancel, !s_command_pending &&
                        pairing.state != PairState::Cancelling && pairing.state != PairState::Completing);
                    if (error == ESP_OK) {
                        const char *message = pairing.state == PairState::Connecting ? "Connecting to Wi-Fi" :
                            pairing.state == PairState::Completing ? "Finishing setup" :
                            pairing.state == PairState::Cancelling ? "Closing pairing" : "Starting Bluetooth";
                        error = show_pairing_progress(scene, message);
                    }
                }
            }
        } else if (pairing.state == PairState::Idle) {
            // A confirmed closed session may follow owner-controlled standby.
            // Never keep rendering the previous attempt's device QR.
            if (s_retry_to_invitation) {
                s_retry_to_invitation = false;
                s_cancel_to_install = false;
                error = show_error(scene, ESP_ERR_INVALID_STATE);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::Retry, true);
                }
                if (error == ESP_OK) {
                    error = attach_content(scene);
                }
            } else {
                s_pairing_owned = false;
                s_cancel_to_install = false;
                error = enter_route(Route::Install06, scene);
                if (error == ESP_OK) {
                    error = enqueue(Work::InstallQr, 0);
                }
            }
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    if (s_retry_to_invitation) {
        return error;
    }
    if (s_route != Route::Failed08 && pairing.state == PairState::ReadyToScan &&
        pairing.provisioning_session_id != s_session &&
        !s_command_pending && !s_cancel_to_install) {
        if (s_view.qr.side != 0) {
            // Never leave an old session QR visible while a replacement encodes.
            changed = true;
            error = show_pairing_progress(scene, "Preparing pairing QR");
            if (error != ESP_OK) {
                return error;
            }
        }
        error = enqueue(Work::SessionQr, s_attempt, pairing.provisioning_session_id);
        if (error != ESP_OK) {
            changed = true;
            return show_error(scene, error);
        }
    }
    if (pairing.state == PairState::Online) {
        const auto &wifi = observation.radios.wifi;
        const bool online = wifi.data_valid && !wifi.data_stale && wifi.health == LUNASHELL::DeviceHealth::Ready &&
            wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
        const auto &ble = observation.radios.ble;
        const bool ble_restarted = ble.data_valid && !ble.data_stale &&
            ble.health == LUNASHELL::DeviceHealth::Ready && ble.last_error == ESP_OK &&
            ble.advertising && ble.device_name[0] != '\0';
        if (online && !s_first_boot_persisted) {
            if (s_local_failure && s_failed_work == Work::CompleteFirstBoot) {
                return ESP_OK; // Retry remains an explicit action on the visible error route.
            }
            if (!s_command_pending) {
                changed = true;
                error = s_route == Route::Pairing07 ? ESP_OK : enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Saving setup");
                }
                if (error == ESP_OK) {
                    error = enqueue(Work::CompleteFirstBoot, pairing.attempt_id);
                }
            }
        } else if (online && s_first_boot_persisted && !ble_restarted) {
            if (s_route != Route::Pairing07 || s_view.line_count == 0 || s_view.action_visible) {
                changed = true;
                error = enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Waiting for Bluetooth");
                }
            }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        } else if (online && s_first_boot_persisted && !s_pairing_debug_dismissed) {
            if (s_route != Route::DebugText) {
                changed = true;
                error = enter_pairing_debug(observation, scene);
            }
#endif
        } else if (!online && s_route != Route::Online09) {
            // Manager completion alone is not an online route. Keep the pending
            // presentation until the same canonical observation has a live IP.
            if (s_route != Route::Pairing07 || s_view.line_count == 0 || s_view.action_visible) {
                changed = true;
                error = enter_route(Route::Pairing07, scene);
                if (error == ESP_OK) {
                    error = prepare_action(OnboardingAction::None, false);
                }
                if (error == ESP_OK) {
                    error = show_pairing_progress(scene, "Waiting for Wi-Fi");
                }
            }
        } else if (s_pairing_debug_dismissed &&
                   (s_route != Route::Online09 || !s_online_presentation_current || online != s_online_link)) {
            changed = true;
            error = enter_route(Route::Online09, scene);
            s_online_link = online;
            s_online_presentation_current = true;
            if (error == ESP_OK && !online) {
                // TextSpan is borrowed by later frames, not copied by set_scene_title.
                static constexpr char title[] = "No connection";
                error = set_scene_title(scene, {title, sizeof(title) - 1U});
                if (error == ESP_OK) {
                    error = prepare_caption(wifi.data_valid && !wifi.data_stale ? "Wi-Fi disconnected" : "UNAVAILABLE",
                        300, s_view.lines[0]);
                }
            }
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        }
    }
    return error;
}

// bounds: Full-canvas rectangle whose half-open hit area matches renderer coordinates.
// point: Logical touch point already mapped through the display rotation owner.
bool point_in_rect(CODY2D::Rect bounds, CODY2D::Point point)
{
    return point.x >= bounds.x && point.y >= bounds.y &&
        point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
}

// No parameters: only routes with a real previous destination expose Back.
bool previous_navigation_available()
{
    if (is_codex_event_route(s_route)) {
        return s_event_plan.back;
    }
    return s_route == Route::SetupMenu || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
        s_route == Route::Install06 ||
        (s_options_from_setup && (s_route == Route::MakeItYours10 || s_route == Route::Settings36)) ||
        (s_view.action_visible && s_view.action_id == OnboardingAction::Cancel);
}

// No parameters: forward actions retain their backend admission and enabled state.
bool next_navigation_available()
{
    if (is_codex_event_route(s_route)) {
        return false; // Only the visible explicit button can Send, dismiss or open an event.
    }
    if (s_route == Route::CodexChoice || s_route == Route::SetupMenu ||
        s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget) {
        return false; // Yes/No and setup choices require their own explicit capsule.
    }
    return s_view.action_visible && s_view.action_id != OnboardingAction::Cancel;
}

// No parameters: hidden lateral arrows do not retain invisible touch targets.
bool previous_control_visible()
{
    if (is_codex_event_route(s_route)) {
        return s_view.actions_visible && previous_navigation_available();
    }
    return s_route == Route::SetupMenu || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
        (s_options_from_setup && (s_route == Route::MakeItYours10 || s_route == Route::Settings36)) ||
        (previous_navigation_available() && s_route != Route::Install06 && !uses_footer_action());
}

// No parameters: a captioned footer replaces the setup journey's right chevron.
bool next_control_visible()
{
    return next_navigation_available() && !uses_footer_action();
}

// scene: Current route, never advanced on unconfirmed cancellation/cleanup.
// direction: Explicit previous/next intent, shared by tap and released swipe.
// Exception: Returns the real persistence, snapshot, command or route error.
esp_err_t activate_onboarding_navigation(SceneFrame &scene, NavigationDirection direction)
{
    if (s_timeout_cleanup.pending) {
        return ESP_OK;
    }
    const bool back_hit = direction == NavigationDirection::Previous && previous_navigation_available();
    const bool action_hit = direction == NavigationDirection::Next && next_navigation_available();
    if (!back_hit && !action_hit) {
        return ESP_OK;
    }
    const bool cancel_hit = back_hit && s_view.action_id == OnboardingAction::Cancel;
    if ((action_hit || cancel_hit) && !s_view.actions_visible) {
        return ESP_OK;
    }
    if (s_command_pending || s_work_busy.load(std::memory_order_acquire) ||
        ((action_hit || cancel_hit) && !s_view.action.enabled)) {
        return ESP_OK;
    }
    ScenePresentation presentation;
    const esp_err_t presentation_error = prepare_scene_presentation(scene, presentation);
    if (presentation_error != ESP_OK || !presentation.content_visible) {
        return presentation_error;
    }
    if (is_codex_event_route(s_route)) {
        if (!back_hit || !s_view.actions_visible) {
            return ESP_OK;
        }
        auto navigation = event_navigation();
        event_presentation::Action back_action;
        back_action.id = event_presentation::ActionId::Back;
        back_action.enabled = s_event_plan.back;
        event_presentation::Effect effect;
        const esp_err_t error = event_presentation::activate(s_presented_event, event_context(),
            back_action, navigation, effect);
        if (error != ESP_OK) { return error; }
        retain_event_navigation(navigation, effect);
        return render_codex_event(scene);
    }
    if (back_hit && s_route == Route::Install06) {
        return enter_route(Route::Invitation05, scene);
    }
    if (back_hit && s_route == Route::SetupMenu) {
        const bool return_to_companion = s_setup_returns_to_companion;
        scene = {};
        reset_onboarding();
        if (return_to_companion) {
            request_saved_setup_startup();
        }
        return ESP_OK;
    }
    if (back_hit && s_route == Route::CodexReplace) {
        return enter_route(Route::SetupMenu, scene);
    }
    if (back_hit && s_route == Route::CodexForget) {
        return begin_codex_connection(scene, CodexConnectionIntent::Setup);
    }
    if (back_hit && s_route == Route::ConnectCodex12 && s_view.action_id == OnboardingAction::Cancel) {
        return begin_codex_cleanup(scene, CodexCleanup::Cancel);
    }
    if (action_hit && s_route == Route::CodexForgotten) {
        return enter_route(Route::SetupMenu, scene);
    }
    if (back_hit && s_options_from_setup && s_route == Route::MakeItYours10) {
        s_options_from_setup = false;
        return enter_route(Route::SetupMenu, scene);
    }
    if (back_hit && s_options_from_setup && s_route == Route::Settings36) {
        return enter_route(Route::MakeItYours10, scene);
    }
    if (action_hit && s_route == Route::Invitation05) {
        esp_err_t error = enter_route(Route::Install06, scene);
        if (error == ESP_OK) {
            error = enqueue(Work::InstallQr, 0);
        }
        if (error != ESP_OK) {
            return show_error(scene, error);
        }
        return ESP_OK;
    }
    if (action_hit && s_route == Route::Online09) {
        // Pairing ownership ends only after the persisted handoff and the user's
        // explicit Continue; later journey screens must not be forced back to 09.
        s_pairing_owned = false;
        return enter_route(Route::MakeItYours10, scene);
    }
    if (action_hit && s_route == Route::MakeItYours10) {
        if (s_preference_error != ESP_OK) {
            esp_err_t error = enqueue_preference(s_requested_preference, s_requested_preference_value);
            if (error == ESP_OK) {
                s_preference_error = ESP_OK;
                error = prepare_options_content(scene);
            }
            return error == ESP_OK ? attach_content(scene) : error;
        }
        return enter_route(Route::Settings36, scene);
    }
    if (action_hit && s_route == Route::Settings36) {
        SceneId done = SceneId::SetupComplete;
        if (s_options_from_setup) {
            done = SceneId::SetupMenu;
        } else if (kCodexIntegrationEnabled) {
            done = SceneId::CodexChoice;
        }
        return enter_look_route(scene, {SceneId::Settings, done});
    }
    if (action_hit && s_route == Route::ConnectCodex12) {
        if (s_view.action_id != OnboardingAction::Retry) {
            return ESP_OK;
        }
        return begin_codex_connection(scene, CodexConnectionIntent::Retry);
    }
    if (action_hit && s_route == Route::SetupComplete13) {
        if (!kCodexIntegrationEnabled) {
            scene = {};
            reset_onboarding();
            return ESP_OK;
        }
        return begin_codex_connection(scene, CodexConnectionIntent::Retry);
    }
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::InstallQr) {
        // The installation QR is local; no pairing session was started by this work.
        s_local_failure = false;
        return enter_route(Route::Invitation05, scene);
    }
    LUNASHELL::ConnectivityObservation observation;
    const esp_err_t observation_error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    const auto &pairing = observation.radios.pairing;
    if (!pairing.data_valid || pairing.data_stale) {
        return show_error(scene, pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE);
    }
    if (s_pairing_owned && pairing.attempt_id != s_attempt) {
        return show_error(scene, ESP_ERR_INVALID_STATE);
    }
    if (s_route == Route::Failed08) {
        if (s_local_failure && s_failed_work == Work::CompleteFirstBoot) {
            // Pairing already succeeded. Retry only the failed local write;
            // restarting an Online session would make the user pair again.
            if (!s_pairing_owned || pairing.state != PairState::Online) {
                return show_error(scene, ESP_ERR_INVALID_STATE);
            }
            s_local_failure = false;
            esp_err_t retry_error = enter_route(Route::Pairing07, scene);
            if (retry_error == ESP_OK) {
                retry_error = prepare_action(OnboardingAction::None, false);
            }
            if (retry_error == ESP_OK) {
                retry_error = show_pairing_progress(scene, "Saving setup");
            }
            if (retry_error == ESP_OK) {
                retry_error = enqueue(Work::CompleteFirstBoot, pairing.attempt_id);
            }
            if (retry_error != ESP_OK) {
                s_local_failure = true;
                s_failed_work = Work::CompleteFirstBoot;
                const esp_err_t display_error = show_error(scene, retry_error);
                if (display_error != ESP_OK) {
                    return display_error;
                }
                const esp_err_t action_error = prepare_action(OnboardingAction::Retry, true);
                return action_error == ESP_OK ? attach_content(scene) : action_error;
            }
            return ESP_OK;
        }
        const bool active_session = pairing.state == PairState::Starting ||
            pairing.state == PairState::ReadyToScan || pairing.state == PairState::Connecting ||
            pairing.state == PairState::Completing;
        const bool failed_cleanup = pairing.state == PairState::Failed && pairing.cleanup_error != ESP_OK;
        if (pairing.state == PairState::Cancelling) {
            const esp_err_t action_error = prepare_action(OnboardingAction::Retry, false);
            return action_error == ESP_OK ? attach_content(scene) : action_error;
        }
        if (active_session || failed_cleanup) {
            if (!s_pairing_owned) {
                return show_error(scene, ESP_ERR_INVALID_STATE);
            }
            // A retry from 08 is navigation intent. Close the owned session first.
            const esp_err_t cancel_error = enqueue(Work::Cancel, pairing.attempt_id);
            if (cancel_error != ESP_OK) {
                s_local_failure = true;
                s_failed_work = Work::Cancel;
                return show_error(scene, cancel_error);
            }
            s_local_failure = false;
            s_retry_to_invitation = true;
            s_cancel_to_install = true;
            const esp_err_t action_error = prepare_action(OnboardingAction::Retry, false);
            return action_error == ESP_OK ? attach_content(scene) : action_error;
        }
        if (pairing.cleanup_error != ESP_OK) {
            return show_error(scene, pairing.cleanup_error);
        }
        const bool clean_terminal = pairing.state == PairState::Idle ||
            pairing.state == PairState::Cancelled || pairing.state == PairState::Failed ||
            pairing.state == PairState::TimedOut || pairing.state == PairState::Online;
        if (!clean_terminal) {
            return show_error(scene, ESP_ERR_INVALID_STATE);
        }
        // Clean terminal attempts can be left in place. Pair Cody on 06 later
        // selects START or RETRY from the canonical state after Show QR on 05.
        s_pairing_owned = false;
        s_cancel_to_install = false;
        s_retry_to_invitation = false;
        s_local_failure = false;
        s_session = 0;
        return enter_route(Route::Invitation05, scene);
    }
    Work work = Work::Cancel;
    if (s_route == Route::Install06) {
        work = pairing.state == PairState::Failed || pairing.state == PairState::TimedOut
            ? Work::Retry : Work::Start;
    }
    s_cancel_to_install = work == Work::Cancel;
    const esp_err_t request_error = enqueue(work, pairing.attempt_id);
    if (request_error != ESP_OK) {
        return show_error(scene, request_error);
    }
    if (work != Work::Cancel) {
        s_session = 0;
        return enter_route(Route::Pairing07, scene);
    }
    if (s_route == Route::Pairing07) {
        // Retire the session's secret immediately; navigation still awaits Cancelled.
        const esp_err_t action_error = prepare_action(OnboardingAction::Cancel, false);
        return action_error == ESP_OK ? show_pairing_progress(scene, "Closing pairing") : action_error;
    }
    const esp_err_t action_error = prepare_action(OnboardingAction::Cancel, false);
    return action_error == ESP_OK ? attach_content(scene) : action_error;
}

// scene: Current decision/menu frame, replaced only after an explicit choice.
// action: Typed action of the capsule that owns the acquired press.
// Exception: Returns backend admission, snapshot or route preparation failures.
esp_err_t activate_setup_choice(SceneFrame &scene, OnboardingAction action)
{
    ScenePresentation presentation{};
    esp_err_t error = prepare_scene_presentation(scene, presentation);
    if (error != ESP_OK || !presentation.content_visible || !s_view.actions_visible || s_timeout_cleanup.pending || s_command_pending ||
        s_work_busy.load(std::memory_order_acquire)) {
        return error;
    }
    const char *label = nullptr;
    error = action_label(action, label);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(kTag, "Setup choice activated: %s", label != nullptr ? label : "None");
    if (action == OnboardingAction::KeepPairing && s_route == Route::CodexReplace) {
        return begin_codex_connection(scene, CodexConnectionIntent::Retry);
    }
    if (action == OnboardingAction::ReplacePairing && s_route == Route::CodexReplace) {
        return begin_codex_connection(scene, CodexConnectionIntent::ConfirmReplacement);
    }
    if (action == OnboardingAction::ForgetPairing && s_route == Route::CodexReplace) {
        return enter_route(Route::CodexForget, scene);
    }
    if (action == OnboardingAction::KeepSaved && s_route == Route::CodexForget) {
        return begin_codex_connection(scene, CodexConnectionIntent::Setup);
    }
    if (action == OnboardingAction::ConfirmForget && s_route == Route::CodexForget) {
        return begin_codex_cleanup(scene, CodexCleanup::Forget);
    }
    if (action == OnboardingAction::Options) {
        s_options_from_setup = true;
        return enter_route(Route::MakeItYours10, scene);
    }
    if (action == OnboardingAction::DeclineCodex || action == OnboardingAction::Later) {
        // No persistent refusal and no backend request. Next cold boot may ask again.
        scene = {};
        reset_onboarding();
        return ESP_OK;
    }
    if (action == OnboardingAction::NetworkSetup) {
        s_startup_pending = false;
        s_codex_owned = false;
        return enter_route(Route::Hello04, scene);
    }
    if (action == OnboardingAction::AcceptCodex || action == OnboardingAction::ConnectCodex) {
        return begin_codex_connection(scene, CodexConnectionIntent::Setup);
    }
    ESP_LOGE(kTag, "Setup choice rejected: id=%u (%s)", to_index(action), esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
}

// point: Real logical coordinate, including contacts consumed by Debug controls.
// pressed: Actual contact state, not an inferred sensor value.
// pressed_edge: True only for the first pressed sample of the contact.
// inactivity_active_time_us: Awake route clock; only actual touch events refresh inactivity.
// Exception: Returns the original screen-activity clock error.
esp_err_t observe_onboarding_touch_activity(CODY2D::Point point, bool pressed,
                                            bool pressed_edge, int64_t inactivity_active_time_us)
{
    const bool moved_contact = pressed && s_activity_touch_pressed &&
        (point.x != s_activity_touch_point.x || point.y != s_activity_touch_point.y);
    const bool released_contact = !pressed && s_activity_touch_pressed;
    s_activity_touch_pressed = pressed;
    if (pressed) {
        s_activity_touch_point = point;
    }
    if (pressed_edge || moved_contact || released_contact) {
        return refresh_screen_activity(inactivity_active_time_us);
    }
    return ESP_OK;
}

// scene: Current route; swipes use the exact same gated actions as the chevrons.
// point: Real logical coordinate from the existing touch acquisition owner.
// pressed: Actual contact state, not an inferred sensor value.
// pressed_edge: True only for the first pressed sample of the contact.
// rotation_degrees: Shared evaluated orientation, never a second sensor read.
// consumed: True for controls/list gestures and a recognized navigation release.
// Exception: Returns the real route, persistence, snapshot or action error.
esp_err_t handle_onboarding_touch(SceneFrame &scene, CODY2D::Point point,
                                  bool pressed, bool pressed_edge,
                                  uint16_t rotation_degrees, bool &consumed)
{
    consumed = false;
    if (s_route == Route::None) {
        return ESP_OK;
    }
    if (s_route == Route::Hello04) {
        if (!s_hello_waits_for_touch) {
            return ESP_OK;
        }
        // Timeout Welcome is an escape back to choices. Do not start Wi-Fi on
        // the first pressed sample: the character hold must own its full contact.
        consumed = true;
        if (pressed_edge) {
            s_swipe_active = can_open_setup_menu();
            s_swipe_start = point;
            s_swipe_rotation = rotation_degrees;
        }
        const int dx = point.x - s_swipe_start.x;
        const int dy = point.y - s_swipe_start.y;
        if (s_swipe_rotation != rotation_degrees || (pressed && dx * dx + dy * dy > 12 * 12)) {
            s_swipe_active = false;
        }
        if (!pressed && s_swipe_active) {
            s_swipe_active = false;
            return can_open_setup_menu() ? open_setup_menu(scene) : ESP_OK;
        }
        return ESP_OK;
    }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    if (s_route == Route::DebugText) {
        consumed = true;
        if (!pressed_edge) {
            return ESP_OK;
        }
        s_pairing_debug_dismissed = true;
        return enter_route(Route::Online09, scene);
    }
#endif
    if (s_swipe_route != s_route || (s_swipe_active && s_swipe_rotation != rotation_degrees)) {
        s_swipe_active = false;
        cancel_look_touch();
    }
    const CODY2D::CapsuleControl back{kBackBounds, {}, NavigationData::top, NavigationData::bottom, true};
    const CODY2D::CapsuleControl next{kForwardBounds, {}, NavigationData::top, NavigationData::bottom, true};
    if (pressed_edge) {
        if (is_codex_event_route(s_route) && s_view.actions_visible) {
            ScenePresentation presentation;
            const esp_err_t presentation_error = prepare_scene_presentation(scene, presentation);
            if (presentation_error != ESP_OK || !presentation.content_visible) {
                return presentation_error;
            }
            const auto *action = event_presentation::hit_action(s_event_plan,
                s_view.actions_visible, point.x, point.y);
            if (action != nullptr) {
                consumed = true;
                if (action->id == event_presentation::ActionId::Choose) {
                    auto navigation = event_navigation();
                    event_presentation::Effect effect;
                    const esp_err_t error = event_presentation::activate(s_presented_event,
                        event_context(), *action, navigation, effect);
                    if (error != ESP_OK) { return error; }
                    retain_event_navigation(navigation, effect);
                    return render_codex_event(scene);
                }
                return activate_codex_event(scene, event_action_identity(action->id));
            }
        }
        if (s_route == Route::MakeItYours10 && s_view.actions_visible && !s_command_pending) {
            for (uint8_t index = 0; index < s_view.preference_count; ++index) {
                const auto &preference = s_view.preferences[index];
                if (!CODY2D::capsule_contains(preference.control, point)) {
                    continue;
                }
                consumed = true;
                esp_err_t error = enqueue_preference(preference.identity, preference.requested_value);
                if (error != ESP_OK) {
                    s_preference_error = error;
                }
                error = prepare_options_content(scene);
                return error == ESP_OK ? attach_content(scene) : error;
            }
        }
        const bool choice_route = s_route == Route::CodexChoice || s_route == Route::SetupMenu ||
            s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget;
        if (choice_route && s_view.actions_visible) {
            const bool primary_hit = s_view.action_visible && CODY2D::capsule_contains(s_view.action, point);
            const bool alternative_hit = s_view.alternative_id != OnboardingAction::None &&
                CODY2D::capsule_contains(s_view.alternative, point);
            const bool options_hit = s_view.options.caption.valid && CODY2D::capsule_contains(s_view.options, point);
            if (primary_hit || alternative_hit || options_hit) {
                consumed = true;
                const CODY2D::CapsuleControl *control = &s_view.options;
                OnboardingAction action = s_view.options_id;
                const char *slot = "options";
                if (primary_hit) {
                    control = &s_view.action;
                    action = s_view.action_id;
                    slot = "right";
                } else if (alternative_hit) {
                    control = &s_view.alternative;
                    action = s_view.alternative_id;
                    slot = "left";
                }
                ESP_LOGI(kTag, "Setup capsule pressed: x=%d y=%d slot=%s", point.x, point.y,
                    slot);
                return control->enabled ? activate_setup_choice(scene, action) : ESP_OK;
            }
        }
        const bool footer_hit = uses_footer_action() && s_view.actions_visible && s_view.action_visible &&
            CODY2D::capsule_contains(s_view.action, point);
        if (footer_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene, s_view.action_id == OnboardingAction::Cancel ?
                NavigationDirection::Previous : NavigationDirection::Next);
        }
        const bool back_hit = previous_control_visible() && CODY2D::capsule_contains(back, point);
        const bool next_hit = next_control_visible() && CODY2D::capsule_contains(next, point);
        if (back_hit || next_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene,
                back_hit ? NavigationDirection::Previous : NavigationDirection::Next);
        }
        // A candidate does not steal Cody's drag. Only an unmistakable horizontal
        // release navigates; vertical drags retain the look-list's scroll behavior.
        s_swipe_active = point_in_rect({73, 73, 320, 320}, point);
        s_swipe_start = point;
        s_swipe_last = point;
        s_swipe_route = s_route;
        s_swipe_rotation = rotation_degrees;
        s_swipe_axis = GestureAxis::Undecided;
    }
    if (pressed && s_swipe_active) {
        s_swipe_last = point;
        const int dx = std::abs(point.x - s_swipe_start.x);
        const int dy = std::abs(point.y - s_swipe_start.y);
        if (s_swipe_axis == GestureAxis::Undecided && std::max(dx, dy) >= 12) {
            if (dx * 2 > dy * 3) {
                s_swipe_axis = GestureAxis::Horizontal;
            } else if (dy * 2 > dx * 3) {
                s_swipe_axis = GestureAxis::Vertical;
            }
        }
    }
    if (!pressed && s_swipe_active) {
        s_swipe_active = false;
        const int dx = s_swipe_last.x - s_swipe_start.x;
        const int dy = s_swipe_last.y - s_swipe_start.y;
        if (s_route == Route::CheckFailures24 && s_view.actions_visible &&
            s_swipe_axis == GestureAxis::Vertical && std::abs(dy) >= 36) {
            consumed = true;
            auto navigation = event_navigation();
            const bool changed = event_presentation::scroll(s_presented_event, navigation,
                s_view.actions_visible, dx, dy);
            retain_event_navigation(navigation);
            return changed ? render_codex_event(scene, true) : ESP_OK;
        }
        if (s_swipe_axis == GestureAxis::Horizontal && std::abs(dx) >= 48 &&
            std::abs(dx) * 2 > std::abs(dy) * 3) {
            cancel_look_touch();
            consumed = true;
            return activate_onboarding_navigation(scene,
                dx < 0 ? NavigationDirection::Next : NavigationDirection::Previous);
        }
    }
    if (s_route == Route::Look11) {
        LookTouchResult result;
        const esp_err_t error = handle_look_touch(scene, pressed ? point : s_swipe_last,
            pressed, pressed_edge, s_swipe_axis == GestureAxis::Horizontal, result);
        consumed = result.consumed;
        if (error != ESP_OK) {
            return error;
        }
        if (result.save_id != nullptr) {
            return enqueue_look_save(scene, result.save_id);
        }
        if (result.selection_confirmed) {
            const esp_err_t content_error = attach_look_content(scene, s_navigation_revision);
            if (content_error != ESP_OK) {
                return content_error;
            }
            consumed = true;
        }
        if (result.done_requested) {
            SceneId destination;
            const esp_err_t destination_error = look_exit_destination(LookExit::Done, destination);
            if (destination_error != ESP_OK) {
                return destination_error;
            }
            consumed = true;
            return enter_host_destination(destination, scene);
        }
    }
    return ESP_OK;
}

// direction: Icon orientation in the same logical coordinate basis as touch.
// enabled: Disabled actions keep their full target but use dimmer ink.
// strip: Caller-owned destination tile, including the lateral region.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the first real capsule or stroke drawing error.
esp_err_t draw_navigation_chevron(NavigationDirection direction, bool enabled,
                                  CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto bounds = direction == NavigationDirection::Previous ? kBackBounds : kForwardBounds;
    const CODY2D::CapsuleControl control{bounds, {}, NavigationData::top, NavigationData::bottom, enabled};
    esp_err_t error = CODY2D::draw_capsule_rows(control, strip, first_row, first_column);
    const float center_x = bounds.x + bounds.width / 2.f;
    const float center_y = bounds.y + bounds.height / 2.f;
    const float sign = direction == NavigationDirection::Previous ? -1.f : 1.f;
    const CODY2D::Vec2 points[] = {{center_x - sign * 3.f, center_y - 8.f},
        {center_x + sign * 5.f, center_y}, {center_x - sign * 3.f, center_y + 8.f}};
    const uint16_t ink = CODY2D::fade_rgb565_to_black(NavigationData::ink, enabled ? 255 : 128);
    for (uint16_t index = 0; error == ESP_OK && index < 2; ++index) {
        error = CODY2D::stroke_rows(strip, first_row, points[index], points[index + 1U], 2.f, ink, first_column);
    }
    return error;
}

// strip: Valid caller-owned logical tile outside the content slot as well.
// first_row: Logical tile y origin, shared with all cloud/text rendering.
// first_column: Logical tile x origin, shared with hit testing.
// Exception: Returns the real surface or primitive drawing error.
esp_err_t draw_onboarding_navigation(CODY2D::Surface strip, uint16_t first_row,
                                    uint16_t first_column)
{
    esp_err_t error = ESP_OK;
    if (previous_control_visible()) {
        const bool enabled = !s_command_pending &&
            (s_view.action_id != OnboardingAction::Cancel || s_view.action.enabled);
        error = draw_navigation_chevron(NavigationDirection::Previous, enabled, strip, first_row, first_column);
    }
    if (error == ESP_OK && next_control_visible()) {
        error = draw_navigation_chevron(NavigationDirection::Next, !s_command_pending && s_view.action.enabled,
            strip, first_row, first_column);
    }
    return error;
}

} // namespace CODY
