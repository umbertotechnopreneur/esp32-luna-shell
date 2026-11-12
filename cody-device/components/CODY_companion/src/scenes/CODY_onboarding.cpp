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
#include "CODY_text_entry.hpp"
#include "CODY_ui_navigation.hpp"
#include "CODY_selected_label.hpp"
#include "CODY_activity_description.hpp"
#include "CODY_activity_identity.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_observation.hpp"
#include "CODY_codex_event_presentation.hpp"
#include "CODY_approval_notice.hpp"
#include "CODY_muse_frame.hpp"
#include "CODY_pixel_avatar.hpp"
#include "CODY_wifi_menu_art.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

#include "CODY2D_controls.hpp"
#include "CODY_title_style.hpp"
#include "CODY_ui_components.hpp"
#include "CODY_flat_menu_row.hpp"
#include "CODY_settings.hpp"
#include "CODY_page_marks.hpp"
#include "CODY_gradient_text.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_widget_state.hpp"
#include "CODY_codex_audio.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "scene_data.hpp"
#include "home_dashboard_fonts.hpp"
#include "navigation_data.hpp"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "qrcode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
#include "CODY_relay_probe.hpp"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#endif

namespace CODY {
// scene: Worker-owned setup destination selected from canonical radio state.
// changed: Receives whether the startup decision requires a new frame.
// Exception: Returns the original snapshot, route or admission failure.
esp_err_t evaluate_saved_setup(SceneFrame &scene, bool &changed);

namespace {
constexpr char kTag[] = "CODY_onboarding";
#if defined(CODY_DEVICE_UI_ONLY)
constexpr bool kDeviceFirstSetup = true;
#else
constexpr bool kDeviceFirstSetup = false;
#endif
constexpr char kInstallUrl[] = "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr char kAppleInstallUrl[] = "https://apps.apple.com/app/esp-ble-provisioning/id1473590141";
constexpr CODY2D::Rect kHomePairButton{73, 282, 320, 48};
constexpr CODY2D::Rect kHomeLocalWifiButton{83, 337, 300, 48};
constexpr CODY2D::Rect kHomeMenuMascot{169, 150, 128, 128};
constexpr CODY2D::Rect kIntroApple{112, 305, 110, 70};
constexpr CODY2D::Rect kIntroAndroid{244, 305, 110, 70};
constexpr DamageRect kHomeMenuContent{73, 80, 393, 385};
static_assert(wifi_menu_art::kPair.width + wifi_menu_art::kChoiceIconSize + wifi_menu_art::kChoiceGap <=
    kHomePairButton.width && wifi_menu_art::kPair.height <= kHomePairButton.height);
static_assert(wifi_menu_art::kWifi.width + wifi_menu_art::kChoiceIconSize + wifi_menu_art::kChoiceGap <=
    kHomeLocalWifiButton.width && wifi_menu_art::kWifi.height <= kHomeLocalWifiButton.height);
constexpr CODY2D::Rect kAndroidButton{83, 154, 145, 42};
constexpr CODY2D::Rect kAppleButton{238, 154, 145, 42};
// Largest centered square below the title whose corners remain inside the round panel.
constexpr CODY2D::Rect kLargeQrBounds{89, 128, 288, 288};
constexpr uint16_t kQrModulesMax = 57; // Version 10; bounded independently of the framebuffer.
constexpr uint16_t kQrBytes = (kQrModulesMax * kQrModulesMax + 7U) / 8U;
constexpr uint16_t kQrQuietModules = 4;
constexpr CODY2D::Rect kInstallQrBounds{137, 140, 192, 192};
constexpr CODY2D::Rect kSessionQrBounds = kLargeQrBounds;
constexpr uint16_t kSessionCaptionTop = 156;
constexpr CODY2D::Rect kFooterBounds{123, 390, 220, 44};
constexpr CODY2D::Rect kOptionsFooterBounds = kDeviceFirstSetup
    ? CODY2D::Rect{123, 382, 220, 52} : kFooterBounds;
constexpr CODY2D::Rect kResetCancelBounds{93, 250, 280, 56};
constexpr CODY2D::Rect kResetConfirmBounds{93, 310, 280, 56};
constexpr CODY2D::Rect kSingleActionBounds{113, 352, 240, 72};
constexpr CODY2D::Rect kLeftCardBounds{73, 260, 154, 104};
constexpr CODY2D::Rect kRightCardBounds{239, 260, 154, 104};
constexpr CODY2D::Rect kOptionsBounds{153, 376, 160, 52};
constexpr CODY2D::Rect kKeepPcBounds{83, 302, 90, 90};
constexpr CODY2D::Rect kPairPcBounds{188, 302, 90, 90};
constexpr CODY2D::Rect kForgetPcBounds{293, 302, 90, 90};
constexpr CODY2D::Rect kSetupCodexBounds = kDeviceFirstSetup
    ? CODY2D::Rect{78, 170, 140, 168} : CODY2D::Rect{93, 230, 280, 56};
constexpr CODY2D::Rect kSetupWifiBounds = kDeviceFirstSetup
    ? CODY2D::Rect{248, 170, 140, 168} : CODY2D::Rect{93, 298, 280, 56};
constexpr uint16_t kConnectionIconSize = 54;
constexpr int16_t kConnectionIconY = 219;
constexpr uint16_t kConnectionsHintTop = 136;
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
constexpr CODY2D::Rect kRelayCheckBounds{123, 366, 220, 52};
// Start with the ESP-IDF HTTPS example's budget on a separate internal stack.
// Handshake high-water and minimum free internal heap remain board checks.
constexpr uint32_t kRelayStackBytes = 8192;
#endif
constexpr CODY2D::Rect kSetupOptionsBounds{153, 382, 160, 52};
constexpr uint16_t kPairingBackground = Theme::surface_pairing;
constexpr uint16_t kPairingAccent = Theme::primary_default;
constexpr uint16_t kPairingTitle = Theme::text_primary;
constexpr uint16_t kPairingCaption = Theme::text_secondary;
constexpr uint16_t kOfflineAmber = Theme::accent_warm;
constexpr uint16_t kOfflineDiscColor = Theme::surface_offline_disc;
constexpr int64_t kWifiConnectingStepUs = 200000;
constexpr uint16_t kWifiConnectingHighlightTop[]{172, 158, 144, 130};
constexpr uint16_t kWifiConnectingMuted = Theme::chrome_wifi_search_muted;
constexpr CODY2D::Rect kWifiConnectingGlyph{197, 116, 72, 72};
constexpr uint16_t kWifiStatusTitleTop = 200;
constexpr uint16_t kWifiStatusDetailTop = 248;
constexpr uint16_t kWifiStatusNetworkTop = 280;
constexpr uint16_t kWifiReadyMint = Theme::secondary_default;
constexpr CODY2D::Rect kCodexPairingGlyph{205, 148, 56, 56};
constexpr int64_t kCodexWaitingStepUs = 400000;
constexpr uint16_t kCodexWaitingColors[]{Theme::gradients_codex_waiting_frames_0,
    Theme::gradients_codex_waiting_frames_1, Theme::gradients_codex_waiting_frames_2,
    Theme::gradients_codex_waiting_frames_3};
constexpr uint16_t kCodexPairingTitleTop = 88;
constexpr uint16_t kCodexPairingContentTop = 140;
constexpr uint16_t kCodexPairingCodeTop = kDeviceFirstSetup ? 236 : 338;
constexpr uint16_t kPcWaitCaptionTop = kDeviceFirstSetup ? 256 : 300;
constexpr uint16_t kPcWaitDetailTop = kDeviceFirstSetup ? 284 : 327;
constexpr CODY2D::Rect kOfflineDisc{191, 76, 84, 84};
constexpr CODY2D::Rect kOfflineGlyph{203, 88, 60, 60};
constexpr CODY2D::Rect kGuideGlyph{206, 66, 54, 54};
constexpr uint16_t kGuideTitleTop = 130;
constexpr uint16_t kGuideCaptionTop = 186;
constexpr CODY2D::Rect kGuideDownloadBounds{93, 274, 280, 62};
constexpr CODY2D::Rect kGuidePairBounds{93, 348, 280, 64};
constexpr CODY2D::Rect kOfflineSetupBounds{93, 272, 280, 58};
constexpr CODY2D::Rect kOfflineLaterBounds{153, 350, 160, 50};
// One grid keeps the category rows aligned with the Options menu.
constexpr uint16_t kOptionsTitleTop = 82;
constexpr uint16_t kPreferenceLeft = kDeviceFirstSetup ? 93 : 113;
constexpr uint16_t kPreferenceWidth = kDeviceFirstSetup ? 280 : 240;
constexpr uint16_t kPreferenceTop = kDeviceFirstSetup ? 130 : 194;
constexpr uint16_t kPreferenceHeight = kDeviceFirstSetup ? 56 : 50;
constexpr uint16_t kPreferenceStep = kDeviceFirstSetup ? 58 : 60;
constexpr uint8_t kPreferencesPerPage = 4;

static_assert(!kDeviceFirstSetup || kPreferenceTop + (kPreferencesPerPage - 1U) * kPreferenceStep +
    kPreferenceHeight < kOptionsFooterBounds.y);
static_assert(kDeviceFirstSetup || kPreferenceTop + 2U * kPreferenceStep +
    kPreferenceHeight < kOptionsFooterBounds.y);
constexpr uint16_t kEventCaptionWidth = 300;
constexpr uint16_t kCaptionWidth = 232;
// Authenticated PC presence does not imply a selected chat.
constexpr char kCodexConnectedCaption[] = "Codex connected";
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
constexpr uint16_t kDebugTextWidth = 386;
constexpr uint16_t kDebugTextColor = Theme::text_default;
constexpr uint8_t kPairingDebugLineCount = 6;
#endif
constexpr uint8_t kContentLineCapacity = 8;
constexpr size_t kDebugLineBytes = 64;
constexpr auto kForwardBounds = UI::kForwardBounds;
constexpr auto kBackBounds = UI::kBackBounds;
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
    InstallQrApple,
    SessionQr,
    Start,
    Retry,
    Cancel,
    CompleteFirstBoot,
    UseCompanion,
    CodexStart,
    CodexReplace,
    CodexCopyCode,
    CodexDiscover,
    CodexCancel,
    CodexForget,
    SavePreference,
    ScheduleBirthIntro,
    SaveWidgetVisibility,
    LocalWifiScan,
    LocalWifiConnect,
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    CheckRelay,
#endif
};
using NavigationDirection = UI::NavigationDirection;
enum class CodexConnectionIntent : uint8_t { Setup, Retry, ConfirmReplacement };
enum class CodexCleanup : uint8_t { None, Cancel, Forget };
enum class OnboardingAction : uint8_t {
    None, ShowQr, PairCody, Cancel, Retry, Continue, Next, Done, LetsGo,
    AcceptCodex, DeclineCodex, ConnectCodex, NetworkSetup, Later, Options, KeepPairing, ReplacePairing,
    ForgetPairing, ConfirmForget, KeepSaved, DisplayOptions, BehaviorOptions, MoreBehavior, MoreDisplay, OpenTime, SyncPcTime,
    ScheduleBirthIntro,
    Connections, CodexStatistics, VisibleScreens, OpenPairingGuide,
    FactoryReset, ConfirmFactoryReset, LocalTimer, RestartDevice, ConfirmRestart, QuickGuide,
    CheckRelay, SynthOptions, BrowseCodex, AndroidApp, AppleApp, LocalWifi, AudioLevels, OpenAbout,
    MoreBehaviorSettings, QuietHours, QuietToggle, QuietStart, QuietEnd, QuietHour, QuietMinute, QuietSave,
    OpenEvent = 64, ViewFailures = 65, SendReply = 66, DismissEvent = 67, RetryEvent = 68
};
enum class GestureAxis : uint8_t { Undecided, Horizontal, Vertical };
enum class OptionsPage : uint8_t {
    Menu, MenuMore, MenuMaintenance, Display, DisplayRings, Time, Behavior, BehaviorMore, BehaviorMoreSettings,
    QuietHours, QuietEdit,
    RestartConfirm, RestartPending, ResetConfirm, ResetPending, ResetFailed,
    GuideButtons, GuideTouch, GuideOffline, SynthVoice
};

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
    PreferenceId preference = PreferenceId::ShakeWake;
    uint32_t preference_value = 0;
    WidgetVisibility widget_visibility{};
};
struct Result {
    Command command{};
    esp_err_t error = ESP_OK;
    bool preference_changed = false;
    QrModules qr{};
    char codex_code[7]{};
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    RelayProbeResult relay{};
#endif
};
struct PreferenceControl {
    OnboardingAction action = OnboardingAction::None;
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

// Display-task-owned buffers for one prepared route. TextLine borrows these
// buffers, so serialized route preparation controls their lifetime and PSRAM access.
struct ContentView {
    CODY2D::TextLine lines[kContentLineCapacity]{};
    uint8_t line_count = 0;
    uint16_t visible_characters[kContentLineCapacity]{};
    bool instant_lines[kContentLineCapacity]{};
    CODY2D::CapsuleControl action{};
    bool action_visible = false;
    bool actions_visible = false;
    uint16_t action_icon_size = 0; // A nonzero size draws the two action glyphs on the page background.
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
    PreferenceControl preferences[kPreferencesPerPage]{};
    uint8_t preference_count = 0;
    uint8_t menu_page = 0;
    uint8_t menu_page_count = 3;
    EventChoiceControl event_choices[3]{};
    uint8_t event_choice_count = 0;
    bool chat_waiting = false;
    QrModules qr{};
    CODY2D::Rect qr_bounds{};
    CODY2D::Rect header_icon_bounds{};
    uint16_t header_icon_color = 0;
    CODY2D::ActionIcon header_icon = CODY2D::ActionIcon::None;
    bool wifi_menu = false;
    bool platform_choices = false;
    char wifi_menu_name[wifi_menu_art::kNameLength + 1U]{};
    char error_text[32]{};
    char debug_text[kContentLineCapacity][kDebugLineBytes]{};
    // Serialized scene-owned metadata and one bounded font window, outside the UI task stack.
    ActivityDescriptionState activity_description{};
    ActivityIdentityState activity_identity{};
    const ApprovalNoticeState *approval_notice = nullptr;
    bool approval_notice_visible = false;
};
#ifdef ESP_PLATFORM
// IDF constructs the default view after PSRAM startup; host fixtures use ordinary RAM.
alignas(ContentView) EXT_RAM_BSS_ATTR uint8_t s_view_storage[sizeof(ContentView)]{};
ContentView &s_view = *new (s_view_storage) ContentView{};
#else
ContentView s_view;
#endif
#ifdef ESP_PLATFORM
// The display worker keeps the static menu pose off its call stack.
alignas(PixelAvatarFrame) EXT_RAM_BSS_ATTR uint8_t s_wifi_mascot_storage[sizeof(PixelAvatarFrame)]{};
PixelAvatarFrame &s_wifi_mascot = *new (s_wifi_mascot_storage) PixelAvatarFrame{};
#else
PixelAvatarFrame s_wifi_mascot{};
#endif

enum class LocalWifiPage : uint8_t { None, Networks, Password, ConfirmOpen, Connecting };
struct LocalWifiView {
    CODY2D::TextLine subtitle{};
    CODY2D::TextLine detail{};
    CODY2D::CapsuleControl rows[4]{};
    CODY2D::TextLine network_signals[3]{};
    CODY2D::CapsuleControl scan{};
    CODY2D::CapsuleControl confirm{};
    char labels[4][34]{};
    char signal_labels[3][16]{};
    char detail_text[64]{};
    TextEntry entry{};
    TextEntryView keyboard{};
    TextEntryTouch keyboard_touch{};
    SceneContent keyboard_content{};
    SelectedLabel selected_label{};
    LUNASHELL::WifiScanSnapshot scan_observation{};
    LUNASHELL::WifiScanSnapshot scan_scratch{};
    LUNASHELL::ConnectivityObservation connectivity_scratch{};
    char selected_ssid[33]{};
    LUNASHELL_wifi_selection_t selected_ap{};
    uint64_t presented_revision = 0;
    uint32_t scan_baseline_generation = 0;
    uint32_t selected_scan_generation = 0;
    uint32_t connection_attempt_baseline = 0;
    uint32_t association_baseline = 0;
    int64_t connect_started_us = 0;
    uint32_t revision = 0;
    uint8_t offset = 0;
    uint8_t hourglass_frame = 0;
    int8_t selected_network = -1;
    int8_t touch_control = -1;
    CODY2D::Point touch_start{};
    CODY2D::Point touch_last{};
    uint16_t touch_rotation = 0;
    LocalWifiPage page = LocalWifiPage::None;
    bool scanning_requested = false;
    bool connect_submitted = false;
    bool touch_moved = false;
};
#ifdef ESP_PLATFORM
alignas(LocalWifiView) EXT_RAM_BSS_ATTR uint8_t s_local_wifi_storage[sizeof(LocalWifiView)]{};
LocalWifiView &s_local_wifi = *new (s_local_wifi_storage) LocalWifiView{};
#else
LocalWifiView s_local_wifi{};
#endif
// The worker consumes this bounded mailbox once. It is never copied to Result.
struct LocalWifiCredentials {
    char ssid[33]{};
    char password[65]{};
    LUNASHELL_wifi_selection_t selection{};
};
LocalWifiCredentials s_local_wifi_credentials{};
#ifdef ESP_PLATFORM
// Menu entry and Debug exit share one hardware-only buffer outside the touch stack.
alignas(LUNASHELL::HardwareStatus) EXT_RAM_BSS_ATTR
    uint8_t s_connection_status_storage[sizeof(LUNASHELL::HardwareStatus)]{};
LUNASHELL::HardwareStatus &s_connection_status =
    *new (s_connection_status_storage) LUNASHELL::HardwareStatus{};
#else
LUNASHELL::HardwareStatus s_connection_status{};
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
#ifdef ESP_PLATFORM
// Independent from ContentView resets; same-event refreshes retain the visible title clock.
alignas(ApprovalNoticeState) EXT_RAM_BSS_ATTR uint8_t s_approval_storage[sizeof(ApprovalNoticeState)]{};
ApprovalNoticeState &s_approval_notice = *new (s_approval_storage) ApprovalNoticeState{};
#else
ApprovalNoticeState s_approval_notice{};
#endif
LUNASHELL::CodexCompactionState s_presented_compaction = LUNASHELL::CodexCompactionState::None;
bool s_presented_compaction_feedback = false;
Route s_route = Route::None;
bool s_options_from_setup = false;
bool s_connections_from_options = false;
WidgetOpenRequest s_widget_open_request = WidgetOpenRequest::None;
OptionsPage s_options_page = OptionsPage::Menu;
esp_err_t s_preference_error = ESP_OK;
bool s_retry_birth_intro = false;
bool s_birth_intro_scheduled = false;
uint32_t s_factory_reset_request_id = 0;
esp_err_t s_factory_reset_error = ESP_OK;
esp_err_t s_restart_error = ESP_OK;
PreferenceId s_requested_preference = PreferenceId::ShakeWake;
uint32_t s_requested_preference_value = 0;
LUNASHELL::CodyOptionsObservation s_presented_options{};
LUNASHELL::ClockSnapshot s_quiet_clock{};
uint16_t s_quiet_draft = 0;
bool s_quiet_edit_start = true;
// The settings worker owns this scratch; the display reads only the completed result.
LUNASHELL::CodyOptionsObservation s_preference_before_write{};
bool s_preference_confirmation = false;
bool preference_would_change(PreferenceId preference, uint32_t value,
    const LUNASHELL::CodyOptionsObservation &observation);
LUNASHELL::CodexPresenceStatus s_pc_time_observation{};
LUNASHELL::PcTimeSyncState s_presented_pc_time_state = LUNASHELL::PcTimeSyncState::Idle;
esp_err_t s_presented_pc_time_error = ESP_OK;
bool s_presented_pc_time_connected = false;
esp_err_t s_manual_pc_time_error = ESP_OK;
Route s_setup_return = Route::None;
Route s_pairing_guide_return = Route::Hello04;
bool s_home_wifi_flow = false;
bool s_home_wifi_pairing_direct = false;
bool s_home_wifi_intro = false;
bool s_home_wifi_apple = false;
bool s_home_wifi_exit_pending = false;
uint8_t s_home_wifi_chrome_opacity = 0;
bool s_companion_seen_selection = false;
int64_t s_session_ended_at_us = -1;
LUNASHELL::CodexActivitySnapshot s_presented_activity{};
bool s_home_codex = false;
bool s_local_presentation_valid = false;
CommandHintsState s_command_hints{};
HomeReadyDwell s_home_ready_dwell{};
HomeReplyDwell s_home_reply_dwell{};
uint32_t s_activity_description_revision = 0;
uint32_t s_activity_identity_revision = 0;
ActivityObservationState s_activity_observation{};
LUNASHELL::CodexActivityState s_presented_work_phase = LUNASHELL::CodexActivityState::Working;
LUNASHELL::CodexEventSnapshot s_presented_event{};
// The display owner alone reads this scratch snapshot. Schema growth must not
// add a second large by-value event to the nested renderer stack.
LUNASHELL::CodexEventSnapshot s_observed_event{};
EventStage s_event_stage = EventStage::Notice;
EventRetry s_event_retry = EventRetry::None;
esp_err_t s_event_ui_error = ESP_OK;
esp_err_t s_event_observation_error = ESP_OK;
bool s_event_online = false;
bool s_event_reply_admitted = false;
bool s_event_dismiss_admitted = false;
char s_event_selected_id[13]{};
char s_event_selection_target[33]{};
char s_inbox_question[33]{};
bool s_inbox_available = false;
uint8_t s_event_failure_page = 0;
uint8_t s_event_choice_page = 0;
uint8_t s_event_detail_page = 0;
// Display-owner buffer avoids putting the complete presentation plan on its task stack.
event_presentation::Plan s_event_plan{};
int64_t s_checks_passed_readable_at_us = -1;
#if !defined(CODY_DEVICE_UI_ONLY)
char s_last_event_audio_request[33]{};
char s_last_delivered_audio_request[33]{};
#endif
QuestionAudioSchedule s_question_audio_schedule{};
// Cue history survives Setup re-entry; hidden status is never replayed as a new action.
#if !defined(CODY_DEVICE_UI_ONLY)
char s_last_audio_chat[65]{};
LUNASHELL::CodexActivityState s_last_audio_phase = LUNASHELL::CodexActivityState::None;
#endif
bool s_startup_pending = false;
bool s_startup_pair_missing = false;
esp_err_t s_startup_wifi_error = ESP_OK;
int64_t s_startup_wifi_wait_started_us = -1;
uint32_t s_startup_wifi_attempt = 0;
bool s_startup_offline_home = false;
bool s_startup_network_passive = false;
bool s_startup_network_presented = false;
uint32_t s_generation = 1;
std::atomic<uint32_t> s_presentation_generation{1};
uint32_t s_revision = 0;
uint32_t s_navigation_revision = 0;
uint32_t s_text_generation = 0;
uint32_t s_attempt = 0;
uint32_t s_session = 0;
bool s_command_pending = false;
bool s_cancel_to_install = false;
bool s_pairing_from_guide = false;
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
bool s_home_closing_codex = false;
bool s_codex_error_allows_setup = false;
esp_err_t s_codex_visible_error = ESP_OK;
char s_codex_code[7]{};
esp_err_t s_visible_error = ESP_OK;
Work s_failed_work = Work::Start;
bool s_local_failure = false;
bool s_swipe_active = false;
bool s_swipe_moved = false;
bool s_options_tap_admitted = false;
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
alignas(LUNASHELL::ConnectivityObservation) EXT_RAM_BSS_ATTR
    uint8_t s_action_connectivity_storage[sizeof(LUNASHELL::ConnectivityObservation)]{};
auto &s_action_connectivity = *new (s_action_connectivity_storage) LUNASHELL::ConnectivityObservation{};
#else
Result s_result{};
LUNASHELL::ConnectivityObservation s_action_connectivity{};
#endif
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
// Display-owned receipt for the current Connections visit.
RelayProbeResult s_relay_result{};
bool s_relay_attempted = false;
#endif
// Startup encodes the static About link before the action worker owns the library.
QrModules *s_encoding_target = nullptr;
#if defined(CODY_DEVICE_UI_ONLY)
constexpr char kAboutProjectUrl[] = "https://github.com/umbertotechnopreneur/esp32-luna-shell";
constexpr char kAboutProjectHeading[] = "Scan for more info";
// Leave the shared page marks below the complete four-module QR quiet zone.
constexpr CODY2D::Rect kAboutProjectBounds{117, 128, 232, 232};
QrModules s_about_project_qr{};
esp_err_t s_about_project_error = ESP_ERR_INVALID_STATE;
#endif

// route: Stable route identity whose bounded local inactivity policy is requested.
const ScreenInactivityPolicy *screen_inactivity_policy(Route route)
{
    // The device-first pairing service owns expiry even while the phone is idle.
    if (s_home_wifi_flow && (route == Route::Invitation05 || route == Route::Install06 ||
        (s_home_wifi_pairing_direct && route == Route::Pairing07))) return nullptr;
    if (kDeviceFirstSetup && route == Route::Pairing07) {
        return nullptr;
    }
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
// result: Original operation result and optional QR.
// Exception: The result retains every persistence, snapshot and radio failure.
void execute_command(const Command &command, Result &result)
{
    result.command = command;
    if (command.work == Work::LocalWifiScan || command.work == Work::LocalWifiConnect) {
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
        } else if (command.work == Work::LocalWifiScan) {
            result.error = LUNASHELL_wifi_start_scan();
        } else {
            result.error = LUNASHELL_wifi_connect_selected(s_local_wifi_credentials.ssid,
                s_local_wifi_credentials.password, true, &s_local_wifi_credentials.selection);
        }
        if (command.work == Work::LocalWifiConnect) wipe(&s_local_wifi_credentials, sizeof(s_local_wifi_credentials));
        return;
    }
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    if (command.work == Work::CheckRelay) {
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.relay.error = ESP_ERR_INVALID_STATE;
        } else {
            result.relay = check_relay_liveness();
        }
        result.error = result.relay.error;
        if (result.error != ESP_OK) {
            ESP_LOGE(kTag, "Relay check failed: stage=%u http=%d (%s)",
                static_cast<unsigned>(result.relay.stage), result.relay.http_status,
                esp_err_to_name(result.error));
        }
        return;
    }
#endif
    if (command.work == Work::UseCompanion) {
        // An explicit local choice uses the same internal-stack persistence owner
        // as commissioning; connectivity is not a requirement for a companion.
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "Local companion choice retired before persistence (%s)", esp_err_to_name(result.error));
            return;
        }
        result.error = persist_first_boot(false);
        return;
    }
    if (command.work == Work::CompleteFirstBoot) {
        auto &observation = s_action_connectivity;
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
        bool changed = false;
        if (command.preference == PreferenceId::QuietEnabled || command.preference == PreferenceId::QuietStart ||
            command.preference == PreferenceId::QuietEnd) {
            LUNASHELL::ClockSnapshot clock{};
            const auto read_error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
            if (read_error == ESP_OK && clock.preferences_valid) {
                const uint32_t previous = command.preference == PreferenceId::QuietEnabled ? clock.quiet_hours_enabled :
                    command.preference == PreferenceId::QuietStart ? clock.quiet_start_minute : clock.quiet_end_minute;
                changed = previous != command.preference_value;
            }
        } else {
            const auto read_error = LUNASHELL::DeviceSnapshotService::instance()
                .readCodyOptionsObservation(s_preference_before_write);
            changed = read_error == ESP_OK && preference_would_change(command.preference,
                command.preference_value, s_preference_before_write);
        }
        result.error = persist_companion_preference(command.preference, command.preference_value);
        result.preference_changed = result.error == ESP_OK && changed;
        return;
    }
    if (command.work == Work::ScheduleBirthIntro) {
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "Birth intro request retired before persistence (%s)", esp_err_to_name(result.error));
            return;
        }
        result.error = persist_birth_intro_pending(true);
        return;
    }
    if (command.work == Work::SaveWidgetVisibility) {
        if (command.generation != s_presentation_generation.load(std::memory_order_acquire)) {
            result.error = ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "Widget visibility write retired before persistence (%s)", esp_err_to_name(result.error));
            return;
        }
        result.error = persist_widget_visibility(command.widget_visibility);
        return;
    }
    if (command.work == Work::InstallQr || command.work == Work::InstallQrApple) {
        result.error = encode_qr(command.work == Work::InstallQrApple ? kAppleInstallUrl : kInstallUrl, result.qr);
        return;
    }
    if (command.work == Work::SessionQr) {
        auto &observation = s_action_connectivity;
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

// No parameters: The admitted worker publishes the single command receipt.
void complete_action()
{
    // The worker owns the mailbox until ready is published. The consumer
    // copies and clears the result before releasing the next command.
    Result &completed = *new (&s_result) Result{};
    execute_command(s_command, completed);
    if (completed.error != ESP_OK) {
        ESP_LOGE(kTag, "Action %u attempt=%lu session=%lu failed (%s)",
            to_index(completed.command.work), static_cast<unsigned long>(completed.command.attempt),
            static_cast<unsigned long>(completed.command.session), esp_err_to_name(completed.error));
    }
    s_result_ready.store(true, std::memory_order_release);
}

#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
// Unnamed context: No borrowed UI pointers cross to the relay worker.
void relay_worker(void *)
{
    // Task creation follows mailbox publication. Acquire before reading it.
    if (s_work_busy.load(std::memory_order_acquire)) {
        complete_action();
    }
    vTaskDeleteWithCaps(nullptr);
}
#endif

// context: Unused; the persistent worker owns radio, QR and preference actions.
void action_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // The acquire pairs with enqueue's release, before reading its mailbox.
        if (!s_work_busy.load(std::memory_order_acquire)) {
            continue;
        }
        complete_action();
    }
}

// work: Explicit radio, Codex-link or QR preparation action.
// attempt: Current canonical attempt identity; never an inferred UI list position.
// session: Current canonical provisioning-session identity, or zero for other actions.
// Exception: Rejects a busy mailbox or a relay worker that could not be allocated.
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
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    if (work == Work::CheckRelay) {
        const auto created = xTaskCreateWithCaps(relay_worker, "CODY_relay", kRelayStackBytes,
            nullptr, 3, nullptr, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (created != pdPASS) {
            s_work_busy.store(false, std::memory_order_release);
            s_command_pending = false;
            ESP_LOGE(kTag, "Relay worker allocation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    }
#endif
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// preference: Stable identity of the actual row pressed, never a row index.
// value: Exact user-selected target, also retained for an explicit Retry.
// Exception: Returns mailbox admission failure without starting a write.
esp_err_t enqueue_preference(PreferenceId preference, uint32_t value)
{
    s_retry_birth_intro = false;
    s_requested_preference = preference;
    s_requested_preference_value = value;
    if (s_worker == nullptr || s_work_busy.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Preference mailbox unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_command = {};
    s_command.work = Work::SavePreference;
    s_command.generation = s_generation;
    s_command.preference = preference;
    s_command.preference_value = value;
    s_command_pending = true;
    ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// scene: Active selector whose previous selection stays visible during the write.
// id: Flash-owned catalog ID from a real tap; copied into bounded request storage.
// Exception: Returns the real admission, preparation or snapshot publication error.
// text: Bounded immutable English text, retained through frame retirement.
// top: Logical caption baseline container, entirely inside the shared content chord.
// line: Receives the prepared flash-font layout.
// max_width: Admitted caption width for this route; ordinary setup keeps its narrow slot.
// font: Canonical body or headline font for the selected caption.
// Exception: Returns ESP_ERR_INVALID_SIZE for empty, unsupported or out-of-canvas text.
esp_err_t prepare_caption(const char *text, uint16_t top, CODY2D::TextLine &line,
                          uint16_t max_width = kCaptionWidth, const CODY2D::MonoFont &font = SceneData::body_font)
{
    const size_t length = std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length > 31 || width > max_width || top + font.height > 466) {
        ESP_LOGE(kTag, "Caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)}, Theme::text_bright, line);
}

// wifi: Canonical snapshot already read by the route caller.
// top: First name row; a full 32-byte printable SSID uses two rows.
// first_line: First available narrative slot, with two reserved backing buffers.
// Exception: Returns a caption error if the available rows cannot hold the name.
esp_err_t prepare_network_name(const LUNASHELL::WifiSnapshot &wifi, uint16_t top, uint8_t first_line)
{
    const char *name = wifi.ssid;
    const auto *end = std::find(name, name + sizeof(wifi.ssid), '\0');
    const bool printable = end != name + sizeof(wifi.ssid) &&
        std::all_of(name, end, [](char ch) { return ch >= 32 && ch <= 126; });
    // The saved profile is configuration, independent of a failed association or IP read.
    if (wifi.data_stale || (!wifi.data_valid && !wifi.credentials_configured)) {
        name = wifi.data_stale ? "Network name stale" : "Network unavailable";
    } else if (!wifi.credentials_configured) {
        name = "No configured Wi-Fi";
    } else if (!printable) {
        name = "Name cannot be displayed";
    } else if (end == name) {
        name = "Network name unavailable";
    }
    const auto length = std::strlen(name);
    // TextLine borrows route storage, never the mutable canonical-read scratch.
    constexpr size_t columns = 26;
    const auto row_count = (length + columns - 1U) / columns;
    if (first_line + row_count > kContentLineCapacity) { return ESP_ERR_INVALID_SIZE; }
    for (size_t row = 0; row < row_count; ++row) {
        const auto count = std::min(columns, length - row * columns);
        auto *text = s_view.debug_text[first_line + row];
        std::memcpy(text, name + row * columns, count);
        text[count] = '\0';
        const auto error = prepare_caption(text, top + row * 26U,
            s_view.lines[first_line + row], 300);
        if (error != ESP_OK) { return error; }
        s_view.lines[first_line + row].color = kPairingTitle;
    }
    s_view.line_count = first_line + row_count;
    return ESP_OK;
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
    if (width > kCaptionWidth || kCodexPairingCodeTop + font.height > 393U) {
        ESP_LOGE(kTag, "CCP display code geometry rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {s_codex_code, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(kCodexPairingCodeTop)}, Theme::accent_active, line);
}

// code_ready: True only after the pairing owner supplied the display code.
// Exception: Returns the first native glyph or caption preparation error.
esp_err_t prepare_device_pairing_content(bool code_ready)
{
    if (!code_ready) {
        s_view.line_count = 1;
        return prepare_caption("Creating pairing code", kPcWaitCaptionTop, s_view.lines[0]);
    }
    s_view.header_icon = CODY2D::ActionIcon::Link;
    s_view.header_icon_bounds = kCodexPairingGlyph;
    s_view.instant_lines[0] = true;
    s_view.line_count = 3;
    auto error = prepare_codex_code(s_view.lines[0]);
    if (error == ESP_OK) {
        error = prepare_caption("Enter this code", 292, s_view.lines[1]);
    }
    if (error == ESP_OK) {
        error = prepare_caption("in the Cody plugin", 320, s_view.lines[2]);
        s_view.lines[2].color = kPairingCaption;
    }
    return error;
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
    return (kDeviceFirstSetup && s_route == Route::Hello04) ||
        s_route == Route::Invitation05 || s_route == Route::Install06 ||
        s_route == Route::Pairing07 || s_route == Route::Failed08 || s_route == Route::Online09 ||
        s_route == Route::MakeItYours10 ||
        s_route == Route::ConnectCodex12 || s_route == Route::SetupComplete13 || s_route == Route::LookingForPc14 ||
        s_route == Route::CodexChoice || s_route == Route::SetupMenu || s_route == Route::NetworkWait ||
        s_route == Route::CodexReplace || s_route == Route::CodexForget || s_route == Route::CodexForgotten ||
        is_codex_event_route(s_route);
}

// No parameters: QR and Options pages retain clearance above their compact action.
CODY2D::Rect single_action_bounds()
{
    if (s_route == Route::MakeItYours10) {
        return kOptionsFooterBounds;
    }
    return s_route == Route::Install06 || s_route == Route::Pairing07 ||
        (s_route == Route::ConnectCodex12 && !s_codex_error_visible) ?
        kFooterBounds : kSingleActionBounds;
}

// No parameters: a confirmed reboot or reset owns the route until the power owner finishes.
bool maintenance_pending()
{
    return s_options_page == OptionsPage::RestartPending || s_options_page == OptionsPage::ResetPending;
}

// No parameters: Back returns from a category or leaves the root options page.
bool options_back_available()
{
    return s_route == Route::MakeItYours10 && !maintenance_pending() &&
        (kDeviceFirstSetup || s_options_page != OptionsPage::Menu || s_options_from_setup);
}

// No parameters: all menu pages use static navigation rows and the same exit action.
bool on_options_menu()
{
    return s_options_page == OptionsPage::Menu || s_options_page == OptionsPage::MenuMore ||
        s_options_page == OptionsPage::MenuMaintenance;
}

// No parameters: keep maintenance pages independent of the preference reader.
bool on_maintenance_page()
{
    return s_route == Route::MakeItYours10 && (maintenance_pending() ||
        s_options_page == OptionsPage::RestartConfirm || s_options_page == OptionsPage::ResetConfirm ||
        s_options_page == OptionsPage::ResetFailed);
}

// No parameters: guide pages are static Options content and never read preferences.
bool on_quick_guide()
{
    return s_route == Route::MakeItYours10 && (s_options_page == OptionsPage::GuideButtons ||
        s_options_page == OptionsPage::GuideTouch || s_options_page == OptionsPage::GuideOffline);
}

// No parameters: these pages use a right swipe to reach the preceding peer page.
bool options_previous_page_by_swipe()
{
    return s_route == Route::MakeItYours10 &&
        (s_options_page == OptionsPage::MenuMore || s_options_page == OptionsPage::MenuMaintenance ||
         s_options_page == OptionsPage::GuideTouch || s_options_page == OptionsPage::GuideOffline ||
         s_options_page == OptionsPage::DisplayRings || s_options_page == OptionsPage::Time ||
         s_options_page == OptionsPage::BehaviorMoreSettings);
}

// action: Footer identity that requires a complete, released Options gesture.
bool released_options_confirmation(OnboardingAction action)
{
    return action == OnboardingAction::Retry || action == OnboardingAction::ConfirmFactoryReset ||
        action == OnboardingAction::ConfirmRestart || action == OnboardingAction::ScheduleBirthIntro;
}

// No parameters: Options and guide paging are independent of their footer actions.
bool options_next_page_available()
{
    return s_route == Route::MakeItYours10 && s_preference_error == ESP_OK &&
        (s_options_page == OptionsPage::Menu || (kDeviceFirstSetup && s_options_page == OptionsPage::MenuMore) ||
         s_options_page == OptionsPage::GuideButtons || s_options_page == OptionsPage::GuideTouch ||
         (kDeviceFirstSetup && (s_options_page == OptionsPage::Display || s_options_page == OptionsPage::DisplayRings ||
                               s_options_page == OptionsPage::BehaviorMore)));
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
        label = s_home_wifi_flow ? "Download app" : kDeviceFirstSetup ? "App download QR" : "Show QR";
        break;
    case OnboardingAction::OpenPairingGuide:
        label = "Tap to pair";
        break;
    case OnboardingAction::PairCody:
        label = s_home_wifi_flow && s_route == Route::Invitation05 ? wifi_menu_art::kPairText :
            kDeviceFirstSetup ? "Show pairing QR" : "Pair Cody";
        break;
    case OnboardingAction::AndroidApp:
        label = "Android";
        break;
    case OnboardingAction::AppleApp:
        label = "Apple";
        break;
    case OnboardingAction::LocalWifi:
        label = wifi_menu_art::kWifiText;
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
    case OnboardingAction::DisplayOptions:
        label = "Display";
        break;
    case OnboardingAction::BehaviorOptions:
        label = "Behavior";
        break;
    case OnboardingAction::MoreBehavior:
    case OnboardingAction::MoreBehaviorSettings:
    case OnboardingAction::MoreDisplay:
        label = "More...";
        break;
    case OnboardingAction::QuietHours: label = "Quiet hours"; break;
    case OnboardingAction::QuietToggle: label = "Quiet hours"; break;
    case OnboardingAction::QuietStart: label = "Start"; break;
    case OnboardingAction::QuietEnd: label = "End"; break;
    case OnboardingAction::QuietHour: label = "Hour -"; break;
    case OnboardingAction::QuietMinute: label = "Minute -"; break;
    case OnboardingAction::QuietSave: label = "Save"; break;
    case OnboardingAction::OpenTime:
        label = "Time";
        break;
    case OnboardingAction::SyncPcTime:
        label = "Sync time";
        break;
    case OnboardingAction::ScheduleBirthIntro:
        label = s_birth_intro_scheduled ? "Next restart" : "Replay intro";
        break;
    case OnboardingAction::SynthOptions:
        label = "Synth Voice";
        break;
    case OnboardingAction::CodexStatistics:
        label = "Codex stats";
        break;
    case OnboardingAction::BrowseCodex:
        label = "Codex";
        break;
    case OnboardingAction::LocalTimer:
        label = "Timer";
        break;
    case OnboardingAction::AudioLevels:
        label = "Audio";
        break;
    case OnboardingAction::QuickGuide:
        label = "Quick guide";
        break;
    case OnboardingAction::OpenAbout:
        label = "About";
        break;
    case OnboardingAction::VisibleScreens:
        label = "Visible screens";
        break;
    case OnboardingAction::Connections:
        label = "Connections";
        break;
    case OnboardingAction::Done:
        label = "OK";
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
    case OnboardingAction::CheckRelay:
        label = "Check relay";
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
    case OnboardingAction::FactoryReset:
        label = "Factory reset";
        break;
    case OnboardingAction::RestartDevice:
    case OnboardingAction::ConfirmRestart:
        label = "Restart device";
        break;
    case OnboardingAction::ConfirmFactoryReset:
        label = "Reset device";
        break;
    case OnboardingAction::OpenEvent: label = "Open"; break;
    case OnboardingAction::ViewFailures: label = "View failures"; break;
    case OnboardingAction::SendReply: label = "Send reply"; break;
    case OnboardingAction::DismissEvent: label = "OK"; break;
    case OnboardingAction::RetryEvent: label = "Try again"; break;
    default:
        ESP_LOGE(kTag, "Unknown navigation action %u (%s)", to_index(action), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// action: Typed semantic purpose, independent of caption and storage order.
// control: Receives native dark gradients, border and the default iconless style.
void style_action(OnboardingAction action, CODY2D::CapsuleControl &control)
{
    control.top_color = Theme::controls_primary_top;
    control.bottom_color = Theme::controls_primary_bottom;
    control.border_color = Theme::controls_primary_border;
    control.icon_color = Theme::controls_primary_icon;
    control.icon = CODY2D::ActionIcon::None;
    switch (action) {
    case OnboardingAction::AcceptCodex:
    case OnboardingAction::KeepPairing:
    case OnboardingAction::KeepSaved:
    case OnboardingAction::Done:
    case OnboardingAction::DismissEvent:
        break;
    case OnboardingAction::ConnectCodex:
    case OnboardingAction::PairCody:
    case OnboardingAction::SendReply:
        break;
    case OnboardingAction::OpenPairingGuide:
        if (kDeviceFirstSetup) { break; }
        control.top_color = Theme::controls_setup_highlight_top;
        control.bottom_color = Theme::controls_setup_highlight_bottom;
        control.border_color = Theme::controls_setup_highlight_border;
        break;
    case OnboardingAction::NetworkSetup:
    case OnboardingAction::ShowQr:
        if (kDeviceFirstSetup) { break; }
        control.top_color = Theme::controls_tertiary_top;
        control.bottom_color = Theme::controls_tertiary_bottom;
        control.border_color = Theme::controls_tertiary_border;
        control.icon_color = Theme::controls_tertiary_icon;
        break;
    case OnboardingAction::DeclineCodex:
    case OnboardingAction::Later:
    case OnboardingAction::Options:
    case OnboardingAction::MoreBehavior:
    case OnboardingAction::MoreDisplay:
    case OnboardingAction::SynthOptions:
    case OnboardingAction::CheckRelay:
        control.top_color = Theme::controls_secondary_top;
        control.bottom_color = Theme::controls_secondary_bottom;
        control.border_color = action == OnboardingAction::MoreBehavior || action == OnboardingAction::MoreDisplay || action == OnboardingAction::SynthOptions
            ? 0 : Theme::controls_secondary_border;
        control.icon_color = Theme::controls_secondary_icon;
        break;
    case OnboardingAction::Cancel:
    case OnboardingAction::Retry:
    case OnboardingAction::RetryEvent:
    case OnboardingAction::ViewFailures:
    case OnboardingAction::ReplacePairing:
        control.top_color = Theme::controls_attention_top;
        control.bottom_color = Theme::controls_attention_bottom;
        control.border_color = Theme::controls_attention_border;
        control.icon_color = Theme::controls_attention_icon;
        break;
    case OnboardingAction::ForgetPairing:
    case OnboardingAction::ConfirmForget:
    case OnboardingAction::ConfirmFactoryReset:
        control.top_color = Theme::controls_destructive_top;
        control.bottom_color = Theme::controls_destructive_bottom;
        control.border_color = Theme::controls_destructive_border;
        control.icon_color = Theme::controls_destructive_icon;
        break;
    default:
        break;
    }
}

// action: Validated stable action identity.
// label: Printable primary caption with an optional split at its first space.
// bounds: Shared logical raster and hit rectangle.
// enabled: Backend admission state.
// card: True for the two-column rounded card layout.
// control: Receives the rounded gradient button and first caption line.
// second: Receives a second line only for a two-word card caption.
// icon: Optional left-aligned glyph for a one-line Setup row.
// Exception: Returns the real caption layout failure.
esp_err_t prepare_button(OnboardingAction action, const char *label, CODY2D::Rect bounds,
                         bool enabled, bool card, CODY2D::CapsuleControl &control,
                         CODY2D::TextLine &second, CODY2D::ActionIcon icon = CODY2D::ActionIcon::None)
{
    control = {bounds, {}, 0, 0, enabled};
    second = {};
    style_action(action, control);
    control.corner_radius = card ? 28 : 0;
    if (label == nullptr) {
        control.icon = CODY2D::ActionIcon::None;
        return ESP_OK;
    }
    const bool setup_row = icon != CODY2D::ActionIcon::None;
    if (setup_row) {
        control.corner_radius = 16;
        control.icon = icon;
        control.icon_center = {static_cast<int16_t>(bounds.x + 28U),
            static_cast<int16_t>(bounds.y + bounds.height / 2U)};
    }
    const auto &font = SceneData::body_font;
    const char *space = card ? std::strchr(label, ' ') : nullptr;
    if (space == label || (space != nullptr && space[1] == '\0')) {
        space = nullptr;
    }
    const uint16_t length = static_cast<uint16_t>(space != nullptr ? space - label : std::strlen(label));
    const uint16_t caption_inset = setup_row ? 56U : 10U;
    const uint16_t reserved_width = caption_inset + 10U;
    const uint16_t width = (length - 1U) * font.advance + font.width;
    if (length == 0 || width + reserved_width > bounds.width || font.height > bounds.height) {
        ESP_LOGE(kTag, "Action caption does not fit: %s (%s)", label, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t caption_height = space != nullptr ? font.height * 2U + 2U : font.height;
    if (caption_height > bounds.height) { return ESP_ERR_INVALID_SIZE; }
    const uint16_t top = bounds.y + (bounds.height - caption_height) / 2U;
    const uint16_t caption_left = bounds.x + (setup_row ? caption_inset : (bounds.width - width) / 2U);
    esp_err_t error = CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>(caption_left), static_cast<int16_t>(top)},
        enabled ? Theme::text_bright : Theme::text_muted, control.caption);
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
             static_cast<int16_t>(top + font.height + 2U)}, enabled ? Theme::text_bright : Theme::text_muted, second);
    }
    return error;
}

// action: Codex pairing or Wi-Fi setup identity.
// enabled: True when the corresponding connection action is available.
// control: Receives the glyph, first caption line and full touch rectangle.
// second: Receives the second centered caption line.
// Exception: Returns the original caption error or rejects an unknown action.
esp_err_t prepare_connection_action(OnboardingAction action, bool enabled,
    CODY2D::CapsuleControl &control, CODY2D::TextLine &second)
{
    const bool codex = action == OnboardingAction::ConnectCodex;
    if (!codex && action != OnboardingAction::NetworkSetup) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *label = codex ? "Codex" : "Wi-Fi";
    const char *hint = codex ? "PC link" : "Join network";
    const auto bounds = codex ? kSetupCodexBounds : kSetupWifiBounds;
    const auto center_x = static_cast<int16_t>(bounds.x + bounds.width / 2);
    control = {};
    control.bounds = bounds;
    control.enabled = enabled;
    control.icon = codex ? CODY2D::ActionIcon::Link : CODY2D::ActionIcon::Wifi;
    control.icon_color = enabled ? (codex ? Theme::primary_muted : kWifiReadyMint) : kPairingCaption;
    control.icon_center = {center_x, kConnectionIconY};

    const auto &font = SceneData::body_font;
    const uint16_t color = enabled ? kPairingTitle : kPairingCaption;
    // caption: Complete caption for this column.
    // top: Logical row inside the touch area.
    // caption_color: Main caption or subordinate hint color.
    // line: Receives the centered text.
    // Exception: Preserves the native layout error or rejects a wide caption.
    const auto prepare_line = [&](const char *caption, int16_t top, uint16_t caption_color, CODY2D::TextLine &line) {
        const CODY2D::TextSpan text{caption, static_cast<uint16_t>(std::strlen(caption))};
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(font, text, width);
        if (error != ESP_OK) {
            return error;
        }
        if (width == 0 || width > bounds.width) {
            return ESP_ERR_INVALID_SIZE;
        }
        return CODY2D::prepare_text_line(font, text,
            {static_cast<int16_t>(center_x - static_cast<int16_t>(width / 2)), top}, caption_color, line);
    };
    const auto error = prepare_line(label, 264, color, control.caption);
    return error == ESP_OK ? prepare_line(hint, 296, kPairingCaption, second) : error;
}

#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
// No parameters: Uses the receipt and admission state owned by Connections.
// Exception: Returns the original caption or action layout error.
esp_err_t prepare_relay_footer()
{
    const char *status = "Choose what to connect";
    uint16_t color = kPairingCaption;
    if (s_command_pending) {
        status = "Checking relay...";
    } else if (s_relay_attempted) {
        if (s_relay_result.error == ESP_OK) {
            status = "Relay reachable";
            color = kWifiReadyMint;
        } else {
            color = kOfflineAmber;
            switch (s_relay_result.stage) {
            case RelayProbeStage::Start: status = "Check could not start"; break;
            case RelayProbeStage::Wifi: status = "Wi-Fi not ready"; break;
            case RelayProbeStage::Clock: status = "Device time unavailable"; break;
            case RelayProbeStage::Https:
                status = s_relay_result.error == ESP_ERR_TIMEOUT ? "HTTPS timed out" : "HTTPS check failed";
                break;
            case RelayProbeStage::Http:
                std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Relay HTTP %d",
                    s_relay_result.http_status);
                status = s_view.error_text;
                break;
            case RelayProbeStage::Response: status = "Unexpected relay response"; break;
            case RelayProbeStage::WebSocketUpgrade: status = "WebSocket upgrade failed"; break;
            case RelayProbeStage::WebSocketFrame:
                status = s_relay_result.error == ESP_ERR_TIMEOUT ? "WebSocket timed out" : "WebSocket check failed";
                break;
            }
        }
    }
    const auto error = prepare_caption(status, kConnectionsHintTop, s_view.lines[0], 300);
    if (error != ESP_OK) { return error; }
    s_view.lines[0].color = color;
    s_view.line_count = 1;
    s_view.instant_lines[0] = s_relay_attempted;
    s_view.action.enabled = !s_command_pending;
    s_view.alternative.enabled = !s_command_pending && kCodexIntegrationEnabled;
    s_view.options_id = OnboardingAction::CheckRelay;
    CODY2D::TextLine unused;
    return prepare_button(s_view.options_id, "Check relay", kRelayCheckBounds,
        !s_command_pending, false, s_view.options, unused);
}
#endif

// action: Semantic pairing command or Options home navigation.
// label: Prepared single-line English caption.
// bounds: Capsule rectangle used for both drawing and touch.
// enabled: Backend admission or navigation readiness.
// icon: Glyph paired with the caption.
// control: Receives the complete capsule with a centered glyph and caption.
// second: Cleared unused second line.
// Exception: Returns the real caption-layout error.
esp_err_t prepare_icon_pill(OnboardingAction action, const char *label, CODY2D::Rect bounds,
    bool enabled, CODY2D::ActionIcon icon, CODY2D::CapsuleControl &control, CODY2D::TextLine &second)
{
    const auto error = prepare_button(action, label, bounds, enabled, false, control, second);
    if (error != ESP_OK) {
        return error;
    }
    const bool secondary = action == OnboardingAction::Later;
    control.icon = icon;
    const auto x = control.caption.origin.x;
    const auto y = static_cast<int16_t>(bounds.y + bounds.height / 2U);
    // Shift the centered caption half the icon-and-gap width so the pair stays centered.
    constexpr int16_t half_glyph_and_gap = 17;
    const auto caption_x = static_cast<int16_t>(x + (secondary ? -half_glyph_and_gap : half_glyph_and_gap));
    control.icon_center = {static_cast<int16_t>(secondary ? x + control.caption.bounds.width + 5 : x - 5), y};
    return CODY2D::prepare_text_line(SceneData::body_font, control.caption.text,
        {caption_x, control.caption.origin.y}, control.caption.color, control.caption);
}

// action: Keep, replace or forget the retained PC association.
// enabled: Canonical admission for the selected operation.
// control: Receives a standalone icon, caption and complete touch bounds.
// second: Cleared because these short captions occupy one line.
// Exception: Rejects an unknown identity or returns the native caption error.
esp_err_t prepare_saved_pc_action(OnboardingAction action, bool enabled,
    CODY2D::CapsuleControl &control, CODY2D::TextLine &second)
{
    using Icon = CODY2D::ActionIcon;
    control = {};
    second = {};
    const char *label = nullptr;
    switch (action) {
    case OnboardingAction::KeepPairing:
        control.bounds = kKeepPcBounds;
        control.icon = Icon::Check;
        control.icon_color = Theme::secondary_default;
        label = "Keep";
        break;
    case OnboardingAction::ReplacePairing:
        control.bounds = kPairPcBounds;
        control.icon = Icon::Retry;
        control.icon_color = Theme::primary_muted;
        label = "Re-pair";
        break;
    case OnboardingAction::ForgetPairing:
        control.bounds = kForgetPcBounds;
        control.icon = Icon::Close;
        control.icon_color = Theme::accent_error_icon;
        label = "Forget";
        break;
    default: return ESP_ERR_INVALID_ARG;
    }
    control.enabled = enabled;
    const auto center = static_cast<int16_t>(control.bounds.x + control.bounds.width / 2U);
    control.icon_center = {center, 329};
    const auto length = static_cast<uint16_t>(std::strlen(label));
    const auto &font = SceneData::body_font;
    const uint16_t width = (length - 1U) * font.advance + font.width;
    return CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>(center - width / 2U), 361},
        enabled ? kPairingTitle : kPairingCaption, control.caption);
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
    if (kDeviceFirstSetup && s_route == Route::CodexReplace) {
        return prepare_saved_pc_action(action, enabled, s_view.action, s_view.action_second_line);
    }
    if (s_home_wifi_flow && s_route == Route::Invitation05) {
        return prepare_button(action, label, kHomePairButton, enabled, false,
            s_view.action, s_view.action_second_line);
    }
    if (kDeviceFirstSetup) {
        using Icon = CODY2D::ActionIcon;
        if (s_route == Route::NetworkWait && action == OnboardingAction::Later) {
            return prepare_icon_pill(action, label, kOfflineLaterBounds, enabled, Icon::ChevronRight,
                s_view.action, s_view.action_second_line);
        }
        if (s_home_wifi_flow && s_route == Route::Install06) {
            return prepare_button(action, label, kAppleButton, enabled, false,
                s_view.action, s_view.action_second_line);
        }
        if (s_route == Route::Invitation05) {
            return prepare_icon_pill(action, label, kGuidePairBounds, enabled, Icon::Bluetooth,
                s_view.action, s_view.action_second_line);
        }
        if (s_route == Route::Hello04 || s_route == Route::Install06) {
            const auto icon = s_route == Route::Hello04 ? Icon::Wifi : Icon::Bluetooth;
            return prepare_icon_pill(action, label, single_action_bounds(), enabled, icon,
                s_view.action, s_view.action_second_line);
        }
    }
    if (s_route == Route::SetupMenu) {
        if (kDeviceFirstSetup) {
            return prepare_connection_action(action, enabled, s_view.action, s_view.action_second_line);
        }
        return prepare_button(action, label, kSetupWifiBounds, enabled, false,
            s_view.action, s_view.action_second_line, CODY2D::ActionIcon::Wifi);
    }
    const bool card = s_route == Route::CodexChoice ||
        s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget;
    const auto bounds = card ? kRightCardBounds : uses_footer_action() ? single_action_bounds() :
        action == OnboardingAction::Cancel ? kBackBounds : kForwardBounds;
    if (uses_footer_action()) {
        return prepare_button(action, label, bounds, enabled, card,
            s_view.action, s_view.action_second_line);
    }
    s_view.action = {bounds, {}, Theme::controls_secondary_top, Theme::controls_secondary_bottom, enabled};
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
    if (kDeviceFirstSetup && s_route == Route::CodexReplace) {
        return prepare_saved_pc_action(action, enabled, s_view.alternative, s_view.alternative_second_line);
    }
    if (s_home_wifi_flow && s_route == Route::Invitation05 && action == OnboardingAction::LocalWifi) {
        return prepare_button(action, label, kHomeLocalWifiButton, enabled, false,
            s_view.alternative, s_view.alternative_second_line);
    }
    if (s_home_wifi_flow && s_route == Route::Install06 && action == OnboardingAction::AndroidApp) {
        return prepare_button(action, label, kAndroidButton, enabled, false,
            s_view.alternative, s_view.alternative_second_line);
    }
    if (kDeviceFirstSetup && s_route == Route::Invitation05 && action == OnboardingAction::ShowQr) {
        return prepare_icon_pill(action, label, kGuideDownloadBounds, enabled, CODY2D::ActionIcon::Qr,
            s_view.alternative, s_view.alternative_second_line);
    }
    if (kDeviceFirstSetup && s_route == Route::NetworkWait && action == OnboardingAction::NetworkSetup) {
        return prepare_icon_pill(action, label, kOfflineSetupBounds, enabled, CODY2D::ActionIcon::Wifi,
            s_view.alternative, s_view.alternative_second_line);
    }
    if (s_route == Route::SetupMenu) {
        if (kDeviceFirstSetup) {
            return prepare_connection_action(action, enabled, s_view.alternative, s_view.alternative_second_line);
        }
        return prepare_button(action, label, kSetupCodexBounds, enabled, false,
            s_view.alternative, s_view.alternative_second_line, CODY2D::ActionIcon::Link);
    }
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
                CODY2D::rgb565_native_to_stored(dark ? Theme::qr_dark : Theme::qr_light, strip.byte_order);
        }
    }
    return ESP_OK;
}

#if defined(CODY_DEVICE_UI_ONLY)
// state: Immutable static project QR.
// strip: Caller-owned content tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original QR raster error.
esp_err_t draw_about_project_rows(const void *state, CODY2D::Surface strip,
                                 uint16_t first_row, uint16_t first_column)
{
    const auto &qr = *static_cast<const QrModules *>(state);
    return draw_qr_rows(qr, kAboutProjectBounds, strip, first_row, first_column);
}
#endif

// control: Prepared action geometry and glyph.
// icon_size: Standalone glyph size, or zero for a text-only action.
// strip: Caller-owned content tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the original native glyph or surface error.
esp_err_t draw_action_visual(const CODY2D::CapsuleControl &control, uint16_t icon_size,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (icon_size != 0) {
        const CODY2D::Rect icon_bounds{
            static_cast<uint16_t>(control.icon_center.x - icon_size / 2),
            static_cast<uint16_t>(control.icon_center.y - icon_size / 2),
            icon_size, icon_size,
        };
        const auto color = control.enabled ? control.icon_color : kPairingCaption;
        return CODY2D::draw_action_icon_rows(control.icon, icon_bounds, color,
            strip, first_row, first_column);
    }
    return UI::draw_action_icon(control, strip, first_row, first_column);
}

// line: Prepared caption with flash-owned glyphs.
// visible: Number of characters admitted by the current presentation.
// enabled: Whether the action accepts input.
// bounds: Existing action and complete touch bounds.
// strip: Borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves the original surface and text drawing errors.
esp_err_t draw_gradient_action_caption(const CODY2D::TextLine &line, uint16_t visible, bool enabled,
    CODY2D::Rect bounds, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    if (!line.valid || line.text.characters == nullptr || line.text.length == 0 || visible > line.text.length)
        return ESP_ERR_INVALID_ARG;
    for (uint16_t index = 0; index < line.text.length; ++index) {
        if (line.text.characters[index] != ' ' && gradient_pixel_glyph(line.text.characters[index]) == nullptr)
            return ESP_ERR_INVALID_ARG;
    }
    const uint32_t columns = 6U * line.text.length - 1U;
    const uint16_t scale = columns * 3U <= bounds.width ? 3 : 2;
    const uint32_t width = columns * scale;
    const uint16_t height = 7U * scale;
    if (width > bounds.width || height > bounds.height) return ESP_ERR_INVALID_SIZE;
    const int origin_x = bounds.x + (bounds.width - width) / 2U;
    const int origin_y = bounds.y + (bounds.height - height) / 2U;
    const int top = std::max<int>(first_row, origin_y);
    const int bottom = std::min<int>(first_row + strip.height, origin_y + height);
    for (int y = top; y < bottom; ++y) {
        const auto color = gradient_action_color(static_cast<uint16_t>(y - origin_y), height, enabled ? 255 : 128);
        const auto stored = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        auto *row = strip.pixels + static_cast<size_t>(y - first_row) * strip.stride;
        const uint8_t glyph_row = static_cast<uint8_t>((y - origin_y) / scale);
        for (uint16_t index = 0; index < visible; ++index) {
            const auto *glyph = gradient_pixel_glyph(line.text.characters[index]);
            if (glyph == nullptr) continue; // Space was admitted during validation.
            for (uint8_t column = 0; column < 5; ++column) {
                if (((*glyph)[glyph_row] & (1U << (4U - column))) == 0) continue;
                const int x = origin_x + (index * 6U + column) * scale;
                const int left = std::max<int>(first_column, x);
                const int right = std::min<int>(first_column + strip.width, x + scale);
                if (left < right) std::fill_n(row + left - first_column, right - left, stored);
            }
        }
    }
    return ESP_OK;
}

// preference: Prepared menu or setting row, sharing its existing touch rectangle.
// strip: Caller-owned content tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the first native glyph, divider or text error.
esp_err_t draw_preference_row(const PreferenceControl &preference, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto &control = preference.control;
    if (!kDeviceFirstSetup) {
        return UI::draw_text_action(control, strip, first_row, first_column);
    }
    const auto &bounds = control.bounds;
    // Menu destinations and setting values share the same flat icon-and-divider style.
    auto error = draw_action_visual(control, bounds.height < 40U ? 24 : 30, strip, first_row, first_column);
    if (error == ESP_OK) {
        error = CODY2D::draw_text_rows(control.caption, control.caption.text.length,
            strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        const CODY2D::CapsuleControl divider{
            {static_cast<uint16_t>(bounds.x + 48U), static_cast<uint16_t>(bounds.y + bounds.height - 2U),
             static_cast<uint16_t>(bounds.width - 48U), 2}, {}, Theme::chrome_row_separator,
             Theme::chrome_row_separator, true};
        error = CODY2D::draw_capsule_rows(divider, strip, first_row, first_column);
    }
    return error;
}

// view: Display-owned menu text that is retained through frame retirement.
// ble: Canonical boot-scoped identity, independent of current advertising state.
bool update_wifi_menu_name(ContentView &view, const LUNASHELL::BleSnapshot &ble)
{
    constexpr auto prefix_size = sizeof(wifi_menu_art::kNamePrefix) - 1U;
    constexpr auto length = wifi_menu_art::kNameLength;
    static_assert(length < sizeof(ble.device_name));
    const char *name = ble.device_name;
    const bool valid = name[length] == '\0' &&
        std::memcmp(name, wifi_menu_art::kNamePrefix, prefix_size) == 0 &&
        std::all_of(name + prefix_size, name + length, [](char value) {
            return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F');
        });
    if (!valid) {
        const bool changed = view.wifi_menu_name[0] != '\0';
        view.wifi_menu_name[0] = '\0';
        return changed;
    }
    if (std::memcmp(view.wifi_menu_name, name, length + 1U) == 0) return false;
    std::memcpy(view.wifi_menu_name, name, length + 1U);
    return true;
}

// view: Prepared menu with its canonical name and two complete touch targets.
// strip: Valid content-clipped RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the original avatar or icon drawing error.
esp_err_t draw_wifi_menu(const ContentView &view, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    using namespace wifi_menu_art;
    draw_identity(view.wifi_menu_name, strip, first_row, first_column);
    auto error = draw_pixel_avatar_rows(s_wifi_mascot, kHomeMenuMascot, strip, first_row, first_column);
    if (error == ESP_OK && view.actions_visible) {
        error = draw_choice(kPair, CODY2D::ActionIcon::Bluetooth, view.action.bounds,
            strip, first_row, first_column);
    }
    if (error == ESP_OK && view.actions_visible) {
        error = draw_choice(kWifi, CODY2D::ActionIcon::Wifi, view.alternative.bounds,
            strip, first_row, first_column);
    }
    return error;
}

// strip: Borrowed tile; coordinates and RGB565 byte order remain native to CODY2D.
// first_row: Logical y coordinate of this tile.
// first_column: Logical x coordinate of this tile.
// Exception: Returns the original raster failure.
esp_err_t draw_platform_choices(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    static constexpr const char *apple[] = {
        "......##....", ".....##.....", "............", "...###.###..", ".##########.",
        "#########...", "########....", "#########...", ".##########.", ".##########.",
        "..########..", "...###.##..."};
    static constexpr const char *android[] = {
        "..#......#..", "...#....#...", "...######...", "..########..", "..##.##.##..",
        "..########..", "............", "#.########.#", "#.########.#", "#.########.#",
        "..########..", "...##..##..."};
    static constexpr uint16_t rainbow[] = {Theme::branding_rainbow_0, Theme::branding_rainbow_1,
        Theme::branding_rainbow_2, Theme::branding_rainbow_3, Theme::branding_rainbow_4, Theme::branding_rainbow_5};
    for (int logo = 0; logo < 2; ++logo) {
        const auto &pixels = logo == 0 ? apple : android;
        const int left = logo == 0 ? 143 : 275;
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
            if (pixels[y][x] != '#') continue;
            const int px = left + x * 4, py = 316 + y * 4;
            const int x0 = std::max<int>(px, first_column), x1 = std::min<int>(px + 4, first_column + strip.width);
            const int y0 = std::max<int>(py, first_row), y1 = std::min<int>(py + 4, first_row + strip.height);
            if (x0 >= x1 || y0 >= y1) continue;
            const auto error = CODY2D::fill_rect(strip, {static_cast<uint16_t>(x0 - first_column),
                static_cast<uint16_t>(y0 - first_row), static_cast<uint16_t>(x1 - x0),
                static_cast<uint16_t>(y1 - y0)}, logo == 0 ? rainbow[y / 2] : Theme::branding_rainbow_0);
            if (error != ESP_OK) return error;
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
    esp_err_t error = view.wifi_menu ? draw_wifi_menu(view, strip, first_row, first_column) :
        draw_qr_rows(view.qr, view.qr_bounds, strip, first_row, first_column);
    if (view.qr.side != 0) return error;
    if (error == ESP_OK && view.platform_choices && view.actions_visible) {
        error = draw_platform_choices(strip, first_row, first_column);
    }
    const bool offline = view.header_icon == CODY2D::ActionIcon::WifiOff;
    const auto &header = offline ? kOfflineDisc : view.header_icon_bounds;
    if (error == ESP_OK && view.header_icon != CODY2D::ActionIcon::None &&
        first_row < header.y + header.height && first_row + strip.height > header.y) {
        if (offline) {
            const CODY2D::CapsuleControl disc{kOfflineDisc, {}, kOfflineDiscColor, kOfflineDiscColor, true};
            error = CODY2D::draw_capsule_rows(disc, strip, first_row, first_column);
        }
        if (error == ESP_OK) {
            const uint16_t color = view.header_icon_color != 0 ? view.header_icon_color :
                offline ? kOfflineAmber : kPairingAccent;
            error = CODY2D::draw_action_icon_rows(view.header_icon, view.header_icon_bounds,
                color, strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && view.approval_notice != nullptr && view.approval_notice_visible) {
        error = draw_approval_notice_rows(*view.approval_notice, strip, first_row, first_column);
    }
    for (uint8_t index = 0; error == ESP_OK && index < view.line_count; ++index) {
        if (index == 0 && view.chat_waiting && view.visible_characters[index] != 0) {
            error = event_presentation::draw_chat_waiting_dot(view.lines[index], strip, first_row, first_column);
        }
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.lines[index], view.visible_characters[index],
                strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && !view.wifi_menu && !view.platform_choices && view.actions_visible && view.action_visible &&
        (view.action.caption.valid || view.action.icon != CODY2D::ActionIcon::None) &&
        !(s_home_wifi_flow && s_route == Route::Pairing07)) {
        // Reveal the complete action atomically after the main line finishes.
        const bool settings_footer = s_route == Route::MakeItYours10 && view.action.caption.valid;
        if (settings_footer) {
            error = draw_gradient_action_caption(view.action.caption, view.action_visible_characters,
                view.action.enabled, view.action.bounds, strip, first_row, first_column);
        } else {
            error = draw_action_visual(view.action, view.action_icon_size, strip, first_row, first_column);
            if (error == ESP_OK && view.action.caption.valid) {
                error = CODY2D::draw_text_rows(view.action.caption, view.action_visible_characters,
                    strip, first_row, first_column);
            }
        }
        if (error == ESP_OK && view.action_second_line.valid) {
            error = CODY2D::draw_text_rows(view.action_second_line, view.action_second_visible,
                strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && !view.wifi_menu && !view.platform_choices && view.actions_visible && view.alternative_id != OnboardingAction::None) {
        error = draw_action_visual(view.alternative, view.action_icon_size, strip, first_row, first_column);
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
        const auto icon_size = view.options_id == OnboardingAction::CheckRelay ? 0 : view.action_icon_size;
        error = draw_action_visual(view.options, icon_size, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(view.options.caption, view.options_visible_characters,
                strip, first_row, first_column);
        }
    }
    for (uint8_t index = 0; error == ESP_OK && view.actions_visible && index < view.preference_count; ++index) {
        const auto &preference = view.preferences[index];
        error = draw_preference_row(preference, strip, first_row, first_column);
        if (error == ESP_OK && preference.value.valid) {
            error = CODY2D::draw_text_rows(preference.value, preference.value.text.length,
                strip, first_row, first_column);
        }
    }
    if (error == ESP_OK && view.actions_visible && view.menu_page != 0)
        error = draw_page_marks(view.menu_page - 1U, view.menu_page_count, strip, first_row, first_column);
    if (error == ESP_OK && view.actions_visible && kDeviceFirstSetup && s_route == Route::MakeItYours10 &&
        (s_options_page == OptionsPage::Display || s_options_page == OptionsPage::DisplayRings || s_options_page == OptionsPage::Time))
        error = draw_page_marks(s_options_page == OptionsPage::Display ? 0 : s_options_page == OptionsPage::DisplayRings ? 1 : 2,
            3, strip, first_row, first_column);
    for (uint8_t index = 0; error == ESP_OK && view.actions_visible && index < view.event_choice_count; ++index) {
        const auto &choice = view.event_choices[index];
        error = UI::draw_text_action(choice.control, strip, first_row, first_column);
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
    if (s_view.line_count == 0 || on_quick_guide()) {
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
    uint16_t length = 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        if (!s_view.instant_lines[index]) {
            length += s_view.lines[index].text.length;
        }
    }
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
// Exception: Returns scene-content or duplicate-caption validation errors without replacing the frame.
esp_err_t attach_content(SceneFrame &scene)
{
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        if (is_codex_event_route(s_route) && index < s_event_plan.line_count) {
            s_view.lines[index].color = event_presentation::line_color(s_event_plan.lines[index], scene.text_elapsed_us);
        }
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
    s_view.approval_notice_visible = s_route == Route::ApprovalOnPc && s_view.actions_visible;
    s_view.action_visible_characters = s_view.actions_visible ? s_view.action.caption.text.length : 0;
    s_view.action_second_visible = s_view.actions_visible ? s_view.action_second_line.text.length : 0;
    s_view.alternative_visible_characters = s_view.actions_visible ? s_view.alternative.caption.text.length : 0;
    s_view.alternative_second_visible = s_view.actions_visible ? s_view.alternative_second_line.text.length : 0;
    s_view.options_visible_characters = s_view.actions_visible ? s_view.options.caption.text.length : 0;
    // Button captions never contribute to typewriter cadence or its audio counts.
    const bool choice = s_view.alternative_id != OnboardingAction::None;
    const uint16_t top = s_route == Route::DebugText ? 130 :
        s_route == Route::MakeItYours10 ? kPreferenceTop :
        s_route == Route::Install06 ? kInstallQrBounds.y :
        s_route == Route::Pairing07 ? kSessionCaptionTop :
        kDeviceFirstSetup && (s_route == Route::ConnectCodex12 || s_route == Route::LookingForPc14) ?
            kCodexPairingContentTop :
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
    if (s_route == Route::MakeItYours10) {
        content.bounds.left = kPreferenceLeft;
        content.bounds.right = kPreferenceLeft + kPreferenceWidth;
        if (kDeviceFirstSetup || on_maintenance_page() || on_quick_guide()) {
            // Menu rows and page marks end above the footer even when it is hidden.
            // Extending their full width to y=430 crosses the circular display edge.
            content.bounds.bottom = kOptionsFooterBounds.y;
        }
    }
    if (s_route == Route::SetupMenu) {
        content.bounds = {kSetupCodexBounds.x, kSetupCodexBounds.y,
            static_cast<uint16_t>(kSetupWifiBounds.x + kSetupWifiBounds.width),
            static_cast<uint16_t>(kSetupWifiBounds.y + kSetupWifiBounds.height)};
        if (kDeviceFirstSetup) {
            content.bounds.top = kConnectionsHintTop;
            content.bottom_action_visible = false;
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
            if (s_view.options_id == OnboardingAction::CheckRelay) {
                content.bottom_action_visible = true;
                const auto &bounds = s_view.options.bounds;
                content.bottom_action_bounds = {bounds.x, bounds.y,
                    static_cast<uint16_t>(bounds.x + bounds.width),
                    static_cast<uint16_t>(bounds.y + bounds.height)};
            }
#endif
        } else {
            content.bottom_action_bounds = {kSetupOptionsBounds.x, kSetupOptionsBounds.y,
                static_cast<uint16_t>(kSetupOptionsBounds.x + kSetupOptionsBounds.width),
                static_cast<uint16_t>(kSetupOptionsBounds.y + kSetupOptionsBounds.height)};
        }
    } else if (s_route == Route::CodexReplace) {
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
            (kDeviceFirstSetup && s_route == Route::Invitation05) ||
            options_back_available()) {
            content.control_bounds[0] = {10, 203, 70, 263};
        }
        content.control_bounds[1] = options_next_page_available() ? DamageRect{396, 203, 456, 263} :
            choice && s_route != Route::SetupMenu ? DamageRect{239, 260, 393, 364} : content.bottom_action_bounds;
    } else {
        content.control_bounds[0] = {10, 203, 70, 263};
        content.control_bounds[1] = {396, 203, 456, 263};
    }
    if (on_maintenance_page() && maintenance_pending()) {
        content.control_bounds[0] = {};
        content.control_bounds[1] = {};
    }
    if (kDeviceFirstSetup && s_view.header_icon != CODY2D::ActionIcon::None) {
        const auto &icon = s_view.header_icon == CODY2D::ActionIcon::WifiOff ?
            kOfflineDisc : s_view.header_icon_bounds;
        // Connection and More behavior glyphs live under the title in the body slot.
        if (s_route != Route::ConnectCodex12 && s_route != Route::LookingForPc14 &&
            s_route != Route::MakeItYours10) {
            content.leading_caption_bounds = {icon.x, icon.y, static_cast<uint16_t>(icon.x + icon.width),
                static_cast<uint16_t>(icon.y + icon.height)};
        }
        if (s_route == Route::NetworkWait) {
            content.bounds = {kOfflineSetupBounds.x, 223,
                static_cast<uint16_t>(kOfflineSetupBounds.x + kOfflineSetupBounds.width),
                static_cast<uint16_t>(kOfflineSetupBounds.y + kOfflineSetupBounds.height)};
            content.bottom_action_bounds = {kOfflineLaterBounds.x, kOfflineLaterBounds.y,
                static_cast<uint16_t>(kOfflineLaterBounds.x + kOfflineLaterBounds.width),
                static_cast<uint16_t>(kOfflineLaterBounds.y + kOfflineLaterBounds.height)};
            content.control_bounds[0] = {kOfflineSetupBounds.x, kOfflineSetupBounds.y,
                content.bounds.right, content.bounds.bottom};
            content.control_bounds[1] = content.bottom_action_bounds;
        } else if (s_route == Route::Invitation05) {
            content.bounds = {kGuideDownloadBounds.x, kGuideCaptionTop,
                static_cast<uint16_t>(kGuideDownloadBounds.x + kGuideDownloadBounds.width),
                static_cast<uint16_t>(kGuideDownloadBounds.y + kGuideDownloadBounds.height)};
            content.bottom_action_bounds = {kGuidePairBounds.x, kGuidePairBounds.y,
                static_cast<uint16_t>(kGuidePairBounds.x + kGuidePairBounds.width),
                static_cast<uint16_t>(kGuidePairBounds.y + kGuidePairBounds.height)};
            content.control_bounds[1] = content.bottom_action_bounds;
        } else if (s_route == Route::Install06) {
            content.bounds.top = 139;
            content.control_bounds[0] = {10, 203, 70, 263};
        } else if (s_route == Route::Hello04) {
            // The saved SSID can occupy two full rows above Tap to pair.
            content.bounds = {83, 223, 383, kSingleActionBounds.y};
        }
    }
    if (kDeviceFirstSetup && (s_route == Route::Online09 ||
        (s_route == Route::NetworkWait && s_startup_network_passive))) {
        // The two SSID rows fit above Continue or in the passive status body.
        content.bounds = {83, kWifiStatusDetailTop, 383, 340};
        if (s_route == Route::NetworkWait) {
            content.control_bounds[0] = {};
            content.control_bounds[1] = {};
        }
    }
    if (kDeviceFirstSetup && s_route == Route::CodexReplace) {
        // All three open actions belong to the body; the lateral Back remains separate.
        content.bounds = {83, 174, 383, 392};
        content.bottom_action_visible = false;
        content.bottom_action_bounds = {};
        content.control_bounds[0] = {10, 203, 70, 263};
        content.control_bounds[1] = {83, 302, 383, 392};
    }
    if (s_home_wifi_flow && (s_route == Route::Invitation05 || s_route == Route::Install06 || s_route == Route::Pairing07)) {
        content.bounds = s_view.wifi_menu ? kHomeMenuContent : DamageRect{73, 125, 393, 380};
        content.leading_caption_bounds = {};
        content.bottom_action_visible = false;
        content.bottom_action_bounds = {};
        content.control_bounds[0] = {10, 203, 70, 263};
        content.control_bounds[1] = s_view.wifi_menu ? DamageRect{} : DamageRect{396, 203, 456, 263};
        if (s_view.wifi_menu) {
            const auto error = prepare_styled_title(scene.title.text, {73, 83, 320, 21}, content.heading, 3, true);
            if (error != ESP_OK) return error;
        }
    }
    if ((s_route == Route::Install06 || s_route == Route::Pairing07) && s_view.qr.side != 0) {
        content.bounds = {89, 128, 377, 416};
        content.leading_caption_bounds = {};
        content.bottom_action_visible = false;
        content.bottom_action_bounds = {};
        content.control_bounds[0] = {};
        content.control_bounds[1] = {};
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
            // Full-width rows end inside the safe square; the narrow footer hint
            // is independently measured against the lower circular chord.
            content.bounds.bottom = 388;
            content.bottom_action_visible = true;
            content.bottom_action_bounds = {99, 394, 367, 419};
        } else if (s_view.action_visible) {
            content.bounds.bottom = kSingleActionBounds.y;
            content.bottom_action_visible = true;
            content.bottom_action_bounds = {113, 352, 353, 424};
        }
        if (s_route == Route::DecisionQueue || s_route == Route::DecisionChoices27 || s_route == Route::DecisionConfirm27 ||
            s_route == Route::CheckFailures24) {
            content.control_bounds[0] = {10, 203, 70, 263};
        }
        if (s_route == Route::DecisionConfirm27) {
            content.bounds.top = event_presentation::kReviewPanel.y;
        }
        content.control_bounds[1] = content.bottom_action_bounds;
        if (s_route == Route::ApprovalOnPc) {
            // The amber caption is above the title; body/artwork stay below it.
            // Use the actual prepared caption extent instead of an overlapping union.
            for (uint8_t index = 0; index < s_event_plan.line_count; ++index) {
                if (!s_event_plan.lines[index].pc_heading) {
                    continue;
                }
                if (content.leading_caption_bounds.bottom != 0) {
                    ESP_LOGE(kTag, "Approval content rejected duplicate PC heading (%s)",
                        esp_err_to_name(ESP_ERR_INVALID_STATE));
                    return ESP_ERR_INVALID_STATE;
                }
                const auto &bounds = s_view.lines[index].bounds;
                content.leading_caption_bounds = {bounds.x, bounds.y,
                    static_cast<uint16_t>(bounds.x + bounds.width),
                    static_cast<uint16_t>(bounds.y + bounds.height)};
            }
            if (content.leading_caption_bounds.bottom == 0) {
                ESP_LOGE(kTag, "Approval content rejected missing PC heading (%s)",
                    esp_err_to_name(ESP_ERR_INVALID_STATE));
                return ESP_ERR_INVALID_STATE;
            }
            content.bounds = {73, 196, 393, 388};
            content.bottom_action_visible = true;
            content.bottom_action_bounds = {99, 393, 367, 419};
            content.control_bounds[0] = {10, 203, 70, 263};
            content.control_bounds[1] = {};
        }
    }
    if (s_route == Route::MakeItYours10) {
        // Peer pages navigate by swipe; retain the separate footer and parent Back.
        if (options_previous_page_by_swipe()) content.control_bounds[0] = {};
        content.control_bounds[1] = content.bottom_action_bounds;
    }
    if (content.control_bounds[1].left == 396 && content.control_bounds[1].top == 203 &&
        content.control_bounds[1].right == 456 && content.control_bounds[1].bottom == 263) {
        content.control_bounds[1] = {};
    }
    return set_scene_content(scene, content);
}

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// wifi: Canonical station snapshot with up to 32 SSID bytes and a terminator.
// Returns the original name or a printable explanation when Cody cannot display it.
const char *pairing_debug_ssid(const LUNASHELL::WifiSnapshot &wifi)
{
    if (wifi.ssid[0] == '\0') {
        return "SSID unavailable";
    }
    for (const unsigned char byte : wifi.ssid) {
        if (byte == 0) {
            return wifi.ssid;
        }
        if (byte < 0x20 || byte > 0x7E) {
            return "SSID not printable in ASCII";
        }
    }
    return "SSID unavailable";
}

// observation: One coherent canonical Wi-Fi/BLE/pairing snapshot after successful handoff.
// scene: Receives the pairing diagnostics and six immutable text rows.
// Exception: Rejects missing IP/BLE identity, truncation and layout errors.
esp_err_t enter_pairing_debug(const LUNASHELL::ConnectivityObservation &observation, SceneFrame &scene)
{
    const auto &wifi = observation.radios.wifi;
    const auto &ble = observation.radios.ble;
    if (wifi.ip_address[0] == '\0' || ble.device_name[0] == '\0') {
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
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    s_route = Route::DebugText;
    s_view.line_count = kPairingDebugLineCount;
    error = format_debug_line(s_view.debug_text[0], kDebugLineBytes,
        "1. %s", "Wi-Fi connected");
    if (error == ESP_OK) {
        // The numbered row fits all 32 printable SSID bytes at the native font advance.
        error = format_debug_line(s_view.debug_text[1], kDebugLineBytes,
            "2. %s", pairing_debug_ssid(wifi));
    }
    if (error == ESP_OK) {
        error = format_debug_line(s_view.debug_text[2], kDebugLineBytes,
            "3. IP obtained: `%s`", wifi.ip_address);
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
    // A hidden route must not charge its elapsed time to the Home marquee on return.
    s_command_hints.sampled_us = -1;
    scene = prepared;
    ESP_LOGI(kTag, "Reusable debug-text route entered after pairing handoff");
    return ESP_OK;
}
#endif

// visible: Whether the selected-chat presentation can request new semantic cues.
// Exception: Logs optional owner failures without replacing their canonical result.
void set_companion_audio_visible(bool visible)
{
#if defined(CODY_DEVICE_UI_ONLY)
    // Device effects belong to the model. Opening Options must not mute notifications.
    (void)visible;
#else
    if (!visible) {
        s_question_audio_schedule.last_attempt_at_us = -1;
    }
    const esp_err_t error = set_codex_audio_suspended(!visible);
    if (error != ESP_OK) {
        // Audio is optional at this UI orchestration boundary. Its acquisition
        // owner retains the real failure in the canonical playback snapshot.
        ESP_LOGE(kTag, "Optional CCP audio visibility failed (%s)", esp_err_to_name(error));
    }
#endif
}

// previous: Previously presented route, used to recognize real connection recovery.
// destination: Newly accepted presentation; unavailable activity never creates a work cue.
// phase: Authenticated semantic phase from the canonical snapshot.
// chat_id: Bounded authenticated chat identity, used only to suppress duplicate cues.
void notify_companion_transition(Route previous, Route destination,
                                 LUNASHELL::CodexActivityState phase, const char *chat_id)
{
#if defined(CODY_DEVICE_UI_ONLY)
    // The model consumes connection events once, independently of navigation.
    (void)previous;
    (void)destination;
    (void)phase;
    (void)chat_id;
#else
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
        case Activity::Paused: cue = CodexAudioCue::Paused; break;
        case Activity::Interrupted: cue = CodexAudioCue::Interrupted; break;
        case Activity::Ended: cue = CodexAudioCue::SessionEnded; break;
        case Activity::None:
        case Activity::Active:
        case Activity::ResponseReady: // The authenticated ReplyReady event owns its sound.
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
#endif
}

// No parameters: resolve the active Options page title from its presentation state.
const char *options_title()
{
    if (s_preference_error != ESP_OK) {
        return "Save failed";
    }
    switch (s_options_page) {
    case OptionsPage::Menu: return kDeviceFirstSetup ? "TOOLS" : "Options 1/2";
    case OptionsPage::MenuMore: return kDeviceFirstSetup ? "PREFERENCES" : "Options 2/2";
    case OptionsPage::MenuMaintenance: return "DEVICE";
    case OptionsPage::Display: return "Display";
    case OptionsPage::DisplayRings: return "Display & time";
    case OptionsPage::Time: return "Time";
    case OptionsPage::Behavior: return "Behavior";
    case OptionsPage::BehaviorMore: return "More behavior";
    case OptionsPage::BehaviorMoreSettings: return "More behavior";
    case OptionsPage::QuietHours: return "Quiet hours";
    case OptionsPage::QuietEdit: return s_quiet_edit_start ? "Quiet start" : "Quiet end";
    case OptionsPage::SynthVoice: return "Synth Voice";
    case OptionsPage::RestartConfirm: return s_restart_error == ESP_OK ? "Restart device" : "Restart failed";
    case OptionsPage::RestartPending: return "Restarting...";
    case OptionsPage::ResetConfirm: return "Factory reset";
    case OptionsPage::ResetPending: return "Resetting...";
    case OptionsPage::ResetFailed: return "Reset failed";
    case OptionsPage::GuideButtons: return "Buttons";
    case OptionsPage::GuideTouch: return "Touch";
    case OptionsPage::GuideOffline: return "Offline";
    }
    return "Options";
}

// text: Permanent command label, explanation or requirement.
// top: Logical row inside the guide's safe area.
// font: Headline font for commands, body font for explanations.
// color: High-contrast page accent or secondary text color.
// Exception: Returns the original caption layout error.
esp_err_t append_guide_line(const char *text, uint16_t top, const CODY2D::MonoFont &font, uint16_t color)
{
    const auto index = s_view.line_count;
    auto &line = s_view.lines[index];
    const auto error = prepare_caption(text, top, line, kPreferenceWidth, font);
    if (error == ESP_OK) {
        line.color = color;
        s_view.instant_lines[index] = true;
        ++s_view.line_count;
    }
    return error;
}

// scene: Current Options frame receiving the guide's heading accent.
// Exception: Returns the original caption or footer layout error.
esp_err_t prepare_quick_guide_content(SceneFrame &scene)
{
    struct GuideCommand { const char *label; const char *detail; };
    constexpr GuideCommand buttons[] = {
        {"HOLD BOOT 2S", "Options / tap: Home"}, {"POWER", "Standby or wake"}, {"HOLD PWR 3S", "Power off"},
    };
    constexpr GuideCommand touch[] = {
        {"SWIPE", "Change pages"}, {"SWIPE DOWN", "Home from top edge"},
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        {"DOUBLE TAP", "Record on stats"},
#else
        {"DOUBLE TAP", "Voice not in this build"},
#endif
    };
    constexpr GuideCommand offline[] = {
        {"SAVED STATS", "Last update in UTC"}, {"TIMER", "Works without PC"}, {"RESTART", "Keeps settings"},
    };
    static_assert(std::size(buttons) == std::size(touch) && std::size(touch) == std::size(offline));
    static_assert(2U * std::size(touch) + 1U <= kContentLineCapacity);
    constexpr uint16_t kGuideCyan = Theme::accent_cyan;
    constexpr uint16_t kGuideViolet = Theme::accent_violet;
    constexpr uint16_t kGuideMint = Theme::secondary_bright;
    constexpr uint16_t kGuideDetail = Theme::text_detail;
    const bool touch_page = s_options_page == OptionsPage::GuideTouch;
    const bool last_page = s_options_page == OptionsPage::GuideOffline;
    s_view.menu_page = touch_page ? 2U : last_page ? 3U : 1U;
    const auto *commands = touch_page ? touch : last_page ? offline : buttons;
    const uint16_t accent = touch_page ? kGuideViolet : last_page ? kGuideMint : kGuideCyan;
    scene.title.color = accent;
    for (size_t index = 0; index < std::size(buttons); ++index) {
        const auto top = static_cast<uint16_t>(130U + index * 68U);
        auto error = append_guide_line(commands[index].label, top, SceneData::title_font, accent);
        if (error == ESP_OK) {
            error = append_guide_line(commands[index].detail, static_cast<uint16_t>(top + 43U),
                SceneData::body_font, kGuideDetail);
        }
        if (error != ESP_OK) return error;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (touch_page) {
        const auto error = append_guide_line("Dictation On + Codex link", 340, SceneData::body_font, kGuideDetail);
        if (error != ESP_OK) return error;
    }
#endif
    if (last_page && kDeviceFirstSetup) {
        auto &row = s_view.preferences[s_view.preference_count];
        row = {};
        row.action = OnboardingAction::Done;
        CODY2D::TextLine unused{};
        auto error = prepare_button(row.action, "OK", {93, 336, 280, 30}, true, false, row.control, unused);
        if (error == ESP_OK) {
            row.control.icon = CODY2D::ActionIcon::Check;
            row.control.icon_color = Theme::secondary_default;
            row.control.icon_center = {111, 351};
            error = CODY2D::prepare_text_line(SceneData::body_font, {"OK", 2},
                {141, 338}, kPairingTitle, row.control.caption);
        }
        if (error != ESP_OK) return error;
        ++s_view.preference_count;
        return prepare_action(OnboardingAction::None, false);
    }
    return prepare_action(last_page ? OnboardingAction::Done : OnboardingAction::None, last_page);
}

// top: Logical top of the retry row, below the current error message.
// Exception: Returns the original control or caption preparation error.
esp_err_t append_retry_row(uint16_t top)
{
    if (s_view.preference_count >= kPreferencesPerPage) return ESP_ERR_INVALID_SIZE;
    auto &row = s_view.preferences[s_view.preference_count];
    row = {};
    row.action = OnboardingAction::Retry;
    CODY2D::TextLine unused{};
    const CODY2D::Rect bounds{kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight};
    auto error = prepare_button(row.action, "Retry", bounds, !s_command_pending, false, row.control, unused);
    if (error == ESP_OK) {
        row.control.icon = CODY2D::ActionIcon::Retry;
        row.control.icon_color = Theme::accent_warm;
        row.control.icon_center = {static_cast<int16_t>(bounds.x + 18U),
            static_cast<int16_t>(bounds.y + bounds.height / 2U)};
        error = CODY2D::prepare_text_line(SceneData::body_font, {"Retry", 5},
            {static_cast<int16_t>(bounds.x + 48U),
             static_cast<int16_t>(bounds.y + (bounds.height - SceneData::body_font.height) / 2U)},
            kPairingTitle, row.control.caption);
    }
    if (error == ESP_OK) ++s_view.preference_count;
    return error;
}

// No parameters: prepare the requested restart or factory-reset page for the display owner.
// Exception: Returns the original native caption or action layout error.
esp_err_t prepare_maintenance_content()
{
    const bool restart = s_options_page == OptionsPage::RestartConfirm;
    if (restart || s_options_page == OptionsPage::ResetConfirm) {
        const char *detail = restart ? nullptr : "Restart with defaults";
        if (restart && s_restart_error != ESP_OK) {
            std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
                static_cast<unsigned long>(s_restart_error));
            detail = s_view.error_text;
        }
        const char *instructions[] = {
            restart ? "Keeps Wi-Fi/settings" : "Clear Wi-Fi, Codex",
            restart ? "Stops active timer" : "link and preferences.",
            detail,
        };
        for (const char *line : instructions) {
            if (line == nullptr) continue;
            const uint16_t top = 155 + 29 * s_view.line_count;
            const auto error = prepare_caption(line, top, s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.line_count;
        }
        const OnboardingAction actions[]{OnboardingAction::Cancel,
            restart ? OnboardingAction::ConfirmRestart : OnboardingAction::ConfirmFactoryReset};
        const char *labels[]{"Cancel", restart ? "Restart device" : "Factory reset"};
        const CODY2D::Rect bounds[]{kResetCancelBounds, kResetConfirmBounds};
        for (size_t index = 0; index < std::size(actions); ++index) {
            auto &row = s_view.preferences[index];
            row = {};
            row.action = actions[index];
            CODY2D::TextLine unused{};
            auto error = prepare_button(row.action, labels[index], bounds[index], true, false, row.control, unused);
            if (error == ESP_OK) {
                row.control.icon = index == 0 ? CODY2D::ActionIcon::Close : CODY2D::ActionIcon::Check;
                row.control.icon_color = index == 0 ? kPairingCaption :
                    restart ? Theme::accent_cyan : Theme::accent_error_icon;
                row.control.icon_center = {static_cast<int16_t>(bounds[index].x + 18U),
                    static_cast<int16_t>(bounds[index].y + bounds[index].height / 2U)};
                error = CODY2D::prepare_text_line(SceneData::body_font,
                    {labels[index], static_cast<uint16_t>(std::strlen(labels[index]))},
                    {static_cast<int16_t>(bounds[index].x + 48U),
                     static_cast<int16_t>(bounds[index].y + (bounds[index].height - SceneData::body_font.height) / 2U)},
                    kPairingTitle, row.control.caption);
            }
            if (error != ESP_OK) return error;
            ++s_view.preference_count;
        }
        return prepare_action(OnboardingAction::None, false);
    }
    const bool pending = maintenance_pending();
    const auto error_status = s_options_page == OptionsPage::RestartPending ? ESP_OK : s_factory_reset_error;
    const char *message = pending ? "Please wait" : "Restart did not occur";
    if (pending && error_status != ESP_OK) {
        message = "Status unavailable";
    }
    esp_err_t error = prepare_caption(message, 155, s_view.lines[0]);
    if (error != ESP_OK) {
        return error;
    }
    s_view.line_count = 1;
    if (error_status != ESP_OK) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(error_status));
        error = prepare_caption(s_view.error_text, 184, s_view.lines[1]);
        if (error != ESP_OK) {
            return error;
        }
        s_view.line_count = 2;
    }
    if (!pending) {
        error = append_retry_row(250);
        if (error != ESP_OK) return error;
    }
    return prepare_action(OnboardingAction::None, false);
}

struct PreferencePresentation {
    bool available = false;
    bool active = false;
    uint32_t value = 0;
};

// identity: One row actually displayed by the current options page.
// observation: Canonical settings/orientation projection, never a hardware read.
// Only visible values participate; source timestamps and hidden settings do not repaint a page.
PreferencePresentation project_preference(PreferenceId identity,
                                         const LUNASHELL::CodyOptionsObservation &observation)
{
    const auto &preferences = observation.preferences;
    const auto &orientation = observation.orientation;
    PreferencePresentation result{preferences.data_valid && !preferences.data_stale, false, 0};
    switch (identity) {
    case PreferenceId::SynthEnabled:
    case PreferenceId::SynthVolume:
    case PreferenceId::SynthVoice:
        // No engine has been admitted yet. Never offer a setting that cannot be applied.
        result.available = false;
        break;
    case PreferenceId::Screensaver:
        result.available = preferences.screensaver_valid && preferences.screensaver_error == ESP_OK;
        result.active = preferences.screensaver_enabled;
        break;
    case PreferenceId::AssistantQuotaRing:
        result.available = preferences.assistant_quota_ring_valid && preferences.assistant_quota_ring_error == ESP_OK;
        result.active = preferences.assistant_quota_ring_enabled;
        break;
    case PreferenceId::BatteryRing:
        result.available = preferences.battery_ring_valid && preferences.battery_ring_error == ESP_OK;
        result.active = preferences.battery_ring_enabled;
        break;
    case PreferenceId::AutoTime:
        result.available = preferences.auto_time_valid && preferences.auto_time_error == ESP_OK;
        result.active = preferences.auto_time_enabled;
        break;
    case PreferenceId::Dictation:
        result.available = preferences.dictation_valid && preferences.dictation_error == ESP_OK;
        result.active = preferences.dictation_enabled;
        break;
    case PreferenceId::ShakeWake:
        result.active = preferences.shake_to_wake;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        result.available = false;
#endif
        break;
    case PreferenceId::Sounds:
        result.active = preferences.sounds_enabled;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO || !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        result.available = false;
#endif
        break;
    case PreferenceId::AutoRotate:
    case PreferenceId::Orientation:
        result.available = orientation.data_valid && !orientation.data_stale && !orientation.rotation_pending;
        result.active = orientation.auto_rotation_enabled;
        // Auto displays no numeric angle and disables the manual action.
        if (identity == PreferenceId::Orientation && !result.active) {
            result.value = orientation.rendering_rotation_degrees;
        }
        break;
    case PreferenceId::UsageMode:
        result.available = preferences.usage_mode_valid && preferences.usage_mode_error == ESP_OK;
        result.active = preferences.usage_mode == LUNASHELL::CodyUsageMode::Away;
        break;
    case PreferenceId::Brightness:
        result.available = observation.brightness_valid && observation.brightness_percent >= 10U &&
            observation.brightness_percent <= 100U;
        result.value = observation.brightness_percent;
        break;
    case PreferenceId::QuietEnabled:
    case PreferenceId::QuietStart:
    case PreferenceId::QuietEnd:
        result.available = false; // Quiet hours use the time owner's independent snapshot.
        break;
    }
    if (!result.available) {
        result.active = false;
        result.value = 0;
    }
    return result;
}

// preference: Setting selected by the local user.
// value: Exact requested value passed to the settings owner.
// observation: Canonical state captured immediately before the write.
// Returns false when the prior value is unavailable or already matches.
bool preference_would_change(PreferenceId preference, uint32_t value,
    const LUNASHELL::CodyOptionsObservation &observation)
{
    const auto previous = project_preference(preference, observation);
    if (!previous.available || (preference == PreferenceId::Orientation && previous.active)) return false;
    const uint32_t old_value = preference == PreferenceId::Brightness || preference == PreferenceId::Orientation
        ? previous.value : static_cast<uint32_t>(previous.active);
    return value != old_value;
}

// observation: Latest canonical projection; compare only rows the user can currently see.
bool options_presentation_changed(const LUNASHELL::CodyOptionsObservation &observation)
{
    for (uint8_t index = 0; index < s_view.preference_count; ++index) {
        if (s_view.preferences[index].action != OnboardingAction::None) continue;
        const auto identity = s_view.preferences[index].identity;
        const auto current = project_preference(identity, observation);
        const auto previous = project_preference(identity, s_presented_options);
        if (current.available != previous.available || current.active != previous.active ||
            current.value != previous.value) {
            return true;
        }
    }
    return false;
}

struct MenuAppearance {
    CODY2D::ActionIcon icon;
    uint16_t color;
    const char *hint;
};

// action: Stable Options destination, independent of its current page or label.
MenuAppearance menu_appearance(OnboardingAction action)
{
    using Icon = CODY2D::ActionIcon;
    switch (action) {
    case OnboardingAction::Connections: return {Icon::Link, Theme::primary_muted, "Wi-Fi and PC"};
    case OnboardingAction::CodexStatistics: return {Icon::Statistics, Theme::secondary_default, "Limits and credits"};
    case OnboardingAction::BrowseCodex: return {Icon::Statistics, Theme::secondary_default, "Chats, events and limits"};
    case OnboardingAction::LocalTimer: return {Icon::Clock, Theme::accent_warm, "Focus countdown"};
    case OnboardingAction::AudioLevels: return {Icon::Microphone, Theme::accent_cyan, "Microphone levels"};
    case OnboardingAction::VisibleScreens: return {Icon::PlanBusiness, Theme::accent_cyan, "Dashboard pages"};
    case OnboardingAction::DisplayOptions: return {Icon::Orientation, Theme::accent_warm, "Light and rotation"};
    case OnboardingAction::BehaviorOptions: return {Icon::Motion, Theme::primary_muted, "Sound, wake, voice"};
    case OnboardingAction::RestartDevice: return {Icon::Retry, Theme::accent_cyan, "Reboot Cody"};
    case OnboardingAction::FactoryReset: return {Icon::Close, Theme::accent_error_icon, "Erase settings"};
    case OnboardingAction::QuickGuide: return {Icon::PlanEducation, Theme::secondary_default, "Learn the controls"};
    case OnboardingAction::OpenAbout: return {Icon::PlanAccount, Theme::primary_muted, "Device information"};
    case OnboardingAction::MoreDisplay: return {Icon::Orientation, Theme::accent_cyan, "More display settings"};
    case OnboardingAction::MoreBehavior: return {Icon::Motion, Theme::primary_muted, "More behavior settings"};
    case OnboardingAction::MoreBehaviorSettings: return {Icon::Motion, Theme::primary_muted, "More behavior settings"};
    case OnboardingAction::QuietHours: return {Icon::Clock, Theme::accent_warm, "Quiet schedule"};
    case OnboardingAction::SynthOptions: return {Icon::Sound, Theme::accent_warm, "Voice settings"};
    case OnboardingAction::ScheduleBirthIntro: return {Icon::Retry, Theme::secondary_default, "Next restart"};
    default: return {Icon::None, kPairingCaption, ""};
    }
}

// label: Complete destination title.
// appearance: Semantic icon, accent color and complete supporting description.
// control: Receives the flat row and its shared raster/touch rectangle.
// detail: Receives the optional second line of the same destination label.
// Exception: Returns the original glyph error or an overflowing label.
esp_err_t prepare_menu_row(const char *label, const MenuAppearance &appearance,
    CODY2D::CapsuleControl &control, CODY2D::TextLine &detail)
{
    constexpr uint16_t kTextInset = 63;
    constexpr uint16_t kTrailingSpace = 10;
    const auto &bounds = control.bounds;
    control.corner_radius = 12;
    control.top_color = Theme::surface_menu_card_legacy;
    control.bottom_color = Theme::surface_menu_card_legacy;
    control.border_color = Theme::controls_menu_legacy_border;
    control.icon = appearance.icon;
    control.icon_color = appearance.color;
    control.icon_center = {static_cast<int16_t>(bounds.x + 33U), static_cast<int16_t>(bounds.y + bounds.height / 2U)};

    // text: Borrowed printable portion of the original label.
    // font: Immutable face for that line.
    // offset: Logical y relative to the row top.
    // color: Native foreground.
    // line: Receives the prepared left-aligned caption.
    // Exception: Preserves the original glyph error or rejects an overflowing caption.
    const auto prepare_line = [&](CODY2D::TextSpan text, const CODY2D::MonoFont &font,
                                  uint16_t offset, uint16_t color, CODY2D::TextLine &line) {
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(font, text, width);
        if (error != ESP_OK) return error;
        if (width + kTextInset + kTrailingSpace > bounds.width || offset + font.height > bounds.height) {
            ESP_LOGE(kTag, "Options row caption does not fit: %s", label);
            return ESP_ERR_INVALID_SIZE;
        }
        return CODY2D::prepare_text_line(font, text,
            {static_cast<int16_t>(bounds.x + kTextInset), static_cast<int16_t>(bounds.y + offset)}, color, line);
    };
    detail = {};
    const auto length = static_cast<uint16_t>(std::strlen(label));
    const char *space = std::strchr(label, ' ');
    if (length == 0) return ESP_ERR_INVALID_ARG;
    if (space == nullptr || space == label || space[1] == '\0') {
        return prepare_line({label, length}, HomeFonts::heading_font,
            static_cast<uint16_t>((bounds.height - HomeFonts::heading_font.height) / 2U),
            kPairingTitle, control.caption);
    }
    const auto first_length = static_cast<uint16_t>(space - label);
    const auto second_length = static_cast<uint16_t>(length - first_length - 1U);
    constexpr uint16_t kLineGap = 2;
    const auto total_height = static_cast<uint16_t>(2U * HomeFonts::heading_font.height + kLineGap);
    if (total_height > bounds.height) return ESP_ERR_INVALID_SIZE;
    const auto first_top = static_cast<uint16_t>((bounds.height - total_height) / 2U);
    auto error = prepare_line({label, first_length}, HomeFonts::heading_font, first_top,
        kPairingTitle, control.caption);
    if (error == ESP_OK) error = prepare_line({space + 1, second_length}, HomeFonts::heading_font,
        static_cast<uint16_t>(first_top + HomeFonts::heading_font.height + kLineGap), kPairingTitle, detail);
    return error;
}

// action: Stable destination or explicit settings action.
// enabled: Whether a complete released touch may activate this row.
// Exception: Preserves the original action-label and row-layout errors.
esp_err_t append_options_action(OnboardingAction action, bool enabled)
{
    if (s_view.preference_count >= kPreferencesPerPage) return ESP_ERR_INVALID_SIZE;
    auto &target = s_view.preferences[s_view.preference_count];
    target = {};
    target.action = action;
    const auto top = static_cast<uint16_t>(kPreferenceTop + s_view.preference_count * kPreferenceStep);
    const char *label = nullptr;
    auto error = action_label(action, label);
    CODY2D::TextLine unused{};
    if (error == ESP_OK) error = prepare_button(action, label,
        {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight}, enabled, false, target.control, unused);
    if (error == ESP_OK) error = prepare_menu_row(label, menu_appearance(action), target.control, target.value);
    if (error == ESP_OK) ++s_view.preference_count;
    return error;
}

// minute: Validated local minute in the day.
// value: Receives a terminated HH:MM label.
void format_quiet_time(uint16_t minute, char (&value)[6])
{
    const auto hour = minute / 60U;
    const auto within_hour = minute % 60U;
    value[0] = static_cast<char>('0' + hour / 10U);
    value[1] = static_cast<char>('0' + hour % 10U);
    value[2] = ':';
    value[3] = static_cast<char>('0' + within_hour / 10U);
    value[4] = static_cast<char>('0' + within_hour % 10U);
    value[5] = '\0';
}

// action: Stable local quiet-hours control.
// value: Optional printable value copied into display-owned storage.
// enabled: Whether the row admits a released touch.
// Exception: Returns the original control or text preparation error.
esp_err_t append_quiet_row(OnboardingAction action, const char *value, bool enabled)
{
    if (s_view.preference_count >= kPreferencesPerPage) return ESP_ERR_INVALID_SIZE;
    auto &row = s_view.preferences[s_view.preference_count];
    row = {};
    row.action = action;
    const auto top = static_cast<uint16_t>(kPreferenceTop + s_view.preference_count * kPreferenceStep);
    const char *label = nullptr;
    auto error = action_label(action, label);
    CODY2D::TextLine unused{};
    if (error == ESP_OK) error = prepare_button(action, label,
        {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight}, enabled, false, row.control, unused);
    if (error == ESP_OK) {
        row.control.icon = CODY2D::ActionIcon::Clock;
        row.control.icon_color = Theme::accent_warm;
        row.control.icon_center = {static_cast<int16_t>(kPreferenceLeft + 18U), static_cast<int16_t>(top + kPreferenceHeight / 2U)};
        error = CODY2D::prepare_text_line(SceneData::body_font,
            {label, static_cast<uint16_t>(std::strlen(label))},
            {static_cast<int16_t>(kPreferenceLeft + 48U), static_cast<int16_t>(top + (value ? 2U : 14U))},
            kPairingTitle, row.control.caption);
    }
    if (error == ESP_OK && value != nullptr) {
        std::snprintf(row.value_text, sizeof(row.value_text), "%s", value);
        error = CODY2D::prepare_text_line(SceneData::body_font,
            {row.value_text, static_cast<uint16_t>(std::strlen(row.value_text))},
            {static_cast<int16_t>(kPreferenceLeft + 48U), static_cast<int16_t>(top + 28U)},
            Theme::secondary_default, row.value);
    }
    if (error == ESP_OK && (action == OnboardingAction::QuietHour || action == OnboardingAction::QuietMinute)) {
        if (s_view.line_count >= kContentLineCapacity) return ESP_ERR_INVALID_SIZE;
        const auto index = s_view.line_count;
        error = CODY2D::prepare_text_line(SceneData::body_font, {"+", 1},
            {static_cast<int16_t>(kPreferenceLeft + kPreferenceWidth - 48U), static_cast<int16_t>(top + 2U)},
            kPairingTitle, s_view.lines[index]);
        if (error == ESP_OK) { s_view.instant_lines[index] = true; ++s_view.line_count; }
    }
    if (error == ESP_OK) ++s_view.preference_count;
    return error;
}

// scene: Screen 10 receiving actual canonical values or a visible write failure.
// captured: Optional observation already read by this update; avoids a second snapshot lock/copy.
// Exception: Returns the original snapshot or text preparation failure.
esp_err_t prepare_options_content(SceneFrame &scene,
                                  const LUNASHELL::CodyOptionsObservation *captured = nullptr)
{
    s_view.line_count = 0;
    s_view.preference_count = 0;
    s_view.menu_page = 0;
    s_view.menu_page_count = 3;
    s_view.header_icon = CODY2D::ActionIcon::None;
    s_view.header_icon_bounds = {};
    s_view.header_icon_color = 0;
    std::fill_n(s_view.instant_lines, kContentLineCapacity, false);
    const char *title = options_title();
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
    scene.title.color = scene.definition->title_color;
    if (on_maintenance_page()) {
        return prepare_maintenance_content();
    }
    if (on_quick_guide()) {
        return prepare_quick_guide_content(scene);
    }
    if (s_preference_error != ESP_OK) {
        std::snprintf(s_view.error_text, sizeof(s_view.error_text), "Error: 0x%lx",
            static_cast<unsigned long>(s_preference_error));
        error = prepare_caption(s_view.error_text, 260, s_view.lines[0]);
        s_view.line_count = 1;
        if (error == ESP_OK) error = append_retry_row(304);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (on_options_menu()) {
        constexpr OnboardingAction legacy_actions[] = {
            OnboardingAction::Connections, OnboardingAction::CodexStatistics, OnboardingAction::VisibleScreens,
            OnboardingAction::DisplayOptions, OnboardingAction::BehaviorOptions,
        };
        constexpr OnboardingAction device_actions[] = {
            OnboardingAction::Connections, OnboardingAction::BrowseCodex, OnboardingAction::LocalTimer,
            OnboardingAction::VisibleScreens, OnboardingAction::DisplayOptions, OnboardingAction::BehaviorOptions,
            OnboardingAction::RestartDevice, OnboardingAction::FactoryReset, OnboardingAction::QuickGuide,
            OnboardingAction::OpenAbout,
        };
        static_assert(std::size(device_actions) <= 3U * kPreferencesPerPage);
        const auto *actions = kDeviceFirstSetup ? device_actions : legacy_actions;
        const size_t count = kDeviceFirstSetup ? std::size(device_actions) : std::size(legacy_actions);
        size_t first = 0;
        if (s_options_page == OptionsPage::MenuMore) first = 3U;
        if (s_options_page == OptionsPage::MenuMaintenance) first = 6U;
        if (kDeviceFirstSetup) s_view.menu_page = static_cast<uint8_t>(first / 3U + 1U);
        const size_t page_capacity = kDeviceFirstSetup && s_options_page == OptionsPage::MenuMaintenance ? 4U : 3U;
        const size_t end = std::min(first + page_capacity, count);
        for (size_t index = first; index < end; ++index) {
            const auto action = actions[index];
            auto &target = s_view.preferences[s_view.preference_count];
            target = {};
            target.action = action;
            const uint16_t top = kPreferenceTop + s_view.preference_count * kPreferenceStep;
            const char *label = nullptr;
            error = action_label(action, label);
            if (error == ESP_OK) {
                CODY2D::TextLine unused;
                error = prepare_button(action, label, {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight},
                    true, false, target.control, unused);
                if (error == ESP_OK && kDeviceFirstSetup) {
                    error = prepare_menu_row(label, menu_appearance(action), target.control, target.value);
                }
            }
            if (error != ESP_OK) {
                return error;
            }
            ++s_view.preference_count;
        }
        if (kDeviceFirstSetup) {
            s_view.action = {};
            s_view.action_id = OnboardingAction::None;
            s_view.action_visible = false;
            ++s_navigation_revision;
            return ESP_OK;
        }
        return prepare_action(OnboardingAction::Done, true);
    }
    if (s_options_page == OptionsPage::BehaviorMoreSettings) {
        s_view.menu_page = 2;
        s_view.menu_page_count = 2;
        error = append_options_action(OnboardingAction::QuietHours, !s_command_pending);
        if (error == ESP_OK) error = append_options_action(OnboardingAction::SynthOptions, !s_command_pending);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (s_options_page == OptionsPage::QuietHours || s_options_page == OptionsPage::QuietEdit) {
        LUNASHELL::ClockSnapshot clock{};
        error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
        const bool available = error == ESP_OK && clock.preferences_valid &&
            clock.quiet_start_minute < 1440U && clock.quiet_end_minute < 1440U;
        if (available) s_quiet_clock = clock;
        char value[6]{};
        if (s_options_page == OptionsPage::QuietHours) {
            const bool equal_disabled = available && !clock.quiet_hours_enabled &&
                clock.quiet_start_minute == clock.quiet_end_minute;
            error = append_quiet_row(OnboardingAction::QuietToggle,
                available ? (clock.quiet_hours_enabled ? "On" : "Off") : "N/A",
                available && !equal_disabled && !s_command_pending);
            if (error == ESP_OK) {
                if (available) format_quiet_time(clock.quiet_start_minute, value);
                error = append_quiet_row(OnboardingAction::QuietStart, available ? value : "N/A", available && !s_command_pending);
            }
            if (error == ESP_OK) {
                if (available) format_quiet_time(clock.quiet_end_minute, value);
                error = append_quiet_row(OnboardingAction::QuietEnd, available ? value : "N/A", available && !s_command_pending);
            }
            if (error == ESP_OK && equal_disabled) {
                error = prepare_caption("Times must differ", 320, s_view.lines[s_view.line_count]);
                if (error == ESP_OK) { s_view.instant_lines[s_view.line_count] = true; ++s_view.line_count; }
            }
        } else {
            format_quiet_time(s_quiet_draft, value);
            error = append_quiet_row(OnboardingAction::QuietHour, value, available && !s_command_pending);
            if (error == ESP_OK) error = append_quiet_row(OnboardingAction::QuietMinute, value, available && !s_command_pending);
            const bool equal = available && clock.quiet_hours_enabled && s_quiet_draft ==
                (s_quiet_edit_start ? clock.quiet_end_minute : clock.quiet_start_minute);
            if (error == ESP_OK) error = append_quiet_row(OnboardingAction::QuietSave, nullptr,
                available && !equal && !s_command_pending);
            if (error == ESP_OK && equal) {
                error = prepare_caption("Times must differ", 320, s_view.lines[s_view.line_count]);
                if (error == ESP_OK) { s_view.instant_lines[s_view.line_count] = true; ++s_view.line_count; }
            }
        }
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    LUNASHELL::CodyOptionsObservation loaded;
    if (captured == nullptr) {
        error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(loaded);
        if (error != ESP_OK) {
            return error;
        }
        captured = &loaded;
    }
    const auto &observation = *captured;
    struct Row {
        PreferenceId identity;
        const char *label;
        CODY2D::ActionIcon icon;
        uint16_t color;
    };
    using Icon = CODY2D::ActionIcon;
    static constexpr Row display_rows[] = {
        {PreferenceId::Brightness, "Brightness", Icon::PlanPro, Theme::accent_warm},
        {PreferenceId::AutoRotate, "Auto-rotate", Icon::Rotate, Theme::secondary_default},
        {PreferenceId::Orientation, "Orientation", Icon::Orientation, Theme::accent_cyan},
    };
    static constexpr Row ring_rows[] = {
        {PreferenceId::AssistantQuotaRing, "Assistant quota", Icon::Statistics, Theme::secondary_default},
        {PreferenceId::BatteryRing, "Battery ring", Icon::PlanPro, Theme::accent_cyan},
    };
    static constexpr Row time_rows[] = {
        {PreferenceId::AutoTime, "Auto time", Icon::Clock, Theme::accent_warm},
    };
    static constexpr Row synth_rows[] = {
        {PreferenceId::SynthEnabled, kDeviceFirstSetup ? "Enable Synth Voice" : "Synth voice", Icon::Sound, Theme::accent_warm},
        {PreferenceId::SynthVolume, "Voice volume", Icon::Sound, Theme::accent_warm},
        {PreferenceId::SynthVoice, "Voice type", Icon::Sound, Theme::accent_warm},
    };
    static constexpr Row usage_row{PreferenceId::UsageMode, "Usage", Icon::Home, Theme::accent_cyan};
    static constexpr Row more_behavior_rows[] = {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        usage_row,
#endif
        {PreferenceId::Screensaver, "Screensaver", Icon::Motion, Theme::primary_muted},
    };
    static constexpr Row behavior_rows[] = {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        {PreferenceId::Dictation, "Dictation", Icon::Microphone, Theme::primary_muted},
#endif
        {PreferenceId::ShakeWake, "Shake sleep/wake", Icon::Motion, Theme::secondary_default},
        {PreferenceId::Sounds, "Sounds", Icon::Sound, Theme::accent_warm},
#if !CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        usage_row,
#endif
    };
    static_assert(std::size(display_rows) + 1U <= kPreferencesPerPage);
    static_assert(std::size(synth_rows) <= kPreferencesPerPage);
    static_assert(std::size(behavior_rows) + 1U <= kPreferencesPerPage);
    static_assert(std::size(more_behavior_rows) + 2U <= kPreferencesPerPage);
    const Row *rows = behavior_rows;
    size_t row_count = std::size(behavior_rows);
    if (s_options_page == OptionsPage::Display) {
        rows = display_rows;
        row_count = std::size(display_rows);
    } else if (s_options_page == OptionsPage::DisplayRings) {
        rows = ring_rows;
        row_count = std::size(ring_rows);
    } else if (s_options_page == OptionsPage::Time) {
        rows = time_rows;
        row_count = std::size(time_rows);
    } else if (s_options_page == OptionsPage::SynthVoice) {
        rows = synth_rows;
        row_count = std::size(synth_rows);
    } else if (s_options_page == OptionsPage::BehaviorMore) {
        rows = more_behavior_rows;
        row_count = std::size(more_behavior_rows);
    }
    const uint16_t first_row_top = kPreferenceTop;
    const auto &font = SceneData::body_font;
    s_presented_options = observation;
    for (size_t index = 0; index < row_count; ++index) {
        const auto &row = rows[index];
        auto &target = s_view.preferences[s_view.preference_count];
        target = {};
        target.identity = row.identity;
        const uint16_t top = first_row_top + s_view.preference_count * kPreferenceStep;
        const auto projected = project_preference(row.identity, observation);
        const bool available = projected.available;
        const bool active = projected.active;
        const char *value = "N/A";
        target.requested_value = active ? 0U : 1U;
        if (available) {
            value = active ? "On" : "Off";
            if (row.identity == PreferenceId::UsageMode) {
                value = active ? "Away" : "Desk";
            }
            if (row.identity == PreferenceId::Orientation) {
                if (active) {
                    value = "Auto";
                } else {
                    std::snprintf(target.value_text, sizeof(target.value_text), "%u",
                        static_cast<unsigned>(projected.value));
                    value = target.value_text;
                }
                target.requested_value = projected.value == 0U ? 180U : 0U;
            }
            if (row.identity == PreferenceId::Brightness) {
                const uint32_t percent = projected.value;
                std::snprintf(target.value_text, sizeof(target.value_text), "%lu%%",
                    static_cast<unsigned long>(percent));
                value = target.value_text;
                target.requested_value = percent == 10U ? 100U : std::max<uint32_t>(10U, percent - 10U);
            }
        }
        const bool manual_locked = row.identity == PreferenceId::Orientation && active;
        target.control = {{kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight}, {},
            static_cast<uint16_t>(active && available ? Theme::controls_primary_top : Theme::controls_secondary_top),
            static_cast<uint16_t>(active && available ? Theme::controls_primary_bottom : Theme::controls_secondary_bottom),
            available && !manual_locked && !s_command_pending};
        target.control.corner_radius = 12;
        target.control.border_color = active && available ? Theme::controls_primary_border : Theme::controls_secondary_border;
        target.control.icon = kDeviceFirstSetup ? row.icon : Icon::None;
        target.control.icon_color = available ? row.color : kPairingCaption;
        target.control.icon_center = {static_cast<int16_t>(kPreferenceLeft + 18U),
            static_cast<int16_t>(top + kPreferenceHeight / 2U)};
        const int16_t text_top = static_cast<int16_t>(top +
            (kDeviceFirstSetup ? 2U : (kPreferenceHeight - font.height) / 2U));
        error = CODY2D::prepare_text_line(font, {row.label, static_cast<uint16_t>(std::strlen(row.label))},
            {static_cast<int16_t>(kPreferenceLeft + (kDeviceFirstSetup ? 48U : 12U)), text_top},
            kPairingTitle, target.control.caption);
        const uint16_t count = static_cast<uint16_t>(std::strlen(value));
        const uint16_t value_width = (count - 1U) * font.advance + font.width;
        if (error == ESP_OK) {
            error = CODY2D::prepare_text_line(font, {value, count},
                {static_cast<int16_t>(kDeviceFirstSetup ? kPreferenceLeft + 48U :
                    kPreferenceLeft + kPreferenceWidth - 8U - value_width),
                 static_cast<int16_t>(kDeviceFirstSetup ? top + 28U : text_top)},
                available && active ? Theme::secondary_default : kPairingCaption, target.value);
        }
        if (error != ESP_OK) {
            return error;
        }
        ++s_view.preference_count;
    }
    if (s_options_page == OptionsPage::Display && kDeviceFirstSetup) {
        error = append_options_action(OnboardingAction::MoreDisplay, !s_command_pending);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (s_options_page == OptionsPage::DisplayRings && kDeviceFirstSetup) {
        auto &target = s_view.preferences[s_view.preference_count];
        target = {};
        target.action = OnboardingAction::OpenTime;
        CODY2D::TextLine unused{};
        const uint16_t top = kPreferenceTop + s_view.preference_count * kPreferenceStep;
        error = prepare_button(target.action, "Time", {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight},
            !s_command_pending, false, target.control, unused);
        if (error == ESP_OK) error = prepare_menu_row("Time", {Icon::Clock, Theme::accent_warm, ""},
            target.control, target.value);
        if (error != ESP_OK) return error;
        ++s_view.preference_count;
    }
    if (s_options_page == OptionsPage::Time && kDeviceFirstSetup) {
        const auto snapshot_error = LUNASHELL::DeviceSnapshotService::instance().readCodexPresenceStatus(s_pc_time_observation);
        const auto &link = s_pc_time_observation.codex_link;
        const bool connected = snapshot_error == ESP_OK && link.data_valid && !link.data_stale &&
            link.last_error == ESP_OK && s_pc_time_observation.codex_link_state == LUNASHELL::CodexLinkState::Connected;
        s_presented_pc_time_connected = connected;
        s_presented_pc_time_state = snapshot_error == ESP_OK ? s_pc_time_observation.pc_time_state : LUNASHELL::PcTimeSyncState::Failed;
        s_presented_pc_time_error = snapshot_error == ESP_OK ? s_pc_time_observation.pc_time_error : snapshot_error;
        const auto state = s_presented_pc_time_state;
        const auto failure = s_manual_pc_time_error != ESP_OK ? s_manual_pc_time_error : s_presented_pc_time_error;
        const char *status = snapshot_error != ESP_OK ? "Unavailable" : !connected ? "Connect PC" :
            state == LUNASHELL::PcTimeSyncState::Pending ? "Syncing..." :
            failure == ESP_ERR_NOT_SUPPORTED ? "Not supported" : failure != ESP_OK || state == LUNASHELL::PcTimeSyncState::Failed ?
            "Sync failed" : state == LUNASHELL::PcTimeSyncState::Applied ? "Synchronized" : "Ready";
        auto &target = s_view.preferences[s_view.preference_count];
        target = {};
        target.action = OnboardingAction::SyncPcTime;
        const uint16_t top = kPreferenceTop + s_view.preference_count * kPreferenceStep;
        CODY2D::TextLine unused{};
        error = prepare_button(target.action, "Sync time", {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight},
            connected && state != LUNASHELL::PcTimeSyncState::Pending && !s_command_pending, false, target.control, unused);
        if (error == ESP_OK) {
            target.control.icon = Icon::Clock;
            target.control.icon_color = Theme::accent_warm;
            target.control.icon_center = {static_cast<int16_t>(kPreferenceLeft + 18U), static_cast<int16_t>(top + kPreferenceHeight / 2U)};
            error = CODY2D::prepare_text_line(font, {"Sync time", 9},
                {static_cast<int16_t>(kPreferenceLeft + 48U), static_cast<int16_t>(top + 2U)}, kPairingTitle, target.control.caption);
        }
        if (error == ESP_OK) error = CODY2D::prepare_text_line(font, {status, static_cast<uint16_t>(std::strlen(status))},
            {static_cast<int16_t>(kPreferenceLeft + 48U), static_cast<int16_t>(top + 28U)}, kPairingCaption, target.value);
        if (error != ESP_OK) return error;
        ++s_view.preference_count;
    }
    if (s_options_page == OptionsPage::DisplayRings) {
        error = append_options_action(OnboardingAction::ScheduleBirthIntro, !s_command_pending && !s_birth_intro_scheduled);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (s_options_page == OptionsPage::Time) return prepare_action(OnboardingAction::None, false);
    if (s_options_page == OptionsPage::Behavior) {
        if (!kDeviceFirstSetup) return prepare_action(OnboardingAction::MoreBehavior, !s_command_pending);
        error = append_options_action(OnboardingAction::MoreBehavior, !s_command_pending);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (s_options_page == OptionsPage::BehaviorMore) {
        s_view.menu_page = 1;
        s_view.menu_page_count = 2;
        if (kDeviceFirstSetup) {
            auto &target = s_view.preferences[s_view.preference_count];
            target = {};
            target.action = OnboardingAction::AudioLevels;
            const uint16_t top = kPreferenceTop + s_view.preference_count * kPreferenceStep;
            CODY2D::TextLine unused;
            error = prepare_button(target.action, "Audio", {kPreferenceLeft, top, kPreferenceWidth, kPreferenceHeight},
                !s_command_pending, false, target.control, unused);
            if (error == ESP_OK) {
                error = prepare_menu_row("Audio", menu_appearance(target.action), target.control, target.value);
            }
            if (error != ESP_OK) return error;
            ++s_view.preference_count;
        }
        if (!kDeviceFirstSetup) return prepare_action(OnboardingAction::MoreBehaviorSettings, !s_command_pending);
        error = append_options_action(OnboardingAction::MoreBehaviorSettings, !s_command_pending);
        return error == ESP_OK ? prepare_action(OnboardingAction::None, false) : error;
    }
    if (s_options_page == OptionsPage::SynthVoice) {
        return prepare_action(OnboardingAction::None, false);
    }
    return prepare_action(OnboardingAction::Done, !s_command_pending);
}

// scene: Active Options frame, with the existing intro and shared control bounds.
// page: Explicit options menu or settings category to present.
// Exception: Returns the original settings, layout or scene-content error.
esp_err_t show_options_page(SceneFrame &scene, OptionsPage page)
{
    s_options_page = page;
    ++s_navigation_revision;
    const esp_err_t error = prepare_options_content(scene);
    return error == ESP_OK ? attach_content(scene) : error;
}

// scene: Visible factory reset confirmation with a completed explicit touch.
// Exception: Presents the original admission failure or returns a layout error.
esp_err_t confirm_factory_reset(SceneFrame &scene)
{
    s_factory_reset_request_id = 0;
    s_factory_reset_error = LUNASHELL_power_manager_request_factory_reset(&s_factory_reset_request_id);
    if (s_factory_reset_error != ESP_OK) {
        ESP_LOGE(kTag, "Factory reset request rejected (%s)", esp_err_to_name(s_factory_reset_error));
        return show_options_page(scene, OptionsPage::ResetFailed);
    }
    return show_options_page(scene, OptionsPage::ResetPending);
}

// scene: Active reset progress screen, with no enabled destructive actions.
// changed: Receives whether a new reset result or status error needs a frame.
// Exception: Returns an original native layout error.
esp_err_t update_factory_reset(SceneFrame &scene, bool &changed)
{
    LUNASHELL::PowerManagementSnapshot power{};
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readPowerState(power);
    if (error == ESP_OK && power.factory_reset.request_id == s_factory_reset_request_id &&
        !power.factory_reset.pending) {
        // Successful reset restarts the device. Only a failed result should reach this screen.
        s_factory_reset_error = power.factory_reset.error != ESP_OK ? power.factory_reset.error
            : ESP_ERR_INVALID_RESPONSE;
        ESP_LOGE(kTag, "Factory reset finished without restart (%s)", esp_err_to_name(s_factory_reset_error));
        s_factory_reset_request_id = 0;
        changed = true;
        return show_options_page(scene, OptionsPage::ResetFailed);
    }
    if (error == s_factory_reset_error) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Factory reset status unavailable (%s)", esp_err_to_name(error));
    }
    s_factory_reset_error = error;
    changed = true;
    return show_options_page(scene, OptionsPage::ResetPending);
}

// line: Fully wrapped shared-plan line, copied into presentation-owned storage before drawing.
// Exception: Returns exact native text/capacity/geometry errors without changing the shared plan.
esp_err_t append_event_line(const event_presentation::Line &line)
{
    const auto &font = SceneData::body_font;
    const size_t count = std::strlen(line.text);
    if (count == 0 || count >= kDebugLineBytes || s_view.line_count >= kContentLineCapacity ||
        line.top + font.height > 419) {
        ESP_LOGE(kTag, "Shared event line bounds rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t index = s_view.line_count;
    auto &buffer = s_view.debug_text[index];
    std::memcpy(buffer, line.text, count + 1U);
    const uint16_t width = static_cast<uint16_t>((count - 1U) * font.advance + font.width);
    const uint16_t marker_space = line.chat_waiting ? event_presentation::kChatWaitingDotSpace : 0;
    const uint16_t group_width = width + marker_space;
    const int horizontal = (group_width + 1U) / 2U;
    const int vertical = static_cast<int>(line.top + font.height - 1U) - 233;
    if (horizontal * horizontal + vertical * vertical > 233 * 233) {
        ESP_LOGE(kTag, "Shared event line exceeds circular chord (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t error = CODY2D::prepare_text_line(font, {buffer, static_cast<uint16_t>(count)},
        {static_cast<int16_t>((466U - group_width) / 2U + marker_space), static_cast<int16_t>(line.top)},
        event_presentation::line_color(line, 0), s_view.lines[index]);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Shared event line geometry failed (%s)", esp_err_to_name(error));
        return error;
    }
    s_view.instant_lines[index] = line.instant;
    if (line.chat_waiting) { s_view.chat_waiting = true; }
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
    navigation.choice_page = s_event_choice_page;
    navigation.detail_page = s_event_detail_page;
    return navigation;
}

// No parameters: projects actual canonical observation and previous owner admissions.
event_presentation::Context event_context()
{
    return {s_event_online, s_event_reply_admitted, s_event_dismiss_admitted,
        s_event_observation_error, s_event_ui_error, s_event_retry,
        s_event_selection_target[0] != '\0' || s_presented_event.selection_request_id[0] != '\0'};
}

// navigation: Local reversible presentation state accepted by the shared model.
// effect: Accepted model intent; reversible navigation clears only local review errors.
void retain_event_navigation(const event_presentation::Navigation &navigation,
                             event_presentation::Effect effect = event_presentation::Effect::None)
{
    s_event_stage = navigation.stage;
    std::memcpy(s_event_selected_id, navigation.selected_id, sizeof(s_event_selected_id));
    s_event_failure_page = navigation.failure_page;
    s_event_choice_page = navigation.choice_page;
    s_event_detail_page = navigation.detail_page;
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

// scene: Event-owned frame drawing the same bounded semantic plan, title and accent as the native host.
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
    if (error == ESP_OK && s_event_plan.route == Route::ApprovalOnPc) {
        error = prepare_approval_notice(s_approval_notice, font, s_presented_event.body);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Approval notice preparation failed (%s)", esp_err_to_name(error));
            return error;
        }
        s_view.approval_notice = &s_approval_notice;
    }
    s_view.action_visible = false;
    for (uint8_t index = 0; error == ESP_OK && index < s_event_plan.action_count; ++index) {
        const auto &action = s_event_plan.actions[index];
        if (action.id == event_presentation::ActionId::Choose ||
            action.id == event_presentation::ActionId::SelectQuestion) {
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
        if (error == ESP_OK) { scene.title.color = s_event_plan.title_color; }
    }
    return error;
}

// base: Registered scene whose identity and standby policy are retained.
// title_top: Heading position leaving clearance for the route's outline glyph.
SceneDefinition with_pairing_theme(SceneDefinition base, uint16_t title_top)
{
    base.background_rgb565 = kPairingBackground;
    base.title_color = kPairingTitle;
    base.title_top = title_top;
    return base;
}

// route: Device-first setup or Options identity.
// base: Registered definition with static lifetime.
const SceneDefinition &device_scene_definition(Route route, const SceneDefinition &base)
{
    switch (route) {
    case Route::SetupMenu: {
        static const auto definition = [&base] {
            auto connections = with_pairing_theme(base, kOptionsTitleTop);
            connections.title = {"Connections", 11};
            connections.title_visible = true;
            return connections;
        }();
        return definition;
    }
    case Route::Hello04: {
        static const auto definition = with_pairing_theme(base, 180);
        return definition;
    }
    case Route::CodexReplace: {
        static const auto definition = with_pairing_theme(base, 126);
        return definition;
    }
    case Route::Online09: {
        static const auto definition = with_pairing_theme(base, kWifiStatusTitleTop);
        return definition;
    }
    case Route::Invitation05: {
        static const auto definition = with_pairing_theme(base, kGuideTitleTop);
        static const auto home_definition = with_pairing_theme(base, 82);
        static const auto menu_definition = [&base] {
            auto menu = with_pairing_theme(base, 82);
            menu.title_visible = false; // The menu content owns the heading viewport.
            return menu;
        }();
        if (s_home_wifi_flow && !s_home_wifi_intro) return menu_definition;
        return s_home_wifi_flow ? home_definition : definition;
    }
    case Route::Install06: {
        static const auto definition = with_pairing_theme(base, 82);
        static const auto home_definition = with_pairing_theme(base, 82);
        return s_home_wifi_flow ? home_definition : definition;
    }
    case Route::Pairing07: {
        static const auto definition = [&base] {
            auto pairing = with_pairing_theme(base, 82);
            // Keep the device active through the service deadline, with time
            // for radio startup and teardown before automatic standby.
            pairing.standby_override_seconds = LUNASHELL_PAIRING_SESSION_TIMEOUT_SECONDS + 30U;
            return pairing;
        }();
        static const auto home_definition = [&base] {
            auto pairing = with_pairing_theme(base, 82);
            pairing.standby_override_seconds = LUNASHELL_PAIRING_SESSION_TIMEOUT_SECONDS + 30U;
            return pairing;
        }();
        return s_home_wifi_flow ? home_definition : definition;
    }
    case Route::ConnectCodex12: {
        static const auto definition = with_pairing_theme(base, kCodexPairingTitleTop);
        return definition;
    }
    case Route::LookingForPc14: {
        static const auto definition = with_pairing_theme(base, kCodexPairingTitleTop);
        return definition;
    }
    case Route::MakeItYours10: {
        static const auto definition = [&base] {
            auto options = base;
            options.title_top = kOptionsTitleTop;
            return options;
        }();
        return definition;
    }
    case Route::NetworkWait: {
        if (base.id == SceneId::NetworkWait) {
            static const auto definition = with_pairing_theme(base, 180);
            return definition;
        }
        static const auto passive_definition = with_pairing_theme(base, kWifiStatusTitleTop);
        return passive_definition;
    }
    default:
        return base;
    }
}

// wifi: Canonical radio state for the provisioning result.
bool wifi_online_for_pairing(const LUNASHELL::WifiSnapshot &wifi)
{
    return wifi.data_valid && !wifi.data_stale && wifi.health == LUNASHELL::DeviceHealth::Ready &&
        wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
}

// wifi: Canonical observation used for status and network identity.
// scene: Prepared route receiving the provisioning result.
// Exception: Returns the original native title, caption or action preparation error.
esp_err_t prepare_wifi_result(const LUNASHELL::WifiSnapshot &wifi, SceneFrame &scene)
{
    const bool observed = wifi.data_valid && !wifi.data_stale && wifi.connection_state_valid;
    const bool online = wifi_online_for_pairing(wifi);
    const char *title = online ? "Wi-Fi connected" : observed ? "Wi-Fi offline" : "Wi-Fi unavailable";
    auto error = set_scene_title(scene, {title, static_cast<uint16_t>(std::strlen(title))});
    if (error != ESP_OK) { return error; }
    s_view.header_icon = online ? CODY2D::ActionIcon::Wifi : CODY2D::ActionIcon::WifiOff;
    s_view.header_icon_bounds = online ? kWifiConnectingGlyph : kOfflineGlyph;
    s_view.header_icon_color = online ? kWifiReadyMint : kOfflineAmber;
    const char *detail = online ? "Ready for the next step" : "Check Wi-Fi setup";
    error = prepare_caption(detail, kWifiStatusDetailTop, s_view.lines[0], 300);
    if (error != ESP_OK) { return error; }
    s_view.lines[0].color = kPairingCaption;
    error = prepare_network_name(wifi, kWifiStatusNetworkTop, 1);
    return error == ESP_OK ? prepare_action(OnboardingAction::Continue, true) : error;
}

// route: Explicit atlas identity; no navigation is inferred from array position.
// scene: Receives a fully validated immutable layout and caller-owned content.
// wifi: Coherent radio observation required by the device-first connection status pages.
// Exception: Returns the real scene, catalog, text or content preparation error.
esp_err_t enter_route(Route route, SceneFrame &scene, const LUNASHELL::WifiSnapshot *wifi = nullptr)
{
    const bool needs_wifi = route == Route::Online09 || route == Route::CodexReplace ||
        (route == Route::NetworkWait && s_startup_network_passive);
    if (kDeviceFirstSetup && needs_wifi && wifi == nullptr) {
        ESP_LOGE(kTag, "Connection route %u requires a Wi-Fi observation", to_index(route));
        return ESP_ERR_INVALID_ARG;
    }
    SceneId identity;
    switch (route) {
    case Route::Hello04:
        identity = kDeviceFirstSetup ? SceneId::ConnectionLost : SceneId::Greeting;
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
    case Route::DecisionQueue:
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
        // Reuse the connection scene for passive startup,
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
        if (kDeviceFirstSetup || route == Route::Install06 || route == Route::Pairing07 ||
            (s_home_wifi_flow && route == Route::Invitation05 && !s_home_wifi_intro)) {
            definition = &device_scene_definition(route, *definition);
        }
        error = begin_scene(*definition, prepared);
    }
    if (error != ESP_OK) {
        return error;
    }
    // QR state is retired on every departure, including cancellation and failure.
    wipe(&s_view, sizeof(s_view));
    s_swipe_active = false;
    ++s_navigation_revision;
    ++s_text_generation;
    if (route == Route::MakeItYours10) {
        s_options_page = OptionsPage::Menu;
        s_connections_from_options = false;
    }
    s_route = route;
    if (route == Route::SetupMenu) {
        if (kDeviceFirstSetup) {
            s_view.action_icon_size = kConnectionIconSize;
        }
        if (!s_connections_from_options) {
            s_options_from_setup = false;
        }
    }
    if (route == Route::Hello04 && !kDeviceFirstSetup) {
        s_hello_completed_at_us = -1;
        scene = prepared;
        ESP_LOGI(kTag, "Atlas route entered: %s", definition->name);
        return ESP_OK;
    }
    s_hello_waits_for_touch = false;
    if (route == Route::Hello04) {
        static constexpr char title[] = "Not connected";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        if (error == ESP_OK && kDeviceFirstSetup) {
            s_view.header_icon = CODY2D::ActionIcon::WifiOff;
            s_view.header_icon_bounds = kOfflineGlyph;
            const bool saved = wifi != nullptr && wifi->credentials_configured;
            error = prepare_caption(saved ? "Previous Wi-Fi" : "Connect Cody to Wi-Fi", 223, s_view.lines[0]);
            s_view.lines[0].color = kPairingCaption;
            s_view.line_count = 1;
            if (error == ESP_OK && saved) {
                error = prepare_network_name(*wifi, 252, 1);
            }
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::OpenPairingGuide, true);
        }
    } else if (is_codex_event_route(route)) {
        error = prepare_event_content(prepared);
    } else if (s_home_wifi_flow && route == Route::Invitation05 && !s_home_wifi_intro) {
        error = set_scene_title(prepared, {wifi_menu_art::kHeadingText, sizeof(wifi_menu_art::kHeadingText) - 1U});
        if (error == ESP_OK) error = LUNASHELL::DeviceSnapshotService::instance()
            .readHardwareStatus(s_connection_status);
        if (error == ESP_OK) {
            s_view.wifi_menu = true;
            update_wifi_menu_name(s_view, s_connection_status.ble);
            error = prepare_action(OnboardingAction::PairCody, true);
        }
        if (error == ESP_OK) error = prepare_alternative(OnboardingAction::LocalWifi, true);
    } else if (s_home_wifi_flow && route == Route::Invitation05) {
        static constexpr char title[] = "Bluetooth setup";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        if (error == ESP_OK) {
            const char *steps[] = {"1. Get Espressif app", "2. Scan Cody QR in app", "3. Choose your Wi-Fi", "TAP BELOW TO CHOOSE"};
            const uint16_t tops[] = {140, 180, 220, 270};
            for (uint8_t index = 0; index < 4; ++index) {
                error = prepare_caption(steps[index], tops[index], s_view.lines[index], 300);
                if (error != ESP_OK) break;
                ++s_view.line_count;
            }
        }
        if (error == ESP_OK) {
            s_view.platform_choices = true;
            s_view.action_icon_size = 1; // Rectangular hit bounds; logos are drawn above.
            s_view.action_id = OnboardingAction::AppleApp;
            s_view.action_visible = true;
            s_view.action = {kIntroApple, {}, 0, 0, true};
            s_view.alternative_id = OnboardingAction::AndroidApp;
            s_view.alternative = {kIntroAndroid, {}, 0, 0, true};
        }
    } else if (s_home_wifi_flow && route == Route::Install06) {
        static constexpr char title[] = "Get the app";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        s_view.qr_bounds = kLargeQrBounds;
    } else if (route == Route::Invitation05) {
        static constexpr const char *title = kDeviceFirstSetup ? "Wi-Fi setup" : "Get your phone";
        error = set_scene_title(prepared, {title, static_cast<uint16_t>(std::strlen(title))});
        if (error != ESP_OK) {
            return error;
        }
        if (kDeviceFirstSetup) {
            s_view.header_icon = CODY2D::ActionIcon::DownloadPhone;
            s_view.header_icon_bounds = kGuideGlyph;
        }
        constexpr const char *captions[] = {
            kDeviceFirstSetup ? "Get the Espressif app" : "Scan next QR for app.",
            kDeviceFirstSetup ? "Then open it and scan" : "Then scan pairing QR",
            kDeviceFirstSetup ? "the pairing QR." : "in the Espressif app.",
        };
        const uint16_t caption_top = kDeviceFirstSetup ? kGuideCaptionTop : 242;
        for (const char *text : captions) {
            error = prepare_caption(text, caption_top + s_view.line_count * 26U, s_view.lines[s_view.line_count]);
            if (error != ESP_OK) {
                return error;
            }
            if (kDeviceFirstSetup) {
                s_view.lines[s_view.line_count].color = kPairingCaption;
            }
            ++s_view.line_count;
        }
        error = prepare_action(kDeviceFirstSetup ? OnboardingAction::PairCody : OnboardingAction::ShowQr, true);
        if (error == ESP_OK && kDeviceFirstSetup) {
            error = prepare_alternative(OnboardingAction::ShowQr, true);
        }
    } else if (route == Route::Install06) {
        // The old decorative glyph occupied the common status band's space.
        s_view.qr_bounds = kLargeQrBounds;
    } else if (route == Route::Pairing07) {
        if (s_home_wifi_flow) {
            static constexpr char title[] = "Pair Cody";
            error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        }
        s_view.qr_bounds = kSessionQrBounds;
        if (error == ESP_OK) error = prepare_caption("Starting Bluetooth", s_home_wifi_flow ? 130 : 222, s_view.lines[0]);
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
        if (kDeviceFirstSetup) {
            error = prepare_wifi_result(*wifi, prepared);
        } else {
            error = prepare_caption("Wi-Fi connected", 300, s_view.lines[0]);
            s_view.line_count = 1;
            if (error == ESP_OK) {
                error = prepare_action(OnboardingAction::Continue, true);
            }
        }
    } else if (route == Route::MakeItYours10) {
        s_preference_error = ESP_OK;
        error = prepare_options_content(prepared);
    } else if (route == Route::CodexReplace) {
        const char *title = s_codex_manage_repair ? "Pairing error" :
            s_codex_manage_pending ? "Pairing pending" : "Saved PC";
        error = set_scene_title(prepared, {title, static_cast<uint16_t>(std::strlen(title))});
        if (error == ESP_OK && kDeviceFirstSetup) {
            s_view.header_icon = CODY2D::ActionIcon::Link;
            s_view.header_icon_bounds = {209, 66, 48, 48};
            s_view.header_icon_color = s_codex_manage_repair ? kOfflineAmber : Theme::primary_muted;
            s_view.action_icon_size = 36;
            const char *detail = s_codex_manage_repair ? "Forget to repair pairing" :
                s_codex_manage_pending ? "Finish pairing or re-pair" : "PC name unavailable";
            error = prepare_caption(detail, 174, s_view.lines[0], 300);
            if (error == ESP_OK) {
                s_view.lines[0].color = kPairingCaption;
                error = prepare_caption("Wi-Fi network", 205, s_view.lines[1], 300);
                s_view.lines[1].color = kPairingCaption;
            }
            if (error == ESP_OK) { error = prepare_network_name(*wifi, 235, 2); }
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::ReplacePairing, !s_codex_manage_repair);
        }
        if (error == ESP_OK) {
            error = prepare_alternative(OnboardingAction::KeepPairing, !s_codex_manage_repair);
        }
        if (error == ESP_OK) {
            CODY2D::TextLine unused;
            s_view.options_id = OnboardingAction::ForgetPairing;
            error = kDeviceFirstSetup
                ? prepare_saved_pc_action(s_view.options_id, true, s_view.options, unused)
                : prepare_button(s_view.options_id, "Forget", kOptionsBounds, true, false, s_view.options, unused);
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
        static constexpr char title[] = "Wi-Fi connecting";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        if (error == ESP_OK) {
            if (kDeviceFirstSetup) {
                error = prepare_caption("Joining network", kWifiStatusDetailTop, s_view.lines[0], 300);
                s_view.lines[0].color = kPairingCaption;
                if (error == ESP_OK) { error = prepare_network_name(*wifi, kWifiStatusNetworkTop, 1); }
            } else {
                error = prepare_caption("Joining saved network", 300, s_view.lines[0]);
                s_view.line_count = 1;
            }
        }
    } else if (route == Route::CodexChoice || route == Route::SetupMenu || route == Route::NetworkWait) {
        const auto primary = route == Route::CodexChoice ? OnboardingAction::DeclineCodex :
            route == Route::SetupMenu ? OnboardingAction::NetworkSetup : OnboardingAction::Later;
        const auto alternative = route == Route::CodexChoice ? OnboardingAction::AcceptCodex :
            route == Route::SetupMenu ? OnboardingAction::ConnectCodex : OnboardingAction::NetworkSetup;
        if (kDeviceFirstSetup && route == Route::SetupMenu) {
            error = prepare_caption("Choose what to connect", kConnectionsHintTop, s_view.lines[0], 300);
            s_view.lines[0].color = kPairingCaption;
            s_view.line_count = 1;
        } else if (kDeviceFirstSetup && route == Route::NetworkWait) {
            s_view.header_icon = CODY2D::ActionIcon::WifiOff;
            s_view.header_icon_bounds = kOfflineGlyph;
            error = prepare_caption("Connect Cody to Wi-Fi", 223, s_view.lines[0]);
            s_view.lines[0].color = kPairingCaption;
            s_view.line_count = 1;
        }
        if (error == ESP_OK) {
            error = prepare_action(primary, true);
        }
        if (error == ESP_OK) {
            error = prepare_alternative(alternative,
                alternative != OnboardingAction::ConnectCodex || kCodexIntegrationEnabled);
        }
        if (error == ESP_OK && route == Route::SetupMenu && !kDeviceFirstSetup) {
            CODY2D::TextLine unused;
            s_view.options_id = OnboardingAction::Options;
            error = prepare_button(OnboardingAction::Options, "Options", kSetupOptionsBounds,
                true, false, s_view.options, unused);
        }
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
        if (error == ESP_OK && route == Route::SetupMenu && kDeviceFirstSetup) {
            s_relay_attempted = false;
            s_relay_result = {};
            error = prepare_relay_footer();
        }
#endif
    } else if (route == Route::ConnectCodex12) {
        static constexpr char title[] = "Pair Codex";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        const bool code_ready = s_codex_code[0] != '\0';
        if (error == ESP_OK && kDeviceFirstSetup) {
            error = prepare_device_pairing_content(code_ready);
        } else if (error == ESP_OK) {
            error = prepare_caption(code_ready ? "Share code in Codex" : "Creating pairing code",
                300, s_view.lines[0]);
            s_view.line_count = 1;
        }
        if (error == ESP_OK && !kDeviceFirstSetup && code_ready) {
            error = prepare_codex_code(s_view.lines[1]);
            s_view.instant_lines[1] = true;
            s_view.line_count = 2;
        } else if (error == ESP_OK && !kDeviceFirstSetup) {
            error = prepare_caption("One moment", 327, s_view.lines[1]);
            if (error == ESP_OK) {
                error = prepare_caption("About 30 seconds", 354, s_view.lines[2]);
            }
            s_view.line_count = 3;
        }
        if (error == ESP_OK) {
            error = prepare_action(code_ready ? OnboardingAction::Cancel : OnboardingAction::None, code_ready);
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
        static constexpr char title[] = "Waiting for PC";
        error = set_scene_title(prepared, {title, sizeof(title) - 1U});
        s_view.line_count = 1;
        if (error == ESP_OK) {
            error = prepare_caption("Open Codex on your PC", kPcWaitCaptionTop, s_view.lines[0]);
        }
        if (error == ESP_OK) {
            error = prepare_caption("Pairing stays saved", kPcWaitDetailTop, s_view.lines[1]);
            if (kDeviceFirstSetup) {
                s_view.lines[1].color = kPairingCaption;
            }
            s_view.line_count = 2;
        }
        if (error == ESP_OK) {
            error = prepare_action(OnboardingAction::Later, true);
        }
    } else if (route == Route::Connected15) {
        error = prepare_caption(kCodexConnectedCaption, 300, s_view.lines[0]);
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
    // Preserve the scroll position while discarding time spent on a preempting route.
    s_command_hints.sampled_us = -1;
    scene = prepared;
    ESP_LOGI(kTag, "Atlas route entered: %s", definition->name);
    return ESP_OK;
}

// scene: Receives the installation page while the action worker prepares its public QR.
// Exception: Returns the original scene or QR work admission error.
esp_err_t open_installation_qr(SceneFrame &scene)
{
    const auto error = enter_route(Route::Install06, scene);
    return error == ESP_OK ? enqueue(s_home_wifi_flow && s_home_wifi_apple ? Work::InstallQrApple : Work::InstallQr, 0) : error;
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
    s_event_selection_target[0] = '\0';
    s_event_failure_page = s_event_choice_page = s_event_detail_page = 0;
    s_checks_passed_readable_at_us = -1;
    s_approval_notice.chat[0] = '\0';
    s_approval_notice.length = 0;
    s_approval_notice.readable_at_us = -1;
}

// No parameters: projects the retained owner snapshot and reversible navigation into a route.
Route codex_event_destination()
{
    return event_presentation::destination(s_presented_event, s_event_stage,
                                           s_event_reply_admitted, s_event_observation_error);
}

// scene: Display-owned frame rebuilt from the retained canonical event, never from transport I/O.
// preserve_timing: Keeps the reading and scrolling clocks on the same route.
// Exception: Returns original route/layout errors.
esp_err_t render_codex_event(SceneFrame &scene, bool preserve_timing = false)
{
    // A real question/notification owns this view; its dwell cannot be charged as observed work.
    pause_activity_observation(s_activity_observation);
    s_swipe_active = false;
    const Route destination = codex_event_destination();
    const bool keep_time = preserve_timing && destination == s_route;
    const uint64_t previous_text_elapsed_us = scene.text_elapsed_us;
    const uint32_t previous_text_generation = scene.content.text_generation;
    esp_err_t error = enter_route(destination, scene);
    if (error == ESP_OK && keep_time) {
        scene.text_elapsed_us = previous_text_elapsed_us;
        s_text_generation = previous_text_generation;
        error = attach_content(scene);
    }
    if (error == ESP_OK) {
        set_companion_audio_visible(true);
    }
    return error;
}

#if !defined(CODY_DEVICE_UI_ONLY)
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
#endif

// awake_time_us: Existing awake clock; standby cannot accumulate overdue reminders.
// Exception: Logs optional playback failures; their real outcome remains owned by shared audio.
void advance_question_audio(int64_t awake_time_us)
{
#if defined(CODY_DEVICE_UI_ONLY)
    // The effects controller owns reminders even while another page is visible.
    (void)awake_time_us;
#else
    const auto alert = question_audio_due(s_presented_event,
        s_event_online && s_event_observation_error == ESP_OK && is_codex_event_route(s_route),
        s_event_reply_admitted || s_event_dismiss_admitted, awake_time_us, s_question_audio_schedule);
    if (alert == QuestionAudioAlert::None) {
        return;
    }
    CodexAudioStatus status;
    const esp_err_t error = notify_question_audio(alert, status);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Optional question alert failed: kind=%u (%s)",
            static_cast<unsigned>(alert), esp_err_to_name(error));
    }
#endif
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
// active_time_us: Existing pausable clock for the brief readonly checks-passed result.
// awake_time_us: Awake route clock for question reminders, independent of animation pause.
// changed: Receives whether the event presentation needs rendering.
// handled: True while an event or explicit event-read error owns the current screen.
// Exception: Returns presentation errors; an optional snapshot failure has a visible Retry path.
esp_err_t present_codex_event(const LUNASHELL::CodexLinkSnapshot &link, SceneFrame &scene,
                             int64_t active_time_us, int64_t awake_time_us, bool &changed, bool &handled)
{
    handled = false;
    const bool in_options = s_route == Route::MakeItYours10;
    if (!in_options && s_event_observation_error != ESP_OK) {
        s_question_audio_schedule.last_attempt_at_us = -1;
        handled = true; // Only the visible Retry requests another failed observation.
        return ESP_OK;
    }
    auto &observed = s_observed_event;
    const esp_err_t read_error =
        LUNASHELL::DeviceSnapshotService::instance().readCodexEventObservation(observed);
    if (read_error != ESP_OK) {
        s_question_audio_schedule.last_attempt_at_us = -1;
        if (in_options) {
            // A failed optional read supplies no event that may interrupt settings.
            return ESP_OK;
        }
        s_event_observation_error = read_error;
        s_event_online = false;
        handled = true;
        changed = true;
        ESP_LOGE(kTag, "Optional Codex event observation failed; explicit Retry available (%s)",
            esp_err_to_name(read_error));
        return render_codex_event(scene);
    }
    if (in_options) {
        s_event_observation_error = ESP_OK;
    }
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    const bool have_event = observed.kind != Kind::None && observed.request_id[0] != '\0';
    const bool inbox_question = s_inbox_available && observed.kind == Kind::Question &&
        std::strcmp(s_inbox_question, observed.request_id) == 0;
    if (s_inbox_available && !inbox_question &&
        (observed.kind == Kind::Question || observed.kind == Kind::ReplyReady)) {
        if (is_codex_event_route(s_route)) {
            reset_event_presentation();
            changed = true;
            return enter_route(Route::Connected15, scene);
        }
        return ESP_OK;
    }
    if (!have_event) {
        s_question_audio_schedule.last_attempt_at_us = -1;
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
        advance_question_audio(awake_time_us);
        return advance_checks_passed_dwell(scene, active_time_us, changed);
    }
    const bool selected_question = !same_request &&
        ((s_event_selection_target[0] != '\0' && std::strcmp(observed.request_id, s_event_selection_target) == 0) ||
         (s_presented_event.selection_request_id[0] != '\0' &&
          std::strcmp(observed.request_id, s_presented_event.selection_request_id) == 0));
    const State previous_state = s_presented_event.state;
    if (!same_request) {
        reset_event_presentation();
    }
    s_presented_event = observed;
    s_event_online = online;
    if ((selected_question || (inbox_question && !same_request)) &&
        observed.kind == Kind::Question && observed.state == State::Pending) {
        s_event_stage = EventStage::Choices;
    }
    if (!online || observed.last_error != ESP_OK || observed.selection_request_id[0] == '\0') {
        s_event_selection_target[0] = '\0';
    }
    s_companion_seen_selection = true;
    if (!same_request || (observed.state != previous_state && LUNASHELL::codexEventIsTerminal(observed.state))) {
        // A terminal owner result retires any failed local admission; Retry must
        // never resurrect an expired/rejected response or replace OK with Send.
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
#if !defined(CODY_DEVICE_UI_ONLY)
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
        case Kind::Question: break; // Pending-question cadence owns both initial and reminder alerts.
        case Kind::ApprovalOnPc: notify_event_audio(CodexAudioCue::ApprovalRequired); break;
        case Kind::ChecksFailed: notify_event_audio(CodexAudioCue::ChecksFailed); break;
        case Kind::ChecksPassed: notify_event_audio(CodexAudioCue::ChecksPassed); break;
        case Kind::ChecksRunning: notify_event_audio(CodexAudioCue::Running); break;
        case Kind::PrPending:
        case Kind::ReplyReady:
        case Kind::Notice: notify_event_audio(CodexAudioCue::ResponseReady); break;
        case Kind::None: break;
        }
    }
#endif
    advance_question_audio(awake_time_us);
    return advance_checks_passed_dwell(scene, active_time_us, changed);
}

// scene: Current event view; source selection and replies retain their distinct owners.
// selected: Explicit control from the retained plan whose gesture has completed.
// Exception: Returns layout errors; owner admission failures remain visible and logged.
esp_err_t activate_event_action(SceneFrame &scene, const event_presentation::Action &selected)
{
    if (s_inbox_available && s_inbox_question[0] != '\0' &&
        selected.id == event_presentation::ActionId::Back && selected.enabled &&
        (s_event_stage == EventStage::Choices || s_event_stage == EventStage::Questions ||
         s_event_stage == EventStage::Notice)) {
        s_inbox_question[0] = '\0';
        reset_event_presentation();
        return enter_route(Route::Connected15, scene);
    }
    auto navigation = event_navigation();
    event_presentation::Effect effect;
    const esp_err_t decision_error = event_presentation::activate(s_presented_event,
        event_context(), selected, navigation, effect);
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
    if (effect == event_presentation::Effect::SelectQuestion) {
        const esp_err_t error = LUNASHELL_codex_event_select(selected.request_id);
        s_event_ui_error = error;
        if (error == ESP_OK) {
            // This is local admission, not an assumed source change. Full choices
            // remain those of the current event until the canonical EVENT arrives.
            std::memcpy(s_event_selection_target, selected.request_id, sizeof(s_event_selection_target));
        } else {
            ESP_LOGE(kTag, "Question selection admission failed (%s)", esp_err_to_name(error));
        }
        return render_codex_event(scene, true);
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
            s_inbox_question[0] = '\0';
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

// active_time_us: Pausable UI clock used solely for description scrolling.
// scene: Persistent companion frame with no invented Codex activity or model.
// changed: Receives whether visible content or marquee position changed.
// Exception: Returns original native title, overlay and content preparation failures.
esp_err_t present_local_companion(int64_t active_time_us, SceneFrame &scene, bool &changed)
{
    if (s_local_presentation_valid && !s_home_codex && s_route == Route::CompanionIdle16) {
        return advance_activity_description(active_time_us, scene, changed);
    }
    s_local_presentation_valid = false;
    const char *title = "Local mode";
    esp_err_t error = enter_route(Route::CompanionIdle16, scene);
    if (error == ESP_OK) {
        const size_t title_length = std::strlen(title);
        if (scene.title.text.length != title_length ||
            std::strncmp(scene.title.text.characters, title, title_length) != 0) {
            ++s_text_generation;
            error = set_scene_title(scene, {title, static_cast<uint16_t>(title_length)});
        }
    }
    if (error == ESP_OK) {
        error = set_scene_activity_overlay(scene, ActivityOverlayState::Hidden);
    }
    if (error == ESP_OK) {
        error = prepare_activity_detail("Device ready", "", active_time_us);
    }
    if (error == ESP_OK) {
        error = attach_content(scene);
    }
    if (error != ESP_OK) {
        return error;
    }
    // Transport heartbeats never restart the local companion route.
    s_home_codex = false;
    s_local_presentation_valid = true;
    set_companion_audio_visible(false);
    changed = true;
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
    const int64_t now_us = esp_timer_get_time();
    if (!LUNASHELL::codex_presence::ready_current(link, now_us) ||
        !LUNASHELL::codex_presence::is_open(link.app_presence, now_us)) {
        return present_local_companion(active_time_us, scene, changed);
    }
    const bool restoring_codex = !s_home_codex || s_local_presentation_valid;
    if (restoring_codex) {
        // Source restoration is a visible transition even before the first ACTIVITY.
        s_local_presentation_valid = false;
        s_presented_activity = {};
        s_presented_compaction = LUNASHELL::CodexCompactionState::None;
        s_presented_compaction_feedback = false;
        s_home_ready_dwell = {};
        s_home_reply_dwell = {};
        s_activity_observation = {};
        s_companion_seen_selection = false;
        s_home_codex = true;
    }
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
    const char *message = kCodexConnectedCaption;
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
            if (restoring_codex) {
                changed = true;
                return enter_route(Route::Connected15, scene);
            }
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
            message = "Codex active";
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
            message = reply_introduction ? "Open Codex to review" : kCodexConnectedCaption;
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
            message = kCodexConnectedCaption;
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
    const bool work_route_unchanged = s_route == Route::Working18 && destination == s_route;
    const bool work_identity_unchanged =
        std::strcmp(activity.project, s_presented_activity.project) == 0 &&
        std::strcmp(activity.branch, s_presented_activity.branch) == 0;
    const bool work_observations_current =
        activity.data_valid && !activity.data_stale &&
        s_presented_activity.data_valid && !s_presented_activity.data_stale;

    // Keep reveal and scroll clocks when only a current work detail changes.
    const bool only_work_detail_changed =
        work_route_unchanged && same_chat && work_identity_unchanged &&
        work_observations_current && s_view.activity_identity.project_line.valid;
    const Route previous_route = s_route;
    const bool headline_changed = work_phase != s_presented_work_phase || compaction_changed;
    const char *headline = nullptr;
    // Attention/compaction/connection take priority over the ordinary work caption.
    if (native_attention) {
        headline = "Waiting for you";
    } else if (compacting) {
        headline = "Compacting";
    } else if (reconnecting) {
        headline = "Connecting";
    } else if (!current_link) {
        headline = "Disconnected";
    } else if (activity.data_valid && !activity.data_stale) {
        if (current_work) {
            switch (work_phase) {
            case Activity::Editing:
                headline = "Editing code";
                break;
            case Activity::Running:
                headline = "Running command";
                break;
            default:
                headline = "Working";
                break;
            }
        } else if (activity.state == Activity::Paused) {
            headline = "Waiting for you";
        } else if (activity.state == Activity::ResponseReady && reply_introduction) {
            headline = "Chat waiting";
        } else if (activity.state == Activity::Interrupted) {
            headline = "Interrupted";
        }
    }
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
// changed: Accumulates native scene damage and finite-footer motion or retirement.
// Exception: Returns original content, token or native glyph preparation failures.
esp_err_t present_companion_activity(int64_t active_time_us, SceneFrame &scene, bool &changed)
{
    esp_err_t error = present_companion_activity_content(active_time_us, scene, changed);
    if (error != ESP_OK) {
        s_command_hints.sampled_us = -1;
        return error;
    }
    if (!s_home_codex) {
        // Pause the finite footer lifetime without replaying its completed passes.
        s_command_hints.sampled_us = -1;
        changed = changed || scene.command_hints != nullptr;
        scene.command_hints = nullptr;
        return ESP_OK;
    }
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
        ? Theme::dashboard_attention : scene.home_dashboard &&
            link.activity.state != LUNASHELL::CodexActivityState::Paused &&
            link.activity.state != LUNASHELL::CodexActivityState::Interrupted
        ? Theme::firmware_home_dashboard_mint : scene.definition->title_color;
    changed = changed || scene.title.color != title_color;
    scene.title.color = title_color;
    const uint16_t detail_color = title_color == Theme::dashboard_attention && s_view.activity_identity.compaction_timer
        ? Theme::dashboard_attention : Theme::text_bright;
    if (s_view.activity_description.window.valid && s_view.activity_description.window.color != detail_color) {
        s_view.activity_description.window.color = detail_color;
        scene.activity_description_revision = ++s_activity_description_revision;
        changed = true;
    }
    ScenePresentation presentation{};
    error = prepare_scene_presentation(scene, presentation);
    if (error != ESP_OK) {
        s_command_hints.sampled_us = -1;
        return error;
    }
    const bool eligible = presentation.content_visible && scene.visible_characters == scene.title.text.length &&
        is_companion_route(s_route) &&
        !s_view.activity_identity.attention_active && !s_view.activity_identity.compaction_timer &&
        (s_presented_compaction == LUNASHELL::CodexCompactionState::None ||
         (s_presented_compaction == LUNASHELL::CodexCompactionState::Refreshed && !s_presented_compaction_feedback)) &&
        link.activity.state != LUNASHELL::CodexActivityState::Paused;
    bool hints_changed = false;
    error = update_command_hints(s_command_hints, SceneData::body_font, connected, eligible,
        active_time_us, hints_changed);
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
// detail: Optional recovery instruction derived from a known prerequisite failure.
// Exception: Returns content preparation errors without replacing the real CCP1 error.
esp_err_t show_codex_error(SceneFrame &scene, esp_err_t error, const char *detail = nullptr)
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
    if (kDeviceFirstSetup) {
        s_view.header_icon = CODY2D::ActionIcon::Retry;
        s_view.header_icon_bounds = kCodexPairingGlyph;
        s_view.header_icon_color = kOfflineAmber;
    }
    if (kDeviceFirstSetup && detail != nullptr) {
        presentation_error = prepare_caption(detail, kPcWaitCaptionTop, s_view.lines[0]);
        if (presentation_error == ESP_OK) {
            presentation_error = prepare_caption(s_view.error_text, kPcWaitDetailTop, s_view.lines[1]);
            s_view.lines[1].color = kPairingCaption;
        }
        s_view.line_count = 2;
    } else {
        presentation_error = prepare_caption(s_view.error_text, kPcWaitCaptionTop, s_view.lines[0]);
        s_view.line_count = 1;
    }
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
        return enter_route(Route::CodexReplace, scene, &observation.radios.wifi);
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
        const char *detail = wifi.data_valid && !wifi.data_stale ? "Connect Wi-Fi first" : nullptr;
        return show_codex_error(scene, wifi.last_error != ESP_OK ? wifi.last_error : ESP_ERR_INVALID_STATE, detail);
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

// scene: Current route that displays a real local action/preparation error.
// error: Original operation error, not substituted by a successful presentation.
esp_err_t show_error(SceneFrame &scene, esp_err_t error)
{
    s_visible_error = error;
    return enter_route(Route::Failed08, scene);
}

// scene: Display-owned frame for a device provisioning QR.
// pairing: Current canonical pairing attempt, including terminal failures eligible for Retry.
esp_err_t start_wifi_pairing(SceneFrame &scene, const LUNASHELL::PairingSessionSnapshot &pairing)
{
    esp_err_t error;
    if (!pairing.data_valid || pairing.data_stale) {
        error = pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE;
    } else {
        const Work work = pairing.state == PairState::Failed || pairing.state == PairState::TimedOut
            ? Work::Retry : Work::Start;
        s_session = 0;
        s_cancel_to_install = false;
        s_pairing_from_guide = s_route == Route::Invitation05 && (kDeviceFirstSetup || s_home_wifi_flow);
        error = enter_route(Route::Pairing07, scene);
        if (error == ESP_OK) {
            error = enqueue(work, pairing.attempt_id);
        }
    }
    if (error == ESP_OK) {
        return ESP_OK;
    }
    const esp_err_t presentation_error = show_error(scene, error);
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    const esp_err_t action_error = prepare_action(OnboardingAction::Retry, true);
    return action_error == ESP_OK ? attach_content(scene) : action_error;
}

// Keep the real snapshot error on the direct pairing route and offer another attempt.
// Existing work/cleanup ownership remains untouched until a fresh snapshot is available.
esp_err_t show_home_wifi_snapshot_error(SceneFrame &scene, esp_err_t error)
{
    const auto presentation_error = show_error(scene, error);
    if (presentation_error != ESP_OK) return presentation_error;
    const auto action_error = prepare_action(OnboardingAction::Retry, true);
    return action_error == ESP_OK ? attach_content(scene) : action_error;
}

// scene: Current choice or retry frame, replaced by the persistent companion Home.
// Exception: Returns native presentation failures; write/admission failures stay visible with Retry.
esp_err_t choose_local_companion(SceneFrame &scene)
{
    reset_onboarding();
    esp_err_t error = enter_route(Route::CompanionIdle16, scene);
    if (error == ESP_OK) {
        bool changed = false;
        error = present_companion_activity(0, scene, changed);
    }
    if (error == ESP_OK) {
        error = enqueue(Work::UseCompanion, 0);
    }
    if (error == ESP_OK) {
        return ESP_OK;
    }
    s_failed_work = Work::UseCompanion;
    s_local_failure = true;
    ESP_LOGE(kTag, "Local companion choice failed (%s)", esp_err_to_name(error));
    const esp_err_t presentation_error = show_error(scene, error);
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    const esp_err_t action_error = prepare_action(OnboardingAction::Retry, true);
    return action_error == ESP_OK ? attach_content(scene) : action_error;
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
    ESP_LOGI(kTag, "Screen inactivity returned to the setup entry; pairing cleanup pending=%u",
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
#if defined(CODY_DEVICE_UI_ONLY)
    s_about_project_error = encode_qr(kAboutProjectUrl, s_about_project_qr);
    if (s_about_project_error != ESP_OK) {
        ESP_LOGW(kTag, "About project QR unavailable (%s)", esp_err_to_name(s_about_project_error));
    }
#endif
    s_worker = xTaskCreateStatic(action_worker, "CODY_actions", sizeof(s_worker_stack), nullptr,
        3, s_worker_stack, &s_worker_control);
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "Action worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#if defined(CODY_DEVICE_UI_ONLY)
// content: Borrows the immutable About heading and static project QR.
// Exception: Returns the original optional About QR preparation error.
esp_err_t prepare_about_project_link(SceneContent &content)
{
    if (s_about_project_error != ESP_OK) return s_about_project_error;
    content = {};
    const auto error = prepare_styled_title({kAboutProjectHeading, sizeof(kAboutProjectHeading) - 1U},
        {89, 89, 288, 25}, content.heading, 3, true);
    if (error != ESP_OK) return error;
    content.bounds = {89, 89, 377, 416};
    content.revision = 1;
    content.state = &s_about_project_qr;
    content.draw_rows = draw_about_project_rows;
    return ESP_OK;
}
#endif

// No parameters: cancel gesture recognition without synthesizing an acquired release.
void cancel_onboarding_touch()
{
    s_swipe_active = false;
    s_local_wifi.keyboard_touch = {};
    s_local_wifi.touch_control = -1;
    s_activity_touch_pressed = false;
}

// No parameters: retain scroll position without charging hidden time to its next sample.
void suspend_onboarding_command_hints()
{
    s_command_hints.sampled_us = -1;
}

// No parameters: retire routes and QR state without erasing saved settings or replaying the boot footer.
void reset_onboarding()
{
    clear_text_entry(s_local_wifi.entry);
    clear_text_entry_view(s_local_wifi.keyboard);
    s_local_wifi.page = LocalWifiPage::None;
    set_companion_audio_visible(false);
    reset_event_presentation();
    s_options_from_setup = false;
    s_connections_from_options = false;
    s_widget_open_request = WidgetOpenRequest::None;
    s_options_page = OptionsPage::Menu;
    s_preference_error = ESP_OK;
    s_retry_birth_intro = false;
    s_factory_reset_request_id = 0;
    s_factory_reset_error = ESP_OK;
    s_setup_return = Route::None;
    s_pairing_guide_return = Route::Hello04;
    s_home_wifi_flow = false;
    s_home_wifi_pairing_direct = false;
    s_home_wifi_intro = false;
    s_home_wifi_exit_pending = false;
    s_companion_seen_selection = false;
    s_session_ended_at_us = -1;
    s_presented_activity = {};
    s_home_codex = false;
    s_local_presentation_valid = false;
    // This owner survives setup, route and connection restarts until the display-owner boot ends.
    s_command_hints.sampled_us = -1;
    s_home_ready_dwell = {};
    s_home_reply_dwell = {};
    s_activity_observation = {};
    s_presented_work_phase = LUNASHELL::CodexActivityState::Working;
    ++s_generation;
    s_presentation_generation.store(s_generation, std::memory_order_release);
    s_route = Route::None;
    s_startup_pending = false;
    s_startup_pair_missing = false;
    s_startup_wifi_error = ESP_OK;
    s_startup_wifi_wait_started_us = -1;
    s_startup_wifi_attempt = 0;
    s_startup_offline_home = false;
    s_startup_network_passive = false;
    s_startup_network_presented = false;
    s_command_pending = false;
    s_pairing_owned = false;
    s_cancel_to_install = false;
    s_pairing_from_guide = false;
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

// No parameters: the display owner reads only a consumed successful worker result, never NVS.
bool onboarding_first_boot_completed()
{
    return s_first_boot_persisted;
}

// No parameters: returning devices still need a saved-link decision after startup.
void request_saved_setup_startup()
{
    s_startup_pending = true;
    s_startup_pair_missing = false;
    // Each actual station attempt gets its own visible connection window.
    s_startup_wifi_wait_started_us = -1;
    s_startup_wifi_attempt = 0;
    s_startup_offline_home = false;
    s_startup_network_passive = false;
    s_startup_network_presented = false;
}

// scene: First network route, chosen from the already running service owners.
// first_boot: True while Wi-Fi or an explicit local choice still owes first-boot persistence.
esp_err_t start_device_setup(SceneFrame &scene, bool first_boot)
{
    reset_onboarding();
    // Cache the menu pose before Home starts advancing the shared renderer.
    const auto status = compose_muse_frame(s_wifi_mascot,
        {PixelAvatarPose::Idle, 1000, 1000, kPixelAvatarReactionMs});
    if (status != PixelAvatarComposeStatus::Ok) {
        ESP_LOGE(kTag, "Wi-Fi menu mascot composition failed: %u", static_cast<unsigned>(status));
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_mascot.palette[0] = s_wifi_mascot.dim_palette[0] = kPairingBackground;
    reset_muse_animation(0);
    s_first_boot_persisted = !first_boot;
    s_pairing_debug_dismissed = true;
    request_saved_setup_startup();
    s_startup_pair_missing = true;
    bool changed = false;
    return evaluate_saved_setup(scene, changed);
}

// wifi: Canonical saved profile and the station owner's current connection attempt.
// now_us: Monotonic time for the bounded animation window.
bool saved_wifi_startup_grace(const LUNASHELL::WifiSnapshot &wifi, int64_t now_us)
{
    if (!wifi.credentials_configured) return false;
    if (kDeviceFirstSetup && (!wifi.data_valid || wifi.data_stale || !wifi.started ||
        !wifi.connect_requested || (wifi.reconnect_pending && !wifi.connected) ||
        wifi.connect_attempt_count == 0 || wifi.last_error != ESP_OK)) {
        return false;
    }
    if (s_startup_wifi_wait_started_us < 0 ||
        (kDeviceFirstSetup && s_startup_wifi_attempt != wifi.connect_attempt_count)) {
        s_startup_wifi_attempt = wifi.connect_attempt_count;
        s_startup_wifi_wait_started_us = now_us;
    }
    return now_us - s_startup_wifi_wait_started_us < kStartupWifiGraceUs;
}

// now_us: Monotonic time used to reject a pending or obsolete startup wait.
bool onboarding_saved_wifi_startup_offline(int64_t now_us)
{
    if (!kDeviceFirstSetup || s_startup_wifi_error == ESP_OK || s_startup_wifi_wait_started_us < 0 ||
        now_us < s_startup_wifi_wait_started_us ||
        now_us - s_startup_wifi_wait_started_us < kStartupWifiGraceUs) return false;

    const bool recovery = s_route == Route::NetworkWait && s_startup_network_presented && !s_startup_network_passive;
    const bool local_dashboard = s_route == Route::CompanionIdle16 && !s_startup_pending;
    return recovery || local_dashboard;
}

// No parameters: keep setup, connection attempts and attention screens in their own routes.
bool onboarding_codex_tools_active()
{
    return kDeviceFirstSetup && s_route == Route::MakeItYours10 && s_options_page == OptionsPage::Menu &&
        !maintenance_pending() && !s_command_pending;
}

// No parameters: true on the existing connected dashboard route.
bool onboarding_dashboard_active()
{
    switch (s_route) {
    case Route::Connected15:
    case Route::Project17:
    case Route::Working18:
    case Route::Response19:
    case Route::Paused20:
    case Route::Ended20:
    case Route::Lost32:
    case Route::CompanionIdle16:
    case Route::ActivityUnavailable:
        return true;
    default:
        return false;
    }
}

// now_us: Monotonic time used for the current connection route's animation.
uint8_t onboarding_connection_activity_phase(int64_t now_us)
{
    if (!kDeviceFirstSetup) {
        return 0;
    }
    if (s_route == Route::NetworkWait && s_startup_network_passive) {
        const int64_t elapsed_us = s_startup_wifi_wait_started_us >= 0 && now_us >= s_startup_wifi_wait_started_us
            ? now_us - s_startup_wifi_wait_started_us : 0;
        return static_cast<uint8_t>(elapsed_us / kWifiConnectingStepUs % std::size(kWifiConnectingHighlightTop) + 1U);
    }
    const bool preparing_code = s_route == Route::ConnectCodex12 && !s_codex_error_visible && s_codex_code[0] == '\0';
    const bool waiting_for_pc = s_route == Route::LookingForPc14 && s_view.header_icon == CODY2D::ActionIcon::None;
    if (preparing_code || waiting_for_pc) {
        const auto step = std::max<int64_t>(now_us, 0) / kCodexWaitingStepUs % std::size(kCodexWaitingColors);
        return static_cast<uint8_t>(std::size(kWifiConnectingHighlightTop) + step + 1U);
    }
    return 0;
}

// phase: Zero hides the icon, followed by the Wi-Fi steps and then the Codex pulse steps.
// strip: Renderer-owned RGB565 tile, already cleared to the scene background.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns an invalid phase or the first original raster error.
esp_err_t draw_connection_activity_rows(uint8_t phase, CODY2D::Surface strip,
                                       uint16_t first_row, uint16_t first_column)
{
    if (phase == 0) return ESP_OK;
    constexpr auto wifi_steps = std::size(kWifiConnectingHighlightTop);
    if (phase > wifi_steps + std::size(kCodexWaitingColors)) return ESP_ERR_INVALID_ARG;
    if (first_row >= kConnectionActivityBounds.bottom || first_row + strip.height <= kConnectionActivityBounds.top ||
        first_column >= kConnectionActivityBounds.right || first_column + strip.width <= kConnectionActivityBounds.left) {
        return ESP_OK;
    }
    if (phase > wifi_steps) {
        const auto color = kCodexWaitingColors[phase - wifi_steps - 1U];
        return CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Link, kCodexPairingGlyph,
            color, strip, first_row, first_column);
    }

    // Paint two disjoint tile segments using the same glyph and coverage mask.
    const auto highlight_top = kWifiConnectingHighlightTop[phase - 1U];
    const auto muted_rows = static_cast<uint16_t>(std::clamp<int>(highlight_top - first_row, 0, strip.height));
    if (muted_rows != 0) {
        auto muted = strip;
        muted.height = muted_rows;
        const auto error = CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Wifi, kWifiConnectingGlyph,
            kWifiConnectingMuted, muted, first_row, first_column);
        if (error != ESP_OK || muted_rows == strip.height) return error;
        const auto offset = static_cast<uint32_t>(muted_rows) * strip.stride;
        if (offset >= strip.pixel_capacity) return ESP_ERR_INVALID_SIZE;
        strip.pixels += offset;
        strip.pixel_capacity -= offset;
        strip.height -= muted_rows;
    }
    return CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Wifi, kWifiConnectingGlyph,
        kWifiReadyMint, strip, static_cast<uint16_t>(first_row + muted_rows), first_column);
}

// No parameters: keep paging controls easy to find while reading the guide.
bool onboarding_quick_guide_active()
{
    return on_quick_guide();
}

// No parameters: resting setup pages may reopen choices without retiring live work.
bool can_open_setup_menu()
{
    const bool offline_recovery = s_route == Route::NetworkWait && !s_startup_network_passive;
    const bool offline_home = kDeviceFirstSetup && s_route == Route::Hello04;
    return (!s_startup_pending || offline_recovery || offline_home) &&
        !s_timeout_cleanup.pending && !s_command_pending &&
        !s_work_busy.load(std::memory_order_acquire) && !s_pairing_owned &&
        (offline_recovery || s_route == Route::None || s_route == Route::LookingForPc14 || s_route == Route::Failed08 ||
         (s_route == Route::Hello04 && (kDeviceFirstSetup || s_hello_waits_for_touch)) ||
         s_route == Route::Invitation05 || s_route == Route::Install06 ||
         is_companion_route(s_route) ||
         (s_route == Route::ConnectCodex12 && s_codex_error_visible && s_codex_error_allows_setup));
}

// No parameters: never replace an active pairing operation or its cleanup.
bool can_open_wifi_setup_from_home()
{
    return !s_timeout_cleanup.pending && !s_command_pending &&
        !s_work_busy.load(std::memory_order_acquire) && !s_pairing_owned;
}

// scene: An explicit setup request enters the existing choices without starting Bluetooth.
// Exception: Returns the original route admission or presentation error.
esp_err_t open_wifi_setup_from_home(SceneFrame &scene)
{
    if (!can_open_wifi_setup_from_home()) return ESP_ERR_INVALID_STATE;
    const Route return_route = s_route == Route::SetupMenu ? Route::SetupMenu : Route::None;
    s_home_wifi_flow = true;
    s_home_wifi_pairing_direct = false;
    s_home_wifi_intro = false;
    s_home_wifi_apple = false;
    s_home_wifi_exit_pending = false;
    s_pairing_guide_return = return_route;
    s_startup_pending = false;
    s_startup_pair_missing = false;
    const auto error = enter_route(Route::Invitation05, scene);
    if (error != ESP_OK) s_home_wifi_flow = false;
    return error;
}

// scene: Home enters the Bluetooth setup introduction before starting provisioning.
// Exception: Returns the original admission or presentation error.
esp_err_t open_wifi_pairing_from_home(SceneFrame &scene)
{
    if (!can_open_wifi_setup_from_home()) return ESP_ERR_INVALID_STATE;
    s_home_wifi_flow = true;
    s_home_wifi_pairing_direct = true;
    s_home_wifi_intro = true;
    s_home_wifi_apple = false;
    s_home_wifi_exit_pending = false;
    s_pairing_guide_return = Route::None;
    s_startup_pending = false;
    s_startup_pair_missing = false;
    const auto start_error = enter_route(Route::Invitation05, scene);
    if (start_error != ESP_OK) {
        s_home_wifi_flow = false;
        s_home_wifi_pairing_direct = false;
    }
    return start_error;
}

// strip: Borrowed RGB565 tile for the completely displayed local Wi-Fi scene.
// first_row: Logical y coordinate of the tile.
// first_column: Logical x coordinate of the tile.
// frame: Small looping animation phase; it does not imply scan progress.
// Exception: Returns the original raster error.
esp_err_t draw_wifi_hourglass(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column, uint8_t frame)
{
    constexpr int left = 205, top = 174, scale = 4;
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 14; ++x) {
            const bool cap = (y <= 1 || y >= 19) && x >= 1 && x <= 12;
            const int distance = y <= 10 ? y : 20 - y;
            const int edge = 2 + distance / 2;
            const bool glass = y >= 2 && y <= 18 && (x == edge || x == 13 - edge);
            const bool upper_sand = y >= 5 + frame / 2 && y <= 8 && x > edge && x < 13 - edge;
            const bool lower_sand = y >= 17 - frame / 2 && y <= 17 && x > edge && x < 13 - edge;
            const bool falling = x == 6 + (y + frame) % 2 && y >= 9 && y <= 15 && (y + frame) % 3 == 0;
            if (!cap && !glass && !upper_sand && !lower_sand && !falling) continue;
            const int px = left + x * scale, py = top + y * scale;
            const int x0 = std::max<int>(px, first_column), x1 = std::min<int>(px + scale, first_column + strip.width);
            const int y0 = std::max<int>(py, first_row), y1 = std::min<int>(py + scale, first_row + strip.height);
            if (x0 >= x1 || y0 >= y1) continue;
            const auto error = CODY2D::fill_rect(strip, {static_cast<uint16_t>(x0 - first_column),
                static_cast<uint16_t>(y0 - first_row), static_cast<uint16_t>(x1 - x0),
                static_cast<uint16_t>(y1 - y0)}, cap ? Theme::wifi_hourglass_cap :
                    glass ? Theme::wifi_hourglass_glass : Theme::wifi_hourglass_sand);
            if (error != ESP_OK) return error;
        }
    }
    return ESP_OK;
}

// state: Retained local Wi-Fi view, including selected-label scrolling state.
// strip: Borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original text, icon, keyboard or control drawing error.
// These screens retain their own complete title and input view below the shared status bar.
esp_err_t draw_local_wifi_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto &view = *static_cast<const LocalWifiView *>(state);
    esp_err_t error = ESP_OK;
    if (view.page == LocalWifiPage::Password) {
        error = view.keyboard_content.draw_rows(&view.keyboard, strip, first_row, first_column);
    } else {
        if (error == ESP_OK && view.subtitle.valid) {
            const bool descriptive = view.page == LocalWifiPage::Networks || view.page == LocalWifiPage::ConfirmOpen;
            error = descriptive ? draw_styled_subtitle_line(view.subtitle, strip, first_row, first_column,
                view.subtitle.text.length) : CODY2D::draw_text_rows(view.subtitle, view.subtitle.text.length,
                strip, first_row, first_column);
        }
        if (error == ESP_OK && view.detail.valid)
            error = CODY2D::draw_text_rows(view.detail, view.detail.text.length, strip, first_row, first_column);
        if (error == ESP_OK && view.page == LocalWifiPage::Networks && view.scanning_requested) {
            error = draw_wifi_hourglass(strip, first_row, first_column, view.hourglass_frame);
        }
        const auto draw = [&](const CODY2D::CapsuleControl &control) {
            return control.caption.valid ? UI::draw_text_action(control, strip, first_row, first_column) : ESP_OK;
        };
        for (size_t i = 0; i < 4 && error == ESP_OK; ++i) {
            const auto &row = view.rows[i];
            if (view.page == LocalWifiPage::Networks && row.caption.valid) {
                auto visual = row;
                const bool selected = view.selected_network >= 0 &&
                    view.selected_network == view.offset + static_cast<int>(i);
                visual.icon = selected ? CODY2D::ActionIcon::Check : CODY2D::ActionIcon::Wifi;
                visual.icon_center = {120, static_cast<int16_t>(row.bounds.y + row.bounds.height / 2U)};
                visual.icon_color = selected ? Theme::secondary_bright : Theme::secondary_default;
                if (selected) visual.caption = {}; // The full selected SSID is drawn in its scrolling viewport.
                error = draw_flat_menu_row(visual, 24, strip, first_row, first_column);
                if (error == ESP_OK && i < std::size(view.network_signals) && view.network_signals[i].valid)
                    error = CODY2D::draw_text_rows(view.network_signals[i], view.network_signals[i].text.length,
                        strip, first_row, first_column);
            } else {
                error = draw(row);
            }
        }
        if (error == ESP_OK && view.page == LocalWifiPage::Networks && !view.scanning_requested &&
            view.scan_observation.count > 0) {
            const auto pages = static_cast<uint8_t>((view.scan_observation.count + 2U) / 3U);
            const auto selected = static_cast<uint8_t>(view.offset / 3U);
            error = draw_page_marks(selected, pages, strip, first_row, first_column, 320);
        }
        if (error == ESP_OK) error = draw(view.scan);
        if (error == ESP_OK) error = draw(view.confirm);
    }
    if (error == ESP_OK && view.page == LocalWifiPage::Networks && view.selected_network >= 0) {
        error = draw_selected_label(view.selected_label, strip, first_row, first_column);
    }
    return error;
}

esp_err_t prepare_local_wifi_button(const char *label, CODY2D::Rect bounds, bool enabled,
    CODY2D::CapsuleControl &control)
{
    CODY2D::TextLine unused;
    const auto error = prepare_button(OnboardingAction::LocalWifi, label, bounds, enabled, false, control, unused);
    if (error != ESP_OK) return error;
    control.top_color = control.bottom_color = Theme::surface_keyboard;
    control.border_color = 0;
    control.corner_radius = 14;
    return ESP_OK;
}

// scene: Receives the retained Wi-Fi view and its complete control bounds.
// Exception: Preserves the original selected-label, layout or control error.
esp_err_t attach_local_wifi(SceneFrame &scene)
{
    auto &view = s_local_wifi;
    static const SceneDefinition definition = [&scene]() {
        auto copy = *scene.definition;
        copy.title_visible = false;
        return copy;
    }();
    scene.definition = &definition;
    view.subtitle = {};
    view.detail = {};
    for (auto &row : view.rows) row = {};
    for (auto &signal : view.network_signals) signal = {};
    view.scan = {};
    view.confirm = {};
    esp_err_t error = ESP_OK;
    if (view.page == LocalWifiPage::Networks && view.selected_network >= view.offset &&
        view.selected_network < view.offset + 3) {
        const char *security = nullptr;
        switch (view.selected_ap.security) {
        case LUNASHELL_WIFI_SELECTED_OPEN: security = "Open"; break;
        case LUNASHELL_WIFI_SELECTED_WPA2: security = "WPA2"; break;
        case LUNASHELL_WIFI_SELECTED_WPA3: security = "WPA3"; break;
        case LUNASHELL_WIFI_SELECTED_WPA2_WPA3: security = "WPA2/WPA3"; break;
        default: return ESP_ERR_INVALID_STATE;
        }
        char selected_caption[65]{};
        const auto *mac = view.selected_ap.bssid;
        const int length = std::snprintf(selected_caption, sizeof(selected_caption),
            "%s %s %02X:%02X:%02X:%02X:%02X:%02X", view.selected_ssid, security,
            static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]), static_cast<unsigned>(mac[2]),
            static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(selected_caption)) return ESP_ERR_INVALID_SIZE;
        const auto row = static_cast<uint16_t>(view.selected_network - view.offset);
        error = set_selected_label(view.selected_label, selected_caption, SceneData::body_font,
            {141, static_cast<uint16_t>(146 + row * 56), 220, 25});
    } else {
        view.selected_label = {};
    }
    StyledTitle heading{};
    if (view.page == LocalWifiPage::Password) {
        if (error == ESP_OK) error = prepare_text_entry_view(view.entry, "Wi-Fi password", "Enter password",
            SceneData::body_font, view.keyboard, view.keyboard_content, "Connect", "Replaces saved Wi-Fi");
        if (error == ESP_OK) {
            heading = view.keyboard_content.heading;
            for (auto &key : view.keyboard.keys) {
                key.top_color = key.bottom_color = Theme::surface_keyboard;
                key.border_color = 0;
            }
        }
    } else {
        const char *heading_text = view.page == LocalWifiPage::Networks ? "Wi-Fi networks" :
            view.page == LocalWifiPage::ConfirmOpen ? "Open network" : "Connecting";
        if (error == ESP_OK) error = prepare_styled_title(
            {heading_text, static_cast<uint16_t>(std::strlen(heading_text))}, {83, 75, 300, 25}, heading, 3, true);
        if (error == ESP_OK) {
            const char *subtitle = view.page == LocalWifiPage::Networks ?
                (view.detail_text[0] != '\0' && !view.scanning_requested ? view.detail_text : "Replaces saved Wi-Fi") :
                view.page == LocalWifiPage::ConfirmOpen ? "Replaces saved Wi-Fi" : "Waiting for network";
            error = prepare_caption(subtitle, 106, view.subtitle, 300);
        }
        if (error == ESP_OK && view.page == LocalWifiPage::Networks && view.scanning_requested) {
            error = prepare_caption("Searching...", 270, view.detail, 300);
        } else if (error == ESP_OK && view.page != LocalWifiPage::Networks && view.detail_text[0] != '\0') {
            error = prepare_caption(view.detail_text, 132, view.detail, 300);
        }
        if (error == ESP_OK && view.page == LocalWifiPage::Networks && !view.scanning_requested) {
            for (uint8_t i = 0; i < 3 && view.offset + i < view.scan_observation.count; ++i) {
                const auto &network = view.scan_observation.networks[view.offset + i];
                size_t len = 0;
                while (len < sizeof(network.ssid) - 1U && network.ssid[len] != '\0') {
                    const unsigned char c = static_cast<unsigned char>(network.ssid[len]);
                    view.labels[i][len] = c >= 32 && c <= 126 ? static_cast<char>(c) : '?';
                    ++len;
                }
                view.labels[i][len] = '\0';
                if (len == 0) {
                    std::memcpy(view.labels[i], "Hidden", 6);
                    len = 6;
                    view.labels[i][len] = '\0';
                }
                uint32_t measured = 0;
                error = CODY2D::measure_text_width(SceneData::body_font,
                    {view.labels[i], static_cast<uint16_t>(len)}, measured);
                if (error != ESP_OK) break;
                if (measured > 220U) {
                    // Leave room for an ellipsis while retaining the complete selected SSID separately.
                    len = std::min(len, sizeof(view.labels[i]) - 4U);
                    do {
                        view.labels[i][len] = '.';
                        view.labels[i][len + 1U] = '.';
                        view.labels[i][len + 2U] = '.';
                        view.labels[i][len + 3U] = '\0';
                        error = CODY2D::measure_text_width(SceneData::body_font,
                            {view.labels[i], static_cast<uint16_t>(len + 3U)}, measured);
                        if (error != ESP_OK || measured <= 220U || len == 0) break;
                        --len;
                    } while (true);
                    if (error != ESP_OK) break;
                    if (measured > 220U) return ESP_ERR_INVALID_SIZE;
                }
                const uint16_t row_top = static_cast<uint16_t>(145U + i * 56U);
                error = prepare_local_wifi_button(view.labels[i], {93, row_top, 280, 52},
                    !view.scanning_requested, view.rows[i]);
                if (error == ESP_OK) {
                    auto &caption = view.rows[i].caption;
                    const CODY2D::TextSpan text{view.labels[i], static_cast<uint16_t>(std::strlen(view.labels[i]))};
                    uint32_t width = 0;
                    error = CODY2D::measure_text_width(SceneData::body_font, text, width);
                    if (error == ESP_OK && width > 220U) {
                        ESP_LOGE(kTag, "Wi-Fi row caption does not fit (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
                        error = ESP_ERR_INVALID_SIZE;
                    }
                    if (error == ESP_OK) error = CODY2D::prepare_text_line(SceneData::body_font, text,
                        {141, static_cast<int16_t>(row_top + 1U)}, caption.color, caption);
                    const int signal_length = std::snprintf(view.signal_labels[i], sizeof(view.signal_labels[i]),
                        "%d dBm", static_cast<int>(network.rssi));
                    if (error == ESP_OK && (signal_length <= 0 || static_cast<size_t>(signal_length) >= sizeof(view.signal_labels[i])))
                        error = ESP_ERR_INVALID_SIZE;
                    if (error == ESP_OK) error = CODY2D::prepare_text_line(SceneData::body_font,
                        {view.signal_labels[i], static_cast<uint16_t>(signal_length)},
                        {141, static_cast<int16_t>(row_top + 25U)}, Theme::secondary_default, view.network_signals[i]);
                }
                if (error != ESP_OK) break;
            }
            if (error == ESP_OK) error = prepare_local_wifi_button("Scan for networks", {93, 342, 280, 38},
                !view.scanning_requested && !s_command_pending, view.scan);
        } else if (error == ESP_OK && view.page == LocalWifiPage::ConfirmOpen) {
            error = prepare_local_wifi_button("Connect", {103, 260, 260, 50}, true, view.confirm);
        }
    }
    if (error != ESP_OK) return error;
    SceneContent content{};
    content.bounds = {73, 73, 393, 383};
    content.bottom_action_visible = false;
    content.bottom_action_bounds = {};
    content.control_bounds[0] = {10, 203, 70, 263};
    content.state = &view;
    content.revision = ++view.revision;
    content.control_revision = ++s_navigation_revision;
    content.draw_rows = draw_local_wifi_rows;
    content.heading = heading;
    return set_scene_content(scene, content);
}

esp_err_t open_local_wifi(SceneFrame &scene)
{
    clear_text_entry(s_local_wifi.entry);
    clear_text_entry_view(s_local_wifi.keyboard);
    wipe(&s_local_wifi, sizeof(s_local_wifi));
    s_local_wifi.touch_control = -1;
    s_local_wifi.selected_network = -1;
    s_local_wifi.page = LocalWifiPage::Networks;
    auto error = LUNASHELL::DeviceSnapshotService::instance().readWifiScanObservation(s_local_wifi.scan_observation);
    if (error != ESP_OK) {
        std::snprintf(s_local_wifi.detail_text, sizeof(s_local_wifi.detail_text), "Scan unavailable");
        wipe(&s_local_wifi.scan_observation, sizeof(s_local_wifi.scan_observation));
        s_local_wifi.presented_revision = std::numeric_limits<uint64_t>::max();
    } else {
        if (s_local_wifi.scan_observation.count > 16) return ESP_ERR_INVALID_RESPONSE;
        s_local_wifi.presented_revision = s_local_wifi.scan_observation.revision;
        if (s_local_wifi.scan_observation.state == LUNASHELL::WifiScanState::Scanning) {
            s_local_wifi.scanning_requested = true;
            s_local_wifi.scan_baseline_generation = s_local_wifi.scan_observation.generation - 1U;
            std::snprintf(s_local_wifi.detail_text, sizeof(s_local_wifi.detail_text), "Scanning...");
        } else {
            s_local_wifi.scan_baseline_generation = s_local_wifi.scan_observation.generation;
            const auto start = enqueue(Work::LocalWifiScan, 0);
            if (start == ESP_OK) {
                s_local_wifi.scanning_requested = true;
            } else {
                ESP_LOGE(kTag, "Wi-Fi scan request failed (%s)", esp_err_to_name(start));
                std::snprintf(s_local_wifi.detail_text, sizeof(s_local_wifi.detail_text), "Scan error 0x%lx",
                    static_cast<unsigned long>(start));
            }
        }
    }
    return attach_local_wifi(scene);
}

esp_err_t submit_local_wifi(SceneFrame &scene, bool open_network)
{
    auto &view = s_local_wifi;
    if (!open_network) {
        const auto length = view.entry.length;
        bool valid = length >= 8 && length <= 64 &&
            (view.selected_ap.security != LUNASHELL_WIFI_SELECTED_WPA3 || length <= 63);
        if (length == 64) {
            for (size_t i = 0; i < length; ++i) {
                const char c = view.entry.text[i];
                valid = valid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
            }
        }
        if (!valid) {
            clear_text_entry(view.entry);
            clear_text_entry_view(view.keyboard);
            view.page = LocalWifiPage::Networks;
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Invalid WPA password");
            return attach_local_wifi(scene);
        }
    }
    auto &observation = view.connectivity_scratch;
    const auto read_error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (read_error != ESP_OK || !observation.radios.wifi.data_valid ||
        !observation.radios.wifi.connection_state_valid || observation.radios.wifi.data_stale) {
        clear_text_entry(view.entry);
        clear_text_entry_view(view.keyboard);
        view.page = LocalWifiPage::Networks;
        std::snprintf(view.detail_text, sizeof(view.detail_text), "Wi-Fi status unavailable");
        return attach_local_wifi(scene);
    }
    const auto &current_wifi = observation.radios.wifi;
    view.connection_attempt_baseline = current_wifi.connect_attempt_count;
    view.association_baseline = current_wifi.association_count;
    wipe(&s_local_wifi_credentials, sizeof(s_local_wifi_credentials));
    std::memcpy(s_local_wifi_credentials.ssid, view.selected_ssid, sizeof(view.selected_ssid));
    s_local_wifi_credentials.selection = view.selected_ap;
    if (!open_network) std::memcpy(s_local_wifi_credentials.password, view.entry.text, view.entry.length + 1U);
    view.connect_started_us = esp_timer_get_time();
    const auto error = enqueue(Work::LocalWifiConnect, 0);
    clear_text_entry(view.entry);
    clear_text_entry_view(view.keyboard);
    if (error != ESP_OK) {
        wipe(&s_local_wifi_credentials, sizeof(s_local_wifi_credentials));
        view.page = LocalWifiPage::Networks;
        std::snprintf(view.detail_text, sizeof(view.detail_text), "Connection request failed");
    } else {
        view.page = LocalWifiPage::Connecting;
        view.connect_submitted = true;
        view.detail_text[0] = '\0';
    }
    return attach_local_wifi(scene);
}

// scene: Current local Wi-Fi route, retaining its borrowed presentation.
// active_time_us: Awake display clock, excluding standby.
// scene_presented: Whether this route owned the last fully displayed frame.
// changed: Receives whether the scene needs another frame.
// Exception: Preserves snapshot, caption and scene preparation errors.
esp_err_t update_local_wifi(SceneFrame &scene, int64_t active_time_us, bool scene_presented, bool &changed)
{
    auto &view = s_local_wifi;
    bool redraw = false;
    bool animation_changed = false;
    if (view.page == LocalWifiPage::Networks && view.scanning_requested && scene_presented && active_time_us >= 0) {
        const auto frame = static_cast<uint8_t>((active_time_us / 200000) % 8);
        if (frame != view.hourglass_frame) {
            view.hourglass_frame = frame;
            animation_changed = true;
        }
    }
    if (s_result_ready.load(std::memory_order_acquire) &&
        (s_result.command.work == Work::LocalWifiScan || s_result.command.work == Work::LocalWifiConnect)) {
        const Work work = s_result.command.work;
        const esp_err_t result = s_result.error;
        wipe(&s_result, sizeof(s_result));
        s_result_ready.store(false, std::memory_order_release);
        s_work_busy.store(false, std::memory_order_release);
        s_command_pending = false;
        if (result != ESP_OK) {
            view.scanning_requested = false;
            view.connect_submitted = false;
            view.page = LocalWifiPage::Networks;
            std::snprintf(view.detail_text, sizeof(view.detail_text), "%s 0x%lx",
                work == Work::LocalWifiScan ? "Scan error" : "Connect error", static_cast<unsigned long>(result));
        }
        redraw = true;
    }
    auto &scan = view.scan_scratch;
    const auto scan_error = LUNASHELL::DeviceSnapshotService::instance().readWifiScanObservation(scan);
    if (scan_error == ESP_OK && scan.revision != view.presented_revision) {
        if (scan.count > 16) return ESP_ERR_INVALID_RESPONSE;
        if (view.selected_network >= 0 && (scan.generation != view.selected_scan_generation ||
            scan.state != LUNASHELL::WifiScanState::Complete || view.selected_network >= scan.count ||
            std::memcmp(scan.networks[view.selected_network].bssid, view.selected_ap.bssid,
                sizeof(view.selected_ap.bssid)) != 0)) view.selected_network = -1;
        view.scan_observation = scan;
        view.presented_revision = scan.revision;
        if (scan.state == LUNASHELL::WifiScanState::Scanning && !view.scanning_requested) {
            view.scanning_requested = true;
            view.scan_baseline_generation = scan.generation - 1U;
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Scanning...");
        }
        if (view.scanning_requested && scan.generation != view.scan_baseline_generation &&
            scan.state != LUNASHELL::WifiScanState::Scanning) {
            view.scanning_requested = false;
            view.offset = 0;
            if (scan.state == LUNASHELL::WifiScanState::Complete) {
                std::snprintf(view.detail_text, sizeof(view.detail_text), "%s", scan.count ? "" : "No networks found");
            } else {
                std::snprintf(view.detail_text, sizeof(view.detail_text), "Scan error 0x%lx", static_cast<unsigned long>(scan.error));
            }
        }
        redraw = redraw || view.page == LocalWifiPage::Networks;
    } else if (scan_error != ESP_OK && view.page == LocalWifiPage::Networks && !s_command_pending) {
        view.selected_network = -1;
        view.scanning_requested = false;
        view.scan_observation.count = 0;
        view.presented_revision = std::numeric_limits<uint64_t>::max();
        if (std::strcmp(view.detail_text, "Scan unavailable") != 0) {
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Scan unavailable");
            redraw = true;
        }
    }
    if (view.page == LocalWifiPage::Connecting && view.connect_submitted && !s_command_pending) {
        auto &observation = view.connectivity_scratch;
        if (LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation) == ESP_OK) {
            const auto &wifi = observation.radios.wifi;
            if (wifi.data_valid && wifi.connection_state_valid && !wifi.data_stale && wifi.last_error == ESP_OK &&
                std::strcmp(wifi.ssid, view.selected_ssid) == 0) {
                if (wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0' && wifi.rssi_valid &&
                    wifi.rssi_sampled_at_us >= view.connect_started_us && wifi.bssid_valid &&
                    std::memcmp(wifi.bssid, view.selected_ap.bssid, sizeof(wifi.bssid)) == 0 &&
                    wifi.connect_attempt_count != view.connection_attempt_baseline &&
                    wifi.association_count != view.association_baseline) {
                    view.connect_submitted = false;
                    view.page = LocalWifiPage::None;
                    s_widget_open_request = WidgetOpenRequest::Home;
                }
            }
        }
        if (view.connect_submitted && esp_timer_get_time() - view.connect_started_us >= 20LL * 1000000LL) {
            view.connect_submitted = false;
            view.page = LocalWifiPage::Networks;
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Connection not confirmed");
            redraw = true;
        }
    }
    if (view.page == LocalWifiPage::Networks && view.selected_network >= 0 &&
        view.selected_network >= view.offset && view.selected_network < view.offset + 3) {
        bool moved = false;
        const auto error = advance_selected_label(view.selected_label, SceneData::body_font,
            active_time_us, scene_presented, moved);
        if (error != ESP_OK) return error;
        // Offset changes have their own narrow display damage; route changes
        // still use the ordinary scene revision and complete redraw.
    }
    if (redraw) {
        changed = true;
        return attach_local_wifi(scene);
    }
    if (animation_changed && view.scanning_requested) {
        // Update artwork only; preserve the retired touch-control generation.
        scene.content.revision = ++view.revision;
        changed = true;
    }
    return ESP_OK;
}

// bounds: Receives the selected row's text viewport for narrow repainting.
// offset: Receives the text's current pixel offset.
// Returns false when the local network list has no selected, visible row.
bool onboarding_selected_label_damage(DamageRect &bounds, uint16_t &offset)
{
    const auto &view = s_local_wifi;
    if (view.page != LocalWifiPage::Networks || view.selected_network < 0 ||
        !view.selected_label.line.valid) return false;
    const auto &rect = view.selected_label.bounds;
    bounds = {rect.x, rect.y, static_cast<uint16_t>(rect.x + rect.width),
        static_cast<uint16_t>(rect.y + rect.height)};
    offset = view.selected_label.offset;
    return true;
}

esp_err_t handle_local_wifi_touch(SceneFrame &scene, CODY2D::Point point, bool pressed,
    bool pressed_edge, uint16_t rotation, bool &consumed)
{
    auto &view = s_local_wifi;
    consumed = false;
    if (s_command_pending || s_work_busy.load(std::memory_order_acquire)) return ESP_OK;
    if (view.page == LocalWifiPage::Password) {
        TextEntryResult result = TextEntryResult::None;
        const auto error = handle_text_entry_touch(view.keyboard_touch, view.entry, point, pressed,
            pressed_edge, rotation, esp_timer_get_time(), result, consumed);
        if (error != ESP_OK) return error;
        if (result == TextEntryResult::Submit) return submit_local_wifi(scene, false);
        if (result == TextEntryResult::Changed) return attach_local_wifi(scene);
        if (consumed) return ESP_OK;
    }
    const CODY2D::CapsuleControl back{kBackBounds, {}, 0, 0, true};
    const auto hit = [&](CODY2D::Point p) -> int8_t {
        if (s_home_wifi_chrome_opacity == 255 && CODY2D::capsule_contains(back, p)) return 11;
        if (view.page == LocalWifiPage::Networks) {
            for (int8_t i = 0; i < 3; ++i) if (view.rows[i].caption.valid && view.rows[i].enabled &&
                CODY2D::capsule_contains(view.rows[i], p)) return i;
            if (view.scan.caption.valid && view.scan.enabled && CODY2D::capsule_contains(view.scan, p)) return 5;
        }
        if (view.page == LocalWifiPage::ConfirmOpen && view.confirm.caption.valid &&
            CODY2D::capsule_contains(view.confirm, p)) return 6;
        return -1;
    };
    if (pressed_edge) {
        view.touch_control = hit(point);
        if (view.page == LocalWifiPage::Networks && !view.scanning_requested && view.touch_control < 0 &&
            point.x >= 73 && point.x < 393 && point.y >= 145 && point.y < 298) view.touch_control = -2;
        view.touch_start = point;
        view.touch_last = point;
        view.touch_rotation = rotation;
        view.touch_moved = false;
    }
    if (view.touch_control == -1) return ESP_OK;
    consumed = true;
    if (pressed) {
        view.touch_last = point;
        const int dx = point.x - view.touch_start.x;
        const int dy = point.y - view.touch_start.y;
        if (rotation != view.touch_rotation || dx * dx + dy * dy > 144 || hit(point) != view.touch_control)
            view.touch_moved = true;
        return ESP_OK;
    }
    const int8_t control = view.touch_control;
    view.touch_control = -1;
    const int swipe_x = view.touch_last.x - view.touch_start.x;
    const int swipe_y = view.touch_last.y - view.touch_start.y;
    if (view.page == LocalWifiPage::Networks && !view.scanning_requested && rotation == view.touch_rotation &&
        (control == -2 || (control >= 0 && control < 3)) &&
        std::abs(swipe_x) >= 48 && std::abs(swipe_x) * 2 > std::abs(swipe_y) * 3) {
        if (swipe_x > 0 && view.offset > 0) view.offset = static_cast<uint8_t>(view.offset - 3);
        else if (swipe_x < 0 && view.offset + 3 < view.scan_observation.count)
            view.offset = static_cast<uint8_t>(view.offset + 3);
        else return ESP_OK;
        view.selected_network = -1;
        return attach_local_wifi(scene);
    }
    if (view.touch_moved || rotation != view.touch_rotation) return ESP_OK;
    if (control == -2) return ESP_OK;
    if (control == 11) {
        clear_text_entry(view.entry);
        clear_text_entry_view(view.keyboard);
        if (view.page == LocalWifiPage::Networks) {
            view.page = LocalWifiPage::None;
            return enter_route(Route::Invitation05, scene);
        }
        view.page = LocalWifiPage::Networks;
        view.connect_submitted = false;
        view.detail_text[0] = '\0';
        return attach_local_wifi(scene);
    }
    if (control == 5) {
        view.selected_network = -1;
        view.scan_baseline_generation = view.scan_observation.generation;
        const auto error = enqueue(Work::LocalWifiScan, 0);
        if (error == ESP_OK) {
            view.scanning_requested = true;
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Scanning...");
        } else {
            std::snprintf(view.detail_text, sizeof(view.detail_text), "Scan request failed");
        }
        return attach_local_wifi(scene);
    }
    if (control == 6) return submit_local_wifi(scene, true);
    const size_t index = view.offset + static_cast<uint8_t>(control);
    if (index >= view.scan_observation.count || view.scan_observation.state != LUNASHELL::WifiScanState::Complete)
        return ESP_OK;
    const auto &network = view.scan_observation.networks[index];
    using Security = LUNASHELL::WifiNetworkSecurity;
    bool printable_ssid = true;
    for (size_t i = 0; i < sizeof(network.ssid) && network.ssid[i] != '\0'; ++i) {
        const auto byte = static_cast<unsigned char>(network.ssid[i]);
        if (byte < 32 || byte > 126) { printable_ssid = false; break; }
    }
    if (network.ssid[0] == '\0' || (network.security != Security::Open &&
        network.security != Security::Wpa2Personal && network.security != Security::Wpa3Personal &&
        network.security != Security::Wpa2Wpa3Personal) || !printable_ssid) {
        view.selected_network = -1;
        std::snprintf(view.detail_text, sizeof(view.detail_text), "%s",
            network.ssid[0] == '\0' ? "Hidden network unsupported" :
            !printable_ssid ? "Network name unsupported" : "Security unsupported");
        return attach_local_wifi(scene);
    }
    const bool confirmed = view.selected_network == static_cast<int>(index) &&
        view.selected_scan_generation == view.scan_observation.generation &&
        std::memcmp(view.selected_ap.bssid, network.bssid, sizeof(network.bssid)) == 0 &&
        std::strcmp(view.selected_ssid, network.ssid) == 0;
    std::memcpy(view.selected_ssid, network.ssid, sizeof(view.selected_ssid));
    view.selected_ssid[sizeof(view.selected_ssid) - 1] = '\0';
    std::memcpy(view.selected_ap.bssid, network.bssid, sizeof(view.selected_ap.bssid));
    switch (network.security) {
    case Security::Open: view.selected_ap.security = LUNASHELL_WIFI_SELECTED_OPEN; break;
    case Security::Wpa2Personal: view.selected_ap.security = LUNASHELL_WIFI_SELECTED_WPA2; break;
    case Security::Wpa3Personal: view.selected_ap.security = LUNASHELL_WIFI_SELECTED_WPA3; break;
    case Security::Wpa2Wpa3Personal: view.selected_ap.security = LUNASHELL_WIFI_SELECTED_WPA2_WPA3; break;
    default: return ESP_ERR_INVALID_STATE;
    }
    if (!confirmed) {
        view.selected_network = static_cast<int8_t>(index);
        view.selected_scan_generation = view.scan_observation.generation;
        std::snprintf(view.detail_text, sizeof(view.detail_text), "Tap again to select");
        return attach_local_wifi(scene);
    }
    view.selected_network = -1;
    if (network.security == Security::Open) {
        view.page = LocalWifiPage::ConfirmOpen;
        std::snprintf(view.detail_text, sizeof(view.detail_text), "Unsecured network");
    } else {
        const auto error = begin_text_entry(view.entry, 64, true);
        if (error != ESP_OK) return error;
        view.page = LocalWifiPage::Password;
    }
    return attach_local_wifi(scene);
}

// No parameters: the keyboard is a local presentation, independent of password content.
bool onboarding_wifi_keyboard_active()
{
    return s_local_wifi.page == LocalWifiPage::Password;
}

// No parameters: QR visibility follows the current scene and its prepared modules.
bool onboarding_qr_visible()
{
    return (s_route == Route::Install06 || s_route == Route::Pairing07) && s_view.qr.side != 0;
}

// opacity: Last completely displayed chrome opacity for Home touch admission.
void set_home_wifi_chrome_opacity(uint8_t opacity)
{
    s_home_wifi_chrome_opacity = opacity;
}

// No parameters: true only while the Home-launched Wi-Fi journey owns the scene.
bool home_wifi_setup_active()
{
    return s_home_wifi_flow && (s_route == Route::Invitation05 ||
        s_route == Route::Install06 || s_route == Route::Pairing07 ||
        (s_home_wifi_pairing_direct && s_route == Route::Failed08));
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
    s_setup_return = s_route;
    s_connections_from_options = false;
    s_startup_pending = false;
    s_startup_pair_missing = false;
    set_companion_audio_visible(false);
    s_codex_owned = false;
    wipe(s_codex_code, sizeof(s_codex_code));
    return enter_route(Route::SetupMenu, scene);
}

// scene: Worker-owned scene receiving Options from the physical button.
// Exception: Returns NOT_FINISHED while a device route is busy, or the original preparation failure.
esp_err_t open_companion_options(SceneFrame &scene)
{
    if (maintenance_pending() || s_command_pending ||
        s_work_busy.load(std::memory_order_acquire)) {
        return kDeviceFirstSetup ? ESP_ERR_NOT_FINISHED : ESP_OK;
    }
    if (s_route == Route::MakeItYours10) {
        return s_options_page == OptionsPage::Menu ? ESP_OK : show_options_page(scene, OptionsPage::Menu);
    }
    const bool entered_from_setup = s_route == Route::SetupMenu;
    if (!entered_from_setup) {
        if (!can_open_setup_menu()) {
            return kDeviceFirstSetup ? ESP_ERR_NOT_FINISHED : ESP_OK;
        }
        const esp_err_t error = open_setup_menu(scene);
        if (error != ESP_OK) {
            return error;
        }
    }
    s_options_from_setup = !kDeviceFirstSetup || entered_from_setup;
    return enter_route(Route::MakeItYours10, scene);
}

// scene: Restores Tools without losing the route retained by the original Options entry.
// Exception: Returns the original route or presentation error.
esp_err_t return_to_options_tools(SceneFrame &scene)
{
    const bool from_setup = s_options_from_setup;
    const auto setup_return = s_setup_return;
    const auto error = open_companion_options(scene);
    if (error == ESP_OK) {
        s_options_from_setup = from_setup;
        s_setup_return = setup_return;
    }
    return error;
}

// scene: Restores the Device section that opened About.
// Exception: Returns the original route or presentation error.
esp_err_t return_to_options_device(SceneFrame &scene)
{
    const auto error = return_to_options_tools(scene);
    if (error != ESP_OK) return error;
    return s_route == Route::MakeItYours10 ? show_options_page(scene, OptionsPage::MenuMaintenance) :
        ESP_ERR_INVALID_STATE;
}

// scene: Options has admitted a deliberate return to the dashboard.
esp_err_t open_device_dashboard(SceneFrame &scene)
{
    if ((!s_home_wifi_flow && s_route != Route::MakeItYours10) || maintenance_pending() ||
        s_pairing_owned || s_command_pending || s_work_busy.load(std::memory_order_acquire)) {
        return ESP_ERR_NOT_FINISHED;
    }
    const auto error = enter_route(Route::CompanionIdle16, scene);
    if (error == ESP_OK) {
        s_home_wifi_flow = false;
        s_home_wifi_pairing_direct = false;
        clear_text_entry(s_local_wifi.entry);
        clear_text_entry_view(s_local_wifi.keyboard);
        s_local_wifi.page = LocalWifiPage::None;
    }
    return error;
}

// scene: Display-owned route; active pairing is closed by its existing owner first.
// Exception: Returns NOT_FINISHED while cleanup runs or the original admission error.
esp_err_t open_device_home(SceneFrame &scene)
{
    if ((s_local_failure && s_failed_work == Work::Cancel) ||
        (s_home_closing_codex && s_codex_error_visible)) {
        s_home_closing_codex = false;
        return ESP_ERR_INVALID_STATE; // Keep the actual cleanup failure on screen.
    }
    if (maintenance_pending() || s_command_pending || s_work_busy.load(std::memory_order_acquire) ||
        s_codex_cleanup != CodexCleanup::None) return ESP_ERR_NOT_FINISHED;
    if (s_pairing_owned) {
        if (s_cancel_to_install) return ESP_ERR_NOT_FINISHED;
        LUNASHELL::ConnectivityObservation observation{};
        auto error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (error != ESP_OK) return error;
        const auto &pairing = observation.radios.pairing;
        if (!pairing.data_valid || pairing.data_stale || pairing.attempt_id != s_attempt) {
            return pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE;
        }
        error = enqueue(Work::Cancel, pairing.attempt_id);
        if (error != ESP_OK) return error;
        s_home_wifi_exit_pending = s_home_wifi_flow;
        s_cancel_to_install = true;
        error = prepare_action(OnboardingAction::Cancel, false);
        if (error == ESP_OK) error = show_pairing_progress(scene, "Closing pairing");
        return error == ESP_OK ? ESP_ERR_NOT_FINISHED : error;
    }
    if (s_codex_owned) {
        s_home_closing_codex = true;
        const auto error = begin_codex_cleanup(scene, CodexCleanup::Cancel);
        if (error != ESP_OK) return error;
        return s_codex_cleanup != CodexCleanup::None ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_STATE;
    }
    const auto error = enter_route(Route::CompanionIdle16, scene);
    if (error == ESP_OK) {
        s_home_closing_codex = false;
        s_home_wifi_flow = false;
        s_home_wifi_pairing_direct = false;
        clear_text_entry(s_local_wifi.entry);
        clear_text_entry_view(s_local_wifi.keyboard);
        s_local_wifi.page = LocalWifiPage::None;
    }
    return error;
}

// available: True after an authenticated Events page has been observed.
void set_onboarding_inbox_available(bool available)
{
    s_inbox_available = available;
    if (!available) s_inbox_question[0] = '\0';
}

// request_id: Exact identity of a visible queue row.
// Exception: Returns the original selection admission failure.
esp_err_t open_onboarding_inbox_question(const char *request_id)
{
    if (!s_inbox_available) return ESP_ERR_INVALID_STATE;
    const auto error = LUNASHELL_codex_event_select(request_id);
    if (error == ESP_OK) std::memcpy(s_inbox_question, request_id, sizeof(s_inbox_question));
    return error;
}

// No parameters: consume only a released widget selection admitted by Options.
WidgetOpenRequest take_widget_open_request()
{
    const auto request = s_widget_open_request;
    s_widget_open_request = WidgetOpenRequest::None;
    return request;
}

// visibility: Exact local draft copied before waking the existing action worker.
// Exception: Returns the original admission error without starting a write.
esp_err_t request_widget_visibility_save(const WidgetVisibility &visibility)
{
    if (!valid_widget_visibility(visibility)) {
        ESP_LOGE(kTag, "Widget visibility rejected: version=%u screens=0x%02x panels=0x%02x (%s)",
            unsigned(visibility.version), unsigned(visibility.screen_mask), unsigned(visibility.panel_mask),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (s_route != Route::MakeItYours10 || !on_options_menu() || s_command_pending ||
        s_worker == nullptr || s_work_busy.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Widget visibility save admission rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_command = {};
    s_command.work = Work::SaveWidgetVisibility;
    s_command.generation = s_generation;
    s_command.widget_visibility = visibility;
    s_command_pending = true;
    ++s_navigation_revision;
    s_work_busy.store(true, std::memory_order_release);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// error: Receives the actual settings result, even if the route changed during the write.
bool take_widget_visibility_save_result(esp_err_t &error)
{
    if (!s_result_ready.load(std::memory_order_acquire) || s_result.command.work != Work::SaveWidgetVisibility) {
        return false;
    }
    if (s_result.command.generation == s_generation) {
        s_command_pending = false;
        ++s_navigation_revision;
    }
    error = s_result.error;
    wipe(&s_result, sizeof(s_result));
    s_result_ready.store(false, std::memory_order_release);
    s_work_busy.store(false, std::memory_order_release);
    return true;
}

// No parameters: consume the confirmed local preference change exactly once.
bool take_preference_confirmation()
{
    const bool confirmed = s_preference_confirmation;
    s_preference_confirmation = false;
    return confirmed;
}

// scene: Visible reconnect page, without interpreting unavailable data as missing credentials.
// wifi: Coherent observation used for the network name during passive reconnect.
// error: Actual canonical Wi-Fi observation failure, or ESP_OK for a known disconnected state.
// changed: Receives whether presentation changed since the previous observation.
// passive: Hide all recovery actions while the saved-network grace is pending.
// Exception: Returns presentation failures; the optional radio observation error stays canonical.
esp_err_t present_network_wait(SceneFrame &scene, const LUNASHELL::WifiSnapshot &wifi,
    esp_err_t error, bool &changed, bool passive)
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
    esp_err_t presentation_error = enter_route(Route::NetworkWait, scene, &wifi);
    if (presentation_error == ESP_OK && !passive) {
        static constexpr char title[] = "Wi-Fi offline";
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
// Exception: Returns read/presentation failures; optional radio faults remain in canonical diagnostics.
esp_err_t evaluate_saved_setup(SceneFrame &scene, bool &changed)
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        return error;
    }
    const auto &wifi = observation.radios.wifi;
    const bool wifi_ready = wifi.data_valid && !wifi.data_stale && wifi.credentials_configured &&
        wifi.health == LUNASHELL::DeviceHealth::Ready && wifi.last_error == ESP_OK &&
        wifi.started && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    if (kDeviceFirstSetup && s_route == Route::Hello04) {
        s_startup_offline_home = true;
    }
    const bool pair_missing = s_startup_pair_missing || s_startup_offline_home;
    if (!wifi_ready) {
        error = wifi.last_error != ESP_OK ? wifi.last_error :
            !wifi.data_valid || wifi.data_stale ? ESP_ERR_INVALID_STATE : ESP_OK;
        if (saved_wifi_startup_grace(wifi, esp_timer_get_time())) {
            s_startup_pending = true;
            return present_network_wait(scene, wifi, error, changed, true);
        }
        if (kDeviceFirstSetup && (s_startup_offline_home || (pair_missing && wifi.credentials_configured))) {
            s_startup_offline_home = true;
            s_startup_pending = false;
            s_startup_wifi_error = error;
            s_startup_network_passive = false;
            s_startup_network_presented = false;
            // A direct Back or Later entry is refreshed from this canonical observation.
            const bool has_saved_caption = s_route == Route::Hello04 && s_view.line_count > 1;
            if (s_route == Route::Hello04 && has_saved_caption == wifi.credentials_configured) {
                return ESP_OK;
            }
            changed = true;
            return enter_route(Route::Hello04, scene, &wifi);
        }
        if (pair_missing) {
            if (wifi.data_valid && !wifi.data_stale && wifi.connection_state_valid &&
                !wifi.credentials_configured) {
                s_startup_pending = false;
                s_startup_pair_missing = false;
                changed = true;
                return kDeviceFirstSetup ? enter_route(Route::Hello04, scene) :
                    start_wifi_pairing(scene, observation.radios.pairing);
            }
            return present_network_wait(scene, wifi, error, changed, false);
        }
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Companion startup retains Wi-Fi failure (%s)", esp_err_to_name(error));
        }
        // Returning devices remain usable while the existing radio owners retry.
        // Missing or failed credentials are never repaired or rewritten here.
        s_startup_wifi_error = error;
    } else {
        s_startup_wifi_error = ESP_OK;
    }
    s_startup_pending = false;
    s_startup_pair_missing = false;
    s_startup_offline_home = false;
    s_startup_network_passive = false;
    s_startup_network_presented = false;
    changed = true;
    if (!wifi_ready || !kCodexIntegrationEnabled) {
        return enter_route(Route::CompanionIdle16, scene);
    }
    const auto &link = observation.radios.codex_link;
    if (pair_missing) {
        if (!link.data_valid || link.data_stale || link.pairing_repair_required ||
            (!link.retained_link_present && !link.pending_pairing_present)) {
            return begin_codex_connection(scene, CodexConnectionIntent::Setup);
        }
        if (link.state == CodexState::Connected) {
            return enter_route(Route::Connected15, scene);
        }
        if (link.state == CodexState::Failed || link.state == CodexState::TimedOut) {
            return show_codex_error(scene, link.last_error != ESP_OK ? link.last_error : ESP_ERR_INVALID_STATE);
        }
        // The retained-link owner is already reconnecting on the new LAN.
        s_codex_owned = true;
        s_codex_attempt = link.attempt_id;
        s_last_codex_change_us = -1;
        return enter_route(Route::LookingForPc14, scene);
    }
    if (!link.data_valid || link.data_stale || link.pairing_repair_required ||
        (!link.retained_link_present && !link.pending_pairing_present && link.last_error != ESP_OK)) {
        error = link.last_error != ESP_OK ? link.last_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Companion startup retains CCP failure (%s)", esp_err_to_name(error));
        return enter_route(Route::CompanionIdle16, scene);
    }
    // The retained-link acquisition owner resumes discovery independently of UI
    // ownership. Startup never opens pairing or admits a second discovery request.
    return enter_route(Route::CompanionIdle16, scene);
}

// scene: Wi-Fi is committed; the next route uses the current CCP association.
esp_err_t finish_wifi_setup(SceneFrame &scene, bool &changed)
{
    s_pairing_owned = false;
    request_saved_setup_startup();
    s_startup_pair_missing = true;
    return evaluate_saved_setup(scene, changed);
}

// scene: Current first-setup scene, always owned by the existing display worker.
// completed_scene: Last fully retired text; never advance from a failed/partial frame.
// active_time_us: Existing animation time, excluding standby and pause.
// inactivity_active_time_us: Awake route time, including animation pause and excluding standby.
// changed: Receives whether scene geometry or content needs a new frame.
// scene_presented: Whether this scene was the last fully visible frame.
// Exception: Returns the original infrastructure or route error.
esp_err_t update_onboarding(SceneFrame &scene, const SceneFrame &completed_scene,
                            int64_t active_time_us, int64_t inactivity_active_time_us, bool &changed,
                            bool scene_presented)
{
    changed = false;
    const esp_err_t activity_error = synchronize_screen_activity(inactivity_active_time_us);
    if (activity_error != ESP_OK) {
        return activity_error;
    }
    if (s_local_wifi.page != LocalWifiPage::None)
        return update_local_wifi(scene, active_time_us, scene_presented, changed);
    if (s_route == Route::MakeItYours10 && s_options_page == OptionsPage::ResetPending) {
        return update_factory_reset(scene, changed);
    }
    if (s_route == Route::MakeItYours10 && s_options_page == OptionsPage::RestartPending) {
        return ESP_OK;
    }
    // Do not consume a pairing worker result until its canonical snapshot can be
    // read: the attempt identity and cleanup state are needed to process it.
    LUNASHELL::ConnectivityObservation direct_observation{};
    const esp_err_t direct_observation_error = s_home_wifi_pairing_direct ?
        LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(direct_observation) : ESP_OK;
    Result result;
    bool have_result = direct_observation_error == ESP_OK && s_result_ready.load(std::memory_order_acquire) &&
        s_result.command.work != Work::SaveWidgetVisibility;
    if (have_result) {
        result = s_result;
        wipe(&s_result, sizeof(s_result));
        s_result_ready.store(false, std::memory_order_release);
        s_work_busy.store(false, std::memory_order_release);
    }
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    if (have_result && result.command.work == Work::CheckRelay) {
        if (result.command.generation == s_generation && s_route == Route::SetupMenu) {
            s_command_pending = false;
            s_relay_result = result.relay;
            ++s_navigation_revision;
            changed = true;
            const auto error = prepare_relay_footer();
            return error == ESP_OK ? attach_content(scene) : error;
        }
        have_result = false;
    }
#endif
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
    const bool observing_offline_home = kDeviceFirstSetup && s_route == Route::Hello04;
    if (s_startup_pending || observing_offline_home) {
        wipe(&result, sizeof(result));
        const esp_err_t startup_error = evaluate_saved_setup(scene, changed);
        if (startup_error != ESP_OK || changed || s_route == Route::None) {
            return startup_error;
        }
        // A pending reconnect keeps its current decision page while the radio retries.
    }
    if (s_route == Route::None) {
        wipe(&result, sizeof(result));
        return ESP_OK;
    }
    if (s_route == Route::Hello04 && !kDeviceFirstSetup) {
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
    uint16_t preceding = scene.definition->title_visible ? scene.title.text.length : 0;
    bool caption_changed = false;
    if (s_route == Route::ApprovalOnPc) {
        const esp_err_t approval_error = advance_approval_notice(s_approval_notice, SceneData::body_font,
            scene.text_elapsed_us, actions_ready(scene), caption_changed);
        if (approval_error != ESP_OK) {
            ESP_LOGE(kTag, "Approval title advance failed (%s)", esp_err_to_name(approval_error));
            return approval_error;
        }
    }
    for (uint8_t index = 0; index < s_view.line_count; ++index) {
        const uint16_t visible = line_visible_characters(scene, index, preceding);
        caption_changed = caption_changed || visible != s_view.visible_characters[index];
        if (visible != 0 && is_codex_event_route(s_route) && index < s_event_plan.line_count) {
            caption_changed = caption_changed || s_view.lines[index].color !=
                event_presentation::line_color(s_event_plan.lines[index], scene.text_elapsed_us);
        }
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
    if (s_route == Route::MakeItYours10 && !on_options_menu() && !on_maintenance_page() && !on_quick_guide() &&
        !s_command_pending && !have_result && s_preference_error == ESP_OK) {
        LUNASHELL::CodyOptionsObservation options;
        const esp_err_t options_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
        if (options_error != ESP_OK) {
            wipe(&result, sizeof(result));
            return options_error;
        }
        bool pc_time_changed = false;
        if (s_options_page == OptionsPage::Time) {
            const auto pc_error = LUNASHELL::DeviceSnapshotService::instance().readCodexPresenceStatus(s_pc_time_observation);
            const auto &link = s_pc_time_observation.codex_link;
            const bool connected = pc_error == ESP_OK && link.data_valid && !link.data_stale && link.last_error == ESP_OK &&
                s_pc_time_observation.codex_link_state == LUNASHELL::CodexLinkState::Connected;
            const auto state = pc_error == ESP_OK ? s_pc_time_observation.pc_time_state : LUNASHELL::PcTimeSyncState::Failed;
            const auto state_error = pc_error == ESP_OK ? s_pc_time_observation.pc_time_error : pc_error;
            pc_time_changed = connected != s_presented_pc_time_connected || state != s_presented_pc_time_state ||
                state_error != s_presented_pc_time_error;
            if (pc_time_changed && (state == LUNASHELL::PcTimeSyncState::Pending ||
                state == LUNASHELL::PcTimeSyncState::Applied)) s_manual_pc_time_error = ESP_OK;
        }
        if (options_presentation_changed(options) || pc_time_changed) {
            esp_err_t refresh_error = prepare_options_content(scene, &options);
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
    esp_err_t error = direct_observation_error;
    if (s_home_wifi_pairing_direct) observation = direct_observation;
    else error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) {
        wipe(&result, sizeof(result));
        if (s_home_wifi_pairing_direct) {
            // Preserve outstanding work and its result for the next valid read.
            if (s_command_pending || s_result_ready.load(std::memory_order_acquire)) return ESP_OK;
            if (s_route == Route::Failed08 && s_visible_error == error) return ESP_OK;
            changed = true;
            return show_home_wifi_snapshot_error(scene, error);
        }
        return error;
    }
    const auto &pairing = observation.radios.pairing;
    const auto &codex_link = observation.radios.codex_link;
    if (s_view.wifi_menu && update_wifi_menu_name(s_view, observation.radios.ble)) {
        error = attach_content(scene);
        if (error != ESP_OK) {
            wipe(&result, sizeof(result));
            return error;
        }
        changed = true;
    }
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
        if ((result.command.work == Work::SavePreference || result.command.work == Work::ScheduleBirthIntro) &&
            s_route == Route::MakeItYours10) {
            if (result.command.work == Work::SavePreference && result.preference_changed)
                s_preference_confirmation = true;
            s_preference_error = result.error;
            s_retry_birth_intro = result.command.work == Work::ScheduleBirthIntro && result.error != ESP_OK;
            if (result.command.work == Work::ScheduleBirthIntro && result.error == ESP_OK)
                s_birth_intro_scheduled = true;
            if (result.error == ESP_OK && result.command.work == Work::SavePreference &&
                (result.command.preference == PreferenceId::QuietStart || result.command.preference == PreferenceId::QuietEnd) &&
                s_options_page == OptionsPage::QuietEdit) {
                error = show_options_page(scene, OptionsPage::QuietHours);
            } else {
                error = prepare_options_content(scene);
                if (error == ESP_OK) error = attach_content(scene);
            }
        } else if (result.error != ESP_OK) {
            if (result.command.work == Work::Cancel) s_home_wifi_exit_pending = false;
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
        } else if ((result.command.work == Work::InstallQr || result.command.work == Work::InstallQrApple) && s_route == Route::Install06) {
            s_local_failure = false;
            s_view.qr = result.qr;
            error = ESP_OK;
            if (error == ESP_OK) {
                error = attach_content(scene);
            }
        } else if (result.command.work == Work::SessionQr && s_pairing_owned && pairing.data_valid &&
                   pairing.attempt_id == s_attempt && pairing.state == PairState::ReadyToScan &&
                   pairing.provisioning_session_id == result.command.session) {
            // The session QR now belongs to screen 07, not an intermediate route.
            static constexpr char title[] = "Scan to pair";
            static constexpr char home_title[] = "Pair Cody";
            // A retried QR copy may complete while the local error scene is active.
            error = s_route == Route::Pairing07 ? ESP_OK : enter_route(Route::Pairing07, scene);
            if (error == ESP_OK) {
                ++s_text_generation;
                error = s_home_wifi_flow ? set_scene_title(scene, {home_title, sizeof(home_title) - 1U}) :
                    set_scene_title(scene, {title, sizeof(title) - 1U});
            }
            if (error == ESP_OK) {
                s_local_failure = false;
                s_view.qr = result.qr;
                s_view.qr_bounds = kSessionQrBounds;
                s_view.line_count = 0;
                s_session = result.command.session;
                error = prepare_action(OnboardingAction::Cancel, true);
                if (error == ESP_OK) error = attach_content(scene);
            }
        } else if (result.command.work == Work::Start || result.command.work == Work::Retry) {
            s_local_failure = false;
            s_attempt = result.command.attempt + 1U; // Backend admission increments exactly once.
            s_pairing_owned = true;
            s_last_pairing_change_us = -1;
        } else if (result.command.work == Work::CompleteFirstBoot || result.command.work == Work::UseCompanion) {
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
    const bool companion = is_companion_route(s_route) && !(s_route == Route::LookingForPc14 && s_codex_owned);
    const bool idle_options = s_route == Route::MakeItYours10 && !s_command_pending &&
        !s_work_busy.load(std::memory_order_acquire);
    if (companion || idle_options) {
        bool event_handled = false;
        const esp_err_t event_error = present_codex_event(codex_link, scene, active_time_us,
            inactivity_active_time_us, changed, event_handled);
        if (event_error != ESP_OK || event_handled) {
            return event_error;
        }
        if (companion) {
            return present_companion_activity(active_time_us, scene, changed);
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
                wipe(s_codex_code, sizeof(s_codex_code));
                error = resume_codex_connection(scene, codex_link);
            } else if (codex_link.state == CodexState::Connected &&
                       s_route == Route::LookingForPc14) {
                s_codex_owned = false;
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
                codex_link.pending_pairing_present ? "Finishing pairing" : "Open Codex on your PC";
            const char *detail = "Pairing stays saved";
            const uint16_t length = static_cast<uint16_t>(std::strlen(caption));
            if (s_view.lines[0].text.length != length ||
                std::memcmp(s_view.lines[0].text.characters, caption, length) != 0) {
                if (kDeviceFirstSetup) {
                    s_view.header_icon = lan_ready ? CODY2D::ActionIcon::None : CODY2D::ActionIcon::Link;
                    s_view.header_icon_bounds = kCodexPairingGlyph;
                    s_view.header_icon_color = lan_ready ? 0 : kPairingCaption;
                }
                error = prepare_caption(caption, kPcWaitCaptionTop, s_view.lines[0]);
                if (error == ESP_OK) {
                    error = prepare_caption(detail, kPcWaitDetailTop, s_view.lines[1]);
                    if (kDeviceFirstSetup) {
                        s_view.lines[1].color = kPairingCaption;
                    }
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
            s_route == Route::LookingForPc14) {
            return ESP_OK;
        }
        if (is_companion_route(s_route)) {
            return present_companion_activity(active_time_us, scene, changed);
        }
    }
    if (!s_pairing_owned) {
        return ESP_OK;
    }
    if (!pairing.data_valid || pairing.data_stale || pairing.attempt_id != s_attempt) {
        if (s_route != Route::Failed08) {
            changed = true;
            const auto pairing_error = pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE;
            return s_home_wifi_pairing_direct ? show_home_wifi_snapshot_error(scene, pairing_error) :
                show_error(scene, pairing_error);
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
            const bool return_to_invitation = s_retry_to_invitation || s_pairing_from_guide;
            const bool retry_direct = s_home_wifi_pairing_direct && s_retry_to_invitation;
            s_pairing_owned = false;
            s_cancel_to_install = false;
            s_retry_to_invitation = false;
            if (s_home_wifi_exit_pending) {
                s_home_wifi_exit_pending = false;
                s_widget_open_request = WidgetOpenRequest::Home;
                error = enter_route(Route::Invitation05, scene);
            } else if (retry_direct) {
                error = start_wifi_pairing(scene, pairing);
            } else {
                error = return_to_invitation ? enter_route(Route::Invitation05, scene) : open_installation_qr(scene);
            }
        } else if ((pairing.state == PairState::Failed || pairing.state == PairState::TimedOut) &&
                   !s_command_pending) {
            s_retry_to_invitation = false;
            s_cancel_to_install = false;
            s_local_failure = false;
            error = show_error(scene, pairing.cleanup_error != ESP_OK ? pairing.cleanup_error : pairing.last_error);
            if (error == ESP_OK && kDeviceFirstSetup && pairing.cleanup_error == ESP_OK) {
                using Reason = LUNASHELL::ProvisioningFailureReason;
                const char *detail = nullptr;
                if (pairing.state == PairState::TimedOut) {
                    detail = "Pairing timed out";
                } else if (pairing.failure_reason == Reason::AccessPointNotFound) {
                    detail = "Network not found";
                } else if (pairing.failure_reason == Reason::Authentication) {
                    detail = "Wi-Fi login failed";
                }
                if (detail != nullptr) {
                    error = prepare_caption(detail, 300, s_view.lines[0]);
                }
            }
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
                error = s_pairing_from_guide ? enter_route(Route::Invitation05, scene) : open_installation_qr(scene);
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
        const bool online = wifi_online_for_pairing(wifi);
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
        } else if (online) {
            return finish_wifi_setup(scene, changed);
        } else if (s_route != Route::Online09) {
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
            error = enter_route(Route::Online09, scene, &wifi);
            if (error == ESP_OK && !kDeviceFirstSetup && !online) {
                // TextSpan is borrowed by later frames, not copied by set_scene_title.
                static constexpr char title[] = "No connection";
                error = set_scene_title(scene, {title, sizeof(title) - 1U});
                if (error == ESP_OK) {
                    error = prepare_caption(wifi.data_valid && !wifi.data_stale ? "Wi-Fi disconnected" : "UNAVAILABLE",
                        300, s_view.lines[0]);
                }
            }
            if (error == ESP_OK && !kDeviceFirstSetup) {
                error = attach_content(scene);
            }
            if (error == ESP_OK) {
                s_online_link = online;
                s_online_presentation_current = true;
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
    if (s_local_wifi.page != LocalWifiPage::None) return true;
    if (s_home_wifi_pairing_direct && s_route == Route::Failed08) return true;
    if (is_codex_event_route(s_route)) {
        return s_event_plan.back;
    }
    return s_route == Route::SetupMenu || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
        ((kDeviceFirstSetup || s_home_wifi_flow) && s_route == Route::Invitation05) ||
        s_route == Route::Install06 ||
        options_back_available() ||
        (s_view.action_visible && s_view.action_id == OnboardingAction::Cancel);
}

// No parameters: forward actions retain their backend admission and enabled state.
bool next_navigation_available()
{
    if (s_home_wifi_flow && s_route == Route::Invitation05 && s_home_wifi_intro) return true;
    if (s_home_wifi_flow && s_route == Route::Install06) return true;
    if (is_codex_event_route(s_route)) {
        return false; // Only the visible explicit button can Send, dismiss or open an event.
    }
    if (s_route == Route::CodexChoice || s_route == Route::SetupMenu ||
        s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
        ((kDeviceFirstSetup || s_home_wifi_flow) && s_route == Route::Invitation05)) {
        return false; // Yes/No and setup choices require their own explicit capsule.
    }
    return s_view.action_visible && s_view.action_id != OnboardingAction::Cancel;
}

// No parameters: hidden lateral arrows do not retain invisible touch targets.
bool previous_control_visible()
{
    if ((s_route == Route::Install06 || s_route == Route::Pairing07) && s_view.qr.side != 0) return false;
    if (s_local_wifi.page != LocalWifiPage::None) return !s_command_pending;
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    if (s_route == Route::SetupMenu && s_command_pending && s_relay_attempted) { return false; }
#endif
    if (s_home_wifi_pairing_direct && s_route == Route::Failed08) return s_view.actions_visible;
    if (s_home_wifi_flow && s_route == Route::Pairing07) return s_view.actions_visible &&
        s_view.action_id == OnboardingAction::Cancel && s_view.action.enabled;
    if (options_previous_page_by_swipe()) return false;
    if (on_quick_guide()) return s_view.actions_visible && options_back_available();
    if (is_codex_event_route(s_route)) {
        return s_view.actions_visible && previous_navigation_available();
    }
    return s_route == Route::SetupMenu || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
        ((kDeviceFirstSetup || s_home_wifi_flow) && (s_route == Route::Invitation05 || s_route == Route::Install06)) ||
        options_back_available() ||
        (previous_navigation_available() && s_route != Route::Install06 && !uses_footer_action());
}

// No parameters: forward paging is available through a left swipe.
bool next_control_visible()
{
    return false;
}

// scene: Current route, never advanced on unconfirmed cancellation/cleanup.
// direction: Explicit previous/next intent, shared by tap and released swipe.
// from_footer: True for the captioned action, distinct from Options pagination.
// Exception: Returns the real persistence, snapshot, command or route error.
esp_err_t activate_onboarding_navigation(SceneFrame &scene, NavigationDirection direction, bool from_footer = false)
{
    if (s_timeout_cleanup.pending) {
        return ESP_OK;
    }
    if (on_quick_guide() && !s_view.actions_visible) return ESP_OK;
    const bool page_forward = direction == NavigationDirection::Next && !from_footer && options_next_page_available();
    const bool back_hit = direction == NavigationDirection::Previous && previous_navigation_available();
    const bool action_hit = page_forward || (direction == NavigationDirection::Next && next_navigation_available());
    if (!back_hit && !action_hit) {
        return ESP_OK;
    }
    const bool cancel_hit = back_hit && s_view.action_id == OnboardingAction::Cancel;
    if ((action_hit || cancel_hit) && !s_view.actions_visible) {
        return ESP_OK;
    }
    const bool home_page_forward = s_home_wifi_flow && action_hit && !from_footer &&
        (s_route == Route::Install06 || (s_route == Route::Invitation05 && s_home_wifi_intro));
    const bool uses_captioned_action = (action_hit && !page_forward && !home_page_forward) || cancel_hit;
    if (s_command_pending || s_work_busy.load(std::memory_order_acquire) ||
        (uses_captioned_action && !s_view.action.enabled)) {
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
        event_presentation::Action back_action;
        back_action.id = event_presentation::ActionId::Back;
        back_action.enabled = s_event_plan.back;
        return activate_event_action(scene, back_action);
    }
    if (back_hit && s_home_wifi_pairing_direct && s_route == Route::Failed08 && !s_pairing_owned) {
        s_widget_open_request = WidgetOpenRequest::Home;
        return ESP_OK;
    }
    if (s_home_wifi_flow && s_route == Route::Invitation05) {
        if (back_hit && s_home_wifi_intro) {
            if (s_home_wifi_pairing_direct) {
                s_widget_open_request = WidgetOpenRequest::Home;
                return ESP_OK;
            }
            s_home_wifi_intro = false;
            return enter_route(Route::Invitation05, scene);
        }
        if (back_hit) {
            if (s_pairing_guide_return == Route::SetupMenu) {
                const auto error = enter_route(Route::SetupMenu, scene);
                if (error == ESP_OK) {
                    s_home_wifi_flow = false;
                    s_home_wifi_pairing_direct = false;
                    s_pairing_guide_return = Route::None;
                }
                return error;
            }
            s_widget_open_request = WidgetOpenRequest::Home;
            return ESP_OK;
        }
        if (action_hit && s_home_wifi_intro) {
            LUNASHELL::ConnectivityObservation observation{};
            const auto error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
            return error == ESP_OK ? start_wifi_pairing(scene, observation.radios.pairing) : show_error(scene, error);
        }
    }
    if (s_home_wifi_flow && s_route == Route::Install06) {
        if (back_hit) {
            s_home_wifi_intro = true;
            return enter_route(Route::Invitation05, scene);
        }
        if (action_hit) {
            LUNASHELL::ConnectivityObservation observation{};
            const auto error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
            return error == ESP_OK ? start_wifi_pairing(scene, observation.radios.pairing) : show_error(scene, error);
        }
    }
    if (back_hit && s_route == Route::Install06) {
        return enter_route(Route::Invitation05, scene);
    }
    if (back_hit && kDeviceFirstSetup && s_route == Route::Invitation05) {
        return enter_route(s_pairing_guide_return, scene);
    }
    if (back_hit && s_route == Route::SetupMenu) {
        if (s_connections_from_options) {
            return enter_route(Route::MakeItYours10, scene);
        }
        if (kDeviceFirstSetup && (s_setup_return == Route::Hello04 || s_setup_return == Route::Invitation05)) {
            return enter_route(s_setup_return, scene);
        }
        if (kDeviceFirstSetup && s_setup_return == Route::Install06) {
            const auto error = open_installation_qr(scene);
            return error == ESP_OK ? ESP_OK : show_error(scene, error);
        }
        const bool return_to_companion = is_companion_route(s_setup_return) ||
            s_setup_return == Route::LookingForPc14;
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
    if (back_hit && s_route == Route::MakeItYours10) {
        if (s_options_page != OptionsPage::Menu) {
            s_preference_error = ESP_OK;
            auto parent = OptionsPage::MenuMore;
            if (s_options_page == OptionsPage::MenuMore) parent = OptionsPage::Menu;
            if (s_options_page == OptionsPage::BehaviorMore) parent = OptionsPage::Behavior;
            if (s_options_page == OptionsPage::BehaviorMoreSettings) parent = OptionsPage::BehaviorMore;
            if (s_options_page == OptionsPage::QuietHours) parent = OptionsPage::BehaviorMoreSettings;
            if (s_options_page == OptionsPage::QuietEdit) parent = OptionsPage::QuietHours;
            if (s_options_page == OptionsPage::DisplayRings) parent = OptionsPage::Display;
            if (s_options_page == OptionsPage::Time) parent = OptionsPage::DisplayRings;
            if (s_options_page == OptionsPage::SynthVoice) parent = OptionsPage::BehaviorMoreSettings;
            if (on_maintenance_page()) parent = OptionsPage::MenuMaintenance;
            if (s_options_page == OptionsPage::GuideButtons) parent = OptionsPage::MenuMaintenance;
            if (s_options_page == OptionsPage::GuideTouch) parent = OptionsPage::GuideButtons;
            if (s_options_page == OptionsPage::GuideOffline) parent = OptionsPage::GuideTouch;
            return show_options_page(scene, parent);
        }
        if (kDeviceFirstSetup && !s_options_from_setup) {
            if (s_setup_return == Route::None || is_companion_route(s_setup_return)) {
                s_widget_open_request = WidgetOpenRequest::ReturnFromOptions;
                return ESP_OK;
            }
            if (s_setup_return == Route::Install06) return open_installation_qr(scene);
            return enter_route(s_setup_return, scene);
        }
        return enter_route(Route::SetupMenu, scene);
    }
    if (action_hit && kDeviceFirstSetup && s_route == Route::Hello04) {
        s_pairing_guide_return = Route::Hello04;
        const auto error = enter_route(Route::Invitation05, scene);
        if (error == ESP_OK) {
            s_startup_pending = false;
            s_startup_pair_missing = false;
        }
        return error;
    }
    if (action_hit && s_route == Route::Invitation05) {
        const auto error = open_installation_qr(scene);
        if (error != ESP_OK) {
            return show_error(scene, error);
        }
        return ESP_OK;
    }
    if (action_hit && s_route == Route::Online09) {
        bool changed = false;
        return finish_wifi_setup(scene, changed);
    }
    if (action_hit && s_route == Route::MakeItYours10) {
        if (from_footer && on_options_menu() && s_view.action_id == OnboardingAction::OpenAbout) {
            s_widget_open_request = WidgetOpenRequest::About;
            return ESP_OK;
        }
        if (on_quick_guide()) {
            if (from_footer) return show_options_page(scene, OptionsPage::MenuMaintenance);
            if (!page_forward) return ESP_OK;
            const auto next = s_options_page == OptionsPage::GuideButtons ? OptionsPage::GuideTouch : OptionsPage::GuideOffline;
            return show_options_page(scene, next);
        }
        if (on_maintenance_page()) {
            if (!from_footer) {
                return ESP_OK;
            }
            if (s_options_page == OptionsPage::ResetConfirm &&
                s_view.action_id == OnboardingAction::ConfirmFactoryReset) {
                return confirm_factory_reset(scene);
            }
            if (s_options_page == OptionsPage::RestartConfirm &&
                s_view.action_id == OnboardingAction::ConfirmRestart) {
                s_restart_error = LUNASHELL_power_manager_request_restart();
                return show_options_page(scene, s_restart_error == ESP_OK
                    ? OptionsPage::RestartPending : OptionsPage::RestartConfirm);
            }
            if (s_options_page == OptionsPage::ResetFailed && s_view.action_id == OnboardingAction::Retry) {
                s_factory_reset_error = ESP_OK;
                return show_options_page(scene, OptionsPage::ResetConfirm);
            }
            return ESP_OK;
        }
        if (s_preference_error != ESP_OK) {
            if (!from_footer) return ESP_OK;
            esp_err_t error = s_retry_birth_intro ? enqueue(Work::ScheduleBirthIntro, 0) :
                enqueue_preference(s_requested_preference, s_requested_preference_value);
            s_preference_error = error;
            error = prepare_options_content(scene);
            return error == ESP_OK ? attach_content(scene) : error;
        }
        if (on_options_menu() && !from_footer) {
            if (!options_next_page_available()) return ESP_OK;
            const auto next = s_options_page == OptionsPage::Menu ? OptionsPage::MenuMore : OptionsPage::MenuMaintenance;
            return show_options_page(scene, next);
        }
        if (s_options_page == OptionsPage::Display &&
            (page_forward || s_view.action_id == OnboardingAction::MoreDisplay)) {
            return show_options_page(scene, OptionsPage::DisplayRings);
        }
        if (s_options_page == OptionsPage::DisplayRings && page_forward) {
            s_manual_pc_time_error = ESP_OK;
            return show_options_page(scene, OptionsPage::Time);
        }
        if (s_options_page == OptionsPage::DisplayRings && s_view.action_id == OnboardingAction::ScheduleBirthIntro) {
            if (!from_footer) return ESP_OK;
            const auto admission_error = enqueue(Work::ScheduleBirthIntro, 0);
            if (admission_error != ESP_OK) {
                s_preference_error = admission_error;
                s_retry_birth_intro = true;
            }
            const auto error = prepare_options_content(scene);
            return error == ESP_OK ? attach_content(scene) : error;
        }
        if (s_options_page == OptionsPage::Behavior && s_view.action_id == OnboardingAction::MoreBehavior) {
            return show_options_page(scene, OptionsPage::BehaviorMore);
        }
        if (s_options_page == OptionsPage::BehaviorMore &&
            (page_forward || s_view.action_id == OnboardingAction::MoreBehaviorSettings)) {
            return show_options_page(scene, OptionsPage::BehaviorMoreSettings);
        }
        if (s_options_page == OptionsPage::BehaviorMore && s_view.action_id == OnboardingAction::SynthOptions) {
            return show_options_page(scene, OptionsPage::SynthVoice);
        }
        if (!on_options_menu()) {
            return show_options_page(scene, OptionsPage::MenuMore);
        }
        if (s_options_from_setup) {
            return enter_route(Route::SetupMenu, scene);
        }
        return enter_route(kCodexIntegrationEnabled ? Route::CodexChoice : Route::SetupComplete13, scene);
    }
    if (action_hit && s_route == Route::ConnectCodex12) {
        if (s_view.action_id != OnboardingAction::Retry) {
            return ESP_OK;
        }
        return begin_codex_connection(scene, CodexConnectionIntent::Retry);
    }
    if (action_hit && s_route == Route::SetupComplete13) {
        if (!kCodexIntegrationEnabled) {
            return choose_local_companion(scene);
        }
        return begin_codex_connection(scene, CodexConnectionIntent::Retry);
    }
    if (action_hit && s_route == Route::LookingForPc14) {
        return choose_local_companion(scene);
    }
    if (s_route == Route::Failed08 && s_local_failure &&
        (s_failed_work == Work::InstallQr || s_failed_work == Work::InstallQrApple)) {
        // The installation QR is local; no pairing session was started by this work.
        s_local_failure = false;
        return enter_route(Route::Invitation05, scene);
    }
    if (s_route == Route::Failed08 && s_local_failure && s_failed_work == Work::UseCompanion) {
        // A failed local write needs no BLE/Wi-Fi state and must never restart pairing.
        return choose_local_companion(scene);
    }
    LUNASHELL::ConnectivityObservation observation;
    const esp_err_t observation_error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (observation_error != ESP_OK) {
        if (s_home_wifi_pairing_direct) return show_home_wifi_snapshot_error(scene, observation_error);
        return observation_error;
    }
    const auto &pairing = observation.radios.pairing;
    if (!pairing.data_valid || pairing.data_stale) {
        const auto error = pairing.last_error != ESP_OK ? pairing.last_error : ESP_ERR_INVALID_STATE;
        return s_home_wifi_pairing_direct ? show_home_wifi_snapshot_error(scene, error) : show_error(scene, error);
    }
    if (s_pairing_owned && pairing.attempt_id != s_attempt) {
        return show_error(scene, ESP_ERR_INVALID_STATE);
    }
    if (back_hit && s_home_wifi_pairing_direct && s_route == Route::Failed08) {
        const bool active_session = pairing.state == PairState::Starting ||
            pairing.state == PairState::ReadyToScan || pairing.state == PairState::Connecting ||
            pairing.state == PairState::Completing;
        const bool failed_cleanup = pairing.state == PairState::Failed && pairing.cleanup_error != ESP_OK;
        if (active_session || failed_cleanup || pairing.state == PairState::Cancelling) {
            s_home_wifi_exit_pending = true;
        } else if (pairing.cleanup_error == ESP_OK) {
            s_pairing_owned = false;
            s_widget_open_request = WidgetOpenRequest::Home;
            return ESP_OK;
        }
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
                s_home_wifi_exit_pending = false;
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
        // Clean terminal attempts can be left in place. The next pairing action
        // selects START or RETRY from the canonical state.
        s_pairing_owned = false;
        s_cancel_to_install = false;
        s_retry_to_invitation = false;
        s_local_failure = false;
        s_session = 0;
        return s_home_wifi_pairing_direct && action_hit ? start_wifi_pairing(scene, pairing) :
            enter_route(Route::Invitation05, scene);
    }
    if (s_route == Route::Install06) {
        return start_wifi_pairing(scene, pairing);
    }
    if (back_hit && s_home_wifi_pairing_direct && s_route == Route::Pairing07) {
        if (!s_pairing_owned) {
            s_widget_open_request = WidgetOpenRequest::Home;
            return ESP_OK;
        }
        s_home_wifi_exit_pending = false;
    }
    s_cancel_to_install = true;
    const esp_err_t request_error = enqueue(Work::Cancel, pairing.attempt_id);
    if (request_error != ESP_OK) {
        s_home_wifi_exit_pending = false;
        return show_error(scene, request_error);
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
    if (s_home_wifi_flow && s_route == Route::Invitation05) {
        if (s_home_wifi_intro && (action == OnboardingAction::AppleApp || action == OnboardingAction::AndroidApp)) {
            s_home_wifi_apple = action == OnboardingAction::AppleApp;
            return open_installation_qr(scene);
        }
        if (action == OnboardingAction::LocalWifi) {
            return open_local_wifi(scene);
        }
        if (action == OnboardingAction::PairCody) {
            s_home_wifi_intro = true;
            return enter_route(Route::Invitation05, scene);
        }
    }
    const char *label = nullptr;
    error = action_label(action, label);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(kTag, "Setup choice activated: %s", label != nullptr ? label : "None");
#if CONFIG_LUNASHELL_CODY_RELAY_PROBE
    if (s_route == Route::SetupMenu && action == OnboardingAction::CheckRelay) {
        s_relay_attempted = true;
        const auto admission_error = enqueue(Work::CheckRelay, 0);
        s_relay_result = {RelayProbeStage::Start, admission_error};
        const auto error = prepare_relay_footer();
        return error == ESP_OK ? attach_content(scene) : error;
    }
#endif
    if (s_home_wifi_flow && s_route == Route::Install06 &&
        (action == OnboardingAction::AndroidApp || action == OnboardingAction::AppleApp)) {
        const bool apple = action == OnboardingAction::AppleApp;
        if (apple == s_home_wifi_apple) return ESP_OK;
        s_home_wifi_apple = apple;
        return open_installation_qr(scene);
    }
    if (kDeviceFirstSetup && s_route == Route::Invitation05) {
        if (action == OnboardingAction::ShowQr) {
            error = open_installation_qr(scene);
            return error == ESP_OK ? ESP_OK : show_error(scene, error);
        }
        if (action == OnboardingAction::PairCody) {
            LUNASHELL::ConnectivityObservation observation{};
            error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
            return error == ESP_OK ? start_wifi_pairing(scene, observation.radios.pairing) : show_error(scene, error);
        }
    }
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
    if (kDeviceFirstSetup && s_route == Route::NetworkWait &&
        (action == OnboardingAction::Later || action == OnboardingAction::NetworkSetup)) {
        const auto destination = action == OnboardingAction::Later ? Route::Hello04 : Route::Invitation05;
        error = enter_route(destination, scene);
        if (error == ESP_OK && destination == Route::Invitation05) {
            s_startup_pending = false;
            s_startup_pair_missing = false;
            s_startup_network_presented = false;
            s_codex_owned = false;
            s_pairing_guide_return = Route::Hello04;
        }
        return error;
    }
    if (action == OnboardingAction::DeclineCodex || action == OnboardingAction::Later) {
        return choose_local_companion(scene);
    }
    if (action == OnboardingAction::NetworkSetup) {
        if (kDeviceFirstSetup && s_route == Route::SetupMenu) {
            return open_wifi_setup_from_home(scene);
        }
        s_startup_pending = false;
        s_codex_owned = false;
        if (kDeviceFirstSetup) {
            s_pairing_guide_return = Route::SetupMenu;
            return enter_route(Route::Invitation05, scene);
        }
        LUNASHELL::ConnectivityObservation observation{};
        error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
        return error == ESP_OK ? start_wifi_pairing(scene, observation.radios.pairing) : show_error(scene, error);
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
    if (s_local_wifi.page != LocalWifiPage::None) {
        return handle_local_wifi_touch(scene, point, pressed, pressed_edge, rotation_degrees, consumed);
    }
    if (s_route == Route::None) {
        return ESP_OK;
    }
    if ((s_route == Route::Install06 || s_route == Route::Pairing07) && s_view.qr.side != 0) {
        consumed = true;
        if (pressed_edge) {
            s_swipe_active = s_view.actions_visible && !s_command_pending;
            s_swipe_start = s_swipe_last = point;
            s_swipe_route = s_route;
            s_swipe_rotation = rotation_degrees;
        }
        if (s_swipe_route != s_route || s_swipe_rotation != rotation_degrees) s_swipe_active = false;
        if (pressed) s_swipe_last = point;
        if (!pressed && s_swipe_active) {
            s_swipe_active = false;
            const int dx = s_swipe_last.x - s_swipe_start.x;
            const int dy = s_swipe_last.y - s_swipe_start.y;
            if (dx >= 48 && dx * 2 > std::abs(dy) * 3)
                return activate_onboarding_navigation(scene, NavigationDirection::Previous);
        }
        return ESP_OK;
    }
    if (s_route == Route::Hello04 && !kDeviceFirstSetup) {
        if (!s_hello_waits_for_touch) {
            return ESP_OK;
        }
        // Timeout Welcome is an escape back to choices. Do not start Wi-Fi on
        // the first pressed sample: act only after a complete stationary contact.
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
        const LUNASHELL::WifiSnapshot *wifi = nullptr;
        if (kDeviceFirstSetup) {
            const auto error = LUNASHELL::DeviceSnapshotService::instance()
                .readHardwareStatus(s_connection_status);
            if (error != ESP_OK) { return error; }
            wifi = &s_connection_status.wifi;
        }
        const auto error = enter_route(Route::Online09, scene, wifi);
        if (error == ESP_OK) { s_pairing_debug_dismissed = true; }
        return error;
    }
#endif
    if (s_swipe_route != s_route || (s_swipe_active && s_swipe_rotation != rotation_degrees)) {
        s_swipe_active = false;
    }
    const CODY2D::CapsuleControl back{kBackBounds, {}, NavigationData::top, NavigationData::bottom, true};
    const CODY2D::CapsuleControl next{kForwardBounds, {}, NavigationData::top, NavigationData::bottom, true};
    if (pressed_edge) {
        s_swipe_moved = false;
        s_options_tap_admitted = false;
        if (is_codex_event_route(s_route)) {
            ScenePresentation presentation;
            const esp_err_t presentation_error = prepare_scene_presentation(scene, presentation);
            if (presentation_error != ESP_OK) { return presentation_error; }
            if (!s_view.actions_visible || !presentation.content_visible) { return ESP_OK; }
            if (event_presentation::hit_back(s_event_plan, true, point.x, point.y)) {
                consumed = true;
                return activate_onboarding_navigation(scene, NavigationDirection::Previous);
            }
            // Rows and the review footer act only on release. A vertical drag
            // starting on a row must scroll instead of prematurely choosing it.
            s_swipe_active = point_in_rect({73, 196, 320, 228}, point);
            s_swipe_start = s_swipe_last = point;
            s_swipe_route = s_route;
            s_swipe_rotation = rotation_degrees;
            s_swipe_axis = GestureAxis::Undecided;
            consumed = s_swipe_active;
            return ESP_OK;
        }
        if (s_route == Route::MakeItYours10 && s_view.actions_visible && !s_command_pending) {
            for (uint8_t index = 0; index < s_view.preference_count; ++index) {
                if (CODY2D::capsule_contains(s_view.preferences[index].control, point)) {
                    s_options_tap_admitted = true;
                    break;
                }
            }
        }
        const bool choice_route = s_route == Route::CodexChoice || s_route == Route::SetupMenu ||
            s_route == Route::NetworkWait || s_route == Route::CodexReplace || s_route == Route::CodexForget ||
            (kDeviceFirstSetup && s_route == Route::Invitation05) ||
            (s_home_wifi_flow && s_route == Route::Invitation05) ||
            (s_home_wifi_flow && s_route == Route::Install06);
        if (choice_route && s_view.actions_visible) {
            const auto action_hit = [&](const CODY2D::CapsuleControl &control) {
                return s_view.wifi_menu || s_view.action_icon_size != 0
                    ? control.enabled && point_in_rect(control.bounds, point)
                    : CODY2D::capsule_contains(control, point);
            };
            const bool primary_hit = s_view.action_visible && action_hit(s_view.action);
            const bool alternative_hit = s_view.alternative_id != OnboardingAction::None &&
                action_hit(s_view.alternative);
            const bool options_hit = s_view.options.caption.valid &&
                (s_view.options_id == OnboardingAction::CheckRelay ?
                    CODY2D::capsule_contains(s_view.options, point) : action_hit(s_view.options));
            if (primary_hit || alternative_hit || options_hit) {
                consumed = true;
                const CODY2D::CapsuleControl *control = &s_view.options;
                OnboardingAction action = s_view.options_id;
                const char *slot = "options";
                if (primary_hit) {
                    control = &s_view.action;
                    action = s_view.action_id;
                    slot = "primary";
                } else if (alternative_hit) {
                    control = &s_view.alternative;
                    action = s_view.alternative_id;
                    slot = "alternative";
                }
                ESP_LOGI(kTag, "Setup action pressed: x=%d y=%d slot=%s", point.x, point.y,
                    slot);
                return control->enabled ? activate_setup_choice(scene, action) : ESP_OK;
            }
        }
        const bool footer_hit = uses_footer_action() && s_view.actions_visible && s_view.action_visible &&
            CODY2D::capsule_contains(s_view.action, point);
        if (footer_hit && s_route == Route::MakeItYours10 && released_options_confirmation(s_view.action_id)) {
            s_options_tap_admitted = true;
        } else if (footer_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene, s_view.action_id == OnboardingAction::Cancel ?
                NavigationDirection::Previous : NavigationDirection::Next, true);
        }
        const bool back_hit = previous_control_visible() && CODY2D::capsule_contains(back, point);
        const bool next_hit = next_control_visible() && CODY2D::capsule_contains(next, point);
        if (back_hit || next_hit) {
            consumed = true;
            return activate_onboarding_navigation(scene,
                back_hit ? NavigationDirection::Previous : NavigationDirection::Next);
        }
        // Only an unmistakable horizontal release navigates on an ordinary setup page.
        s_swipe_active = s_options_tap_admitted || point_in_rect({73, 73, 320, 320}, point);
        s_swipe_start = point;
        s_swipe_last = point;
        s_swipe_route = s_route;
        s_swipe_rotation = rotation_degrees;
        s_swipe_axis = GestureAxis::Undecided;
    }
    if (pressed && s_swipe_active) {
        consumed = s_options_tap_admitted;
        s_swipe_last = point;
        const int dx = std::abs(point.x - s_swipe_start.x);
        const int dy = std::abs(point.y - s_swipe_start.y);
        if (std::max(dx, dy) >= 12) { s_swipe_moved = true; }
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
        if (is_codex_event_route(s_route)) {
            consumed = true;
            auto navigation = event_navigation();
            const event_presentation::Action *action = nullptr;
            const auto effect = event_presentation::release(s_event_plan, navigation, s_view.actions_visible,
                s_swipe_start.x, s_swipe_start.y, s_swipe_last.x, s_swipe_last.y, action);
            // An excursion followed by a return to the origin is still a drag.
            if (s_swipe_moved && std::abs(dx) < 12 && std::abs(dy) < 12) {
                return ESP_OK;
            }
            if (effect == event_presentation::Effect::Render) {
                retain_event_navigation(navigation, effect);
                return render_codex_event(scene, true);
            }
            return action != nullptr ? activate_event_action(scene, *action) : ESP_OK;
        }
        if (s_swipe_axis == GestureAxis::Horizontal && std::abs(dx) >= 48 &&
            std::abs(dx) * 2 > std::abs(dy) * 3) {
            consumed = true;
            return activate_onboarding_navigation(scene,
                dx < 0 ? NavigationDirection::Next : NavigationDirection::Previous);
        }
        if (s_route == Route::MakeItYours10 && s_options_tap_admitted) {
            consumed = true;
            if (s_swipe_moved || !s_view.actions_visible || s_command_pending) {
                return ESP_OK;
            }
            // The BSP must observe the release before the worker may rotate panel and touch.
            if (released_options_confirmation(s_view.action_id) &&
                CODY2D::capsule_contains(s_view.action, s_swipe_start) &&
                CODY2D::capsule_contains(s_view.action, s_swipe_last)) {
                return activate_onboarding_navigation(scene, NavigationDirection::Next, true);
            }
            for (uint8_t index = 0; index < s_view.preference_count; ++index) {
                const auto &row = s_view.preferences[index];
                if (!CODY2D::capsule_contains(row.control, s_swipe_start) ||
                    !CODY2D::capsule_contains(row.control, s_swipe_last)) {
                    continue;
                }
                switch (row.action) {
                case OnboardingAction::Connections:
                    s_connections_from_options = kDeviceFirstSetup;
                    return enter_route(Route::SetupMenu, scene);
                case OnboardingAction::DisplayOptions:
                    return show_options_page(scene, OptionsPage::Display);
                case OnboardingAction::MoreDisplay:
                    return show_options_page(scene, OptionsPage::DisplayRings);
                case OnboardingAction::MoreBehavior:
                    return show_options_page(scene, OptionsPage::BehaviorMore);
                case OnboardingAction::MoreBehaviorSettings:
                    return show_options_page(scene, OptionsPage::BehaviorMoreSettings);
                case OnboardingAction::QuietHours:
                    return show_options_page(scene, OptionsPage::QuietHours);
                case OnboardingAction::QuietStart:
                case OnboardingAction::QuietEnd:
                    s_quiet_edit_start = row.action == OnboardingAction::QuietStart;
                    s_quiet_draft = s_quiet_edit_start ? s_quiet_clock.quiet_start_minute : s_quiet_clock.quiet_end_minute;
                    return show_options_page(scene, OptionsPage::QuietEdit);
                case OnboardingAction::QuietHour:
                case OnboardingAction::QuietMinute: {
                    const int direction = s_swipe_start.x < kPreferenceLeft + kPreferenceWidth / 2 ? -1 : 1;
                    if (row.action == OnboardingAction::QuietHour) {
                        s_quiet_draft = static_cast<uint16_t>((s_quiet_draft + direction * 60 + 1440) % 1440);
                    } else {
                        const int hour = s_quiet_draft / 60;
                        const int minute = (s_quiet_draft % 60 + direction + 60) % 60;
                        s_quiet_draft = static_cast<uint16_t>(hour * 60 + minute);
                    }
                    auto error = prepare_options_content(scene);
                    return error == ESP_OK ? attach_content(scene) : error;
                }
                case OnboardingAction::QuietToggle:
                case OnboardingAction::QuietSave: {
                    const auto preference = row.action == OnboardingAction::QuietToggle ? PreferenceId::QuietEnabled :
                        s_quiet_edit_start ? PreferenceId::QuietStart : PreferenceId::QuietEnd;
                    const uint32_t value = row.action == OnboardingAction::QuietToggle ?
                        static_cast<uint32_t>(!s_quiet_clock.quiet_hours_enabled) : s_quiet_draft;
                    auto error = enqueue_preference(preference, value);
                    if (error != ESP_OK) s_preference_error = error;
                    error = prepare_options_content(scene);
                    return error == ESP_OK ? attach_content(scene) : error;
                }
                case OnboardingAction::SynthOptions:
                    return show_options_page(scene, OptionsPage::SynthVoice);
                case OnboardingAction::OpenAbout:
                    s_widget_open_request = WidgetOpenRequest::About;
                    return ESP_OK;
                case OnboardingAction::ScheduleBirthIntro: {
                    const auto admission_error = enqueue(Work::ScheduleBirthIntro, 0);
                    if (admission_error != ESP_OK) {
                        s_preference_error = admission_error;
                        s_retry_birth_intro = true;
                    }
                    const auto error = prepare_options_content(scene);
                    return error == ESP_OK ? attach_content(scene) : error;
                }
                case OnboardingAction::Retry: {
                    if (s_options_page == OptionsPage::ResetFailed) {
                        s_factory_reset_error = ESP_OK;
                        return show_options_page(scene, OptionsPage::ResetConfirm);
                    }
                    if (s_preference_error == ESP_OK) return ESP_OK;
                    auto error = s_retry_birth_intro ? enqueue(Work::ScheduleBirthIntro, 0) :
                        enqueue_preference(s_requested_preference, s_requested_preference_value);
                    s_preference_error = error;
                    error = prepare_options_content(scene);
                    return error == ESP_OK ? attach_content(scene) : error;
                }
                case OnboardingAction::OpenTime:
                    s_manual_pc_time_error = ESP_OK;
                    return show_options_page(scene, OptionsPage::Time);
                case OnboardingAction::SyncPcTime: {
                    if (!row.control.enabled) return ESP_OK;
                    s_manual_pc_time_error = LUNASHELL_codex_request_pc_time();
                    if (s_manual_pc_time_error != ESP_OK) {
                        ESP_LOGW(kTag, "PC time request failed (%s)", esp_err_to_name(s_manual_pc_time_error));
                    }
                    auto error = prepare_options_content(scene);
                    return error == ESP_OK ? attach_content(scene) : error;
                }
                case OnboardingAction::BehaviorOptions:
                    return show_options_page(scene, OptionsPage::Behavior);
                case OnboardingAction::FactoryReset:
                    s_factory_reset_error = ESP_OK;
                    return show_options_page(scene, OptionsPage::ResetConfirm);
                case OnboardingAction::RestartDevice:
                    s_restart_error = ESP_OK;
                    return show_options_page(scene, OptionsPage::RestartConfirm);
                case OnboardingAction::Cancel:
                    return show_options_page(scene, OptionsPage::MenuMaintenance);
                case OnboardingAction::Done:
                    return s_options_page == OptionsPage::GuideOffline ?
                        show_options_page(scene, OptionsPage::MenuMaintenance) : ESP_OK;
                case OnboardingAction::ConfirmFactoryReset:
                    return s_options_page == OptionsPage::ResetConfirm ? confirm_factory_reset(scene) : ESP_OK;
                case OnboardingAction::ConfirmRestart:
                    if (s_options_page != OptionsPage::RestartConfirm) return ESP_OK;
                    s_restart_error = LUNASHELL_power_manager_request_restart();
                    return show_options_page(scene, s_restart_error == ESP_OK
                        ? OptionsPage::RestartPending : OptionsPage::RestartConfirm);
                case OnboardingAction::BrowseCodex:
                    s_widget_open_request = WidgetOpenRequest::CodexBrowser;
                    return ESP_OK;
                case OnboardingAction::CodexStatistics:
                    s_widget_open_request = WidgetOpenRequest::Statistics;
                    return ESP_OK;
                case OnboardingAction::LocalTimer:
                    s_widget_open_request = WidgetOpenRequest::Timer;
                    return ESP_OK;
                case OnboardingAction::AudioLevels:
                    s_widget_open_request = WidgetOpenRequest::Audio;
                    return ESP_OK;
                case OnboardingAction::QuickGuide:
                    return show_options_page(scene, OptionsPage::GuideButtons);
                case OnboardingAction::VisibleScreens:
                    s_widget_open_request = WidgetOpenRequest::Visibility;
                    return ESP_OK;
                default:
                    break;
                }
                esp_err_t error = enqueue_preference(row.identity, row.requested_value);
                if (error != ESP_OK) {
                    s_preference_error = error;
                }
                error = prepare_options_content(scene);
                return error == ESP_OK ? attach_content(scene) : error;
            }
        }
    }
    return ESP_OK;
}

// direction: Logical arrow orientation.
// enabled: Whether the route currently admits navigation.
// strip: Caller-owned destination tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// opacity: Current navigation fade.
// background: Native RGB565 scene color.
// Exception: Returns the original navigation rendering error.
esp_err_t draw_navigation_chevron(NavigationDirection direction, bool enabled,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column, uint8_t opacity, uint16_t background)
{
    return UI::draw_navigation_chevron(direction, enabled, strip, first_row, first_column, opacity, background,
        {Theme::controls_secondary_top, Theme::controls_secondary_bottom, Theme::controls_secondary_icon});
}

// strip: Valid caller-owned logical tile outside the content slot as well.
// first_row: Logical tile y origin, shared with scene rendering.
// first_column: Logical tile x origin, shared with hit testing.
// opacity: Current visibility across the navigation fade.
// background: Native RGB565 scene color.
// Exception: Returns the real surface or primitive drawing error.
esp_err_t draw_onboarding_navigation(CODY2D::Surface strip, uint16_t first_row,
                                    uint16_t first_column, uint8_t opacity, uint16_t background)
{
    esp_err_t error = ESP_OK;
    if (previous_control_visible()) {
        const bool enabled = !s_command_pending &&
            (s_view.action_id != OnboardingAction::Cancel || s_view.action.enabled);
        error = draw_navigation_chevron(NavigationDirection::Previous, enabled, strip, first_row, first_column,
            opacity, background);
    }
    if (error == ESP_OK && next_control_visible()) {
        const bool enabled = !s_command_pending && (options_next_page_available() || s_view.action.enabled);
        error = draw_navigation_chevron(NavigationDirection::Next, enabled,
            strip, first_row, first_column, opacity, background);
    }
    return error;
}

// point: Acquired logical coordinates within the same full targets we render.
LateralNavigation lateral_navigation_at(CODY2D::Point point)
{
    if (UI::navigation_contains(NavigationDirection::Previous, point)) {
        return LateralNavigation::Previous;
    }
    return UI::navigation_contains(NavigationDirection::Next, point) ? LateralNavigation::Next : LateralNavigation::None;
}

// point: Logical coordinates in the current scene, with no touch side effects.
bool onboarding_lateral_control_at(CODY2D::Point point)
{
    if (is_codex_event_route(s_route)) {
        return event_presentation::hit_back(s_event_plan, s_view.actions_visible, point.x, point.y);
    }
    switch (lateral_navigation_at(point)) {
    case LateralNavigation::Previous: return previous_control_visible();
    case LateralNavigation::Next: return next_control_visible();
    case LateralNavigation::None: return false;
    }
    return false;
}

// next: Whether a forward page exists.
// strip: Borrowed logical display tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// opacity: Current visibility against the black dashboard.
// Exception: Returns the original capsule or stroke drawing error.
esp_err_t draw_lateral_navigation(bool next, CODY2D::Surface strip,
                                  uint16_t first_row, uint16_t first_column, uint8_t opacity)
{
    const esp_err_t error = draw_navigation_chevron(NavigationDirection::Previous, true, strip, first_row,
        first_column, opacity, 0);
    return error == ESP_OK && next
        ? draw_navigation_chevron(NavigationDirection::Next, true, strip, first_row, first_column, opacity, 0) : error;
}

} // namespace CODY
