#include "CODY_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#include "CODY_boot_branding.hpp"
#include "CODY_boot_menu.hpp"
#include "CODY_codex_audio.hpp"
#include "CODY_whatsapp_audio.hpp"
#include "CODY_whatsapp_service.hpp"
#include "CODY_notifications.hpp"
#include "CODY_cost_badge.hpp"
#include "CODY_device_view.hpp"
#include "CODY_ui_model.hpp"
#include "CODY_device_effects.hpp"
#include "CODY_indexed_image.hpp"
#include "CODY_codex_panels.hpp"
#include "CODY_codex_browser.hpp"
#include "CODY_audio_view.hpp"
#include "CODY_pixel_demo.hpp"
#include "CODY_pet_service.hpp"
#include "CODY_screensaver.hpp"
#include "CODY_status_overlay.hpp"
#include "LUNASHELL_audio_controls.h"
#include "CODY_onboarding.hpp"
#include "CODY_page_marks.hpp"
#include "CODY_settings.hpp"
#include "CODY_settings_service.hpp"
#include "CODY_third_party_view.hpp"
#include "CODY_prices_view.hpp"
#include "CODY_parallax.hpp"
#include "CODY_usage_alarm.hpp"
#include "CODY_model_background.hpp"
#include "CODY_setup_progress.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_technical_panels.hpp"
#include "CODY_voice_waveform.hpp"
#include "CODY_waveshare_device.hpp"
#include "LUNASHELL_codex_debug.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_task_affinity.hpp"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "scene_data.hpp"
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
#include "cody_host_studio.h"
#include "cody_host_studio_events.hpp"
#include "CODY_debug_views.hpp"
#include "../../../../simulator/native/CODY_host_studio_statistics.hpp"
#include "../../../../simulator/native/CODY_host_audio_codec.hpp"
#include "../../../../simulator/native/CODY_host_onboarding_snapshot.hpp"
#include "LUNASHELL_clock.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
// pressed: Contact state, x/y: Logical native hit coordinates.
// Exception: Returns busy, conflicting-contact or coordinate errors.
extern "C" esp_err_t cody_host_studio_touch_event(uint8_t pressed, uint16_t x, uint16_t y);
// No parameters: reserve an empty touch owner for the deterministic gesture.
extern "C" esp_err_t cody_host_studio_touch_begin();
// abort: Retire only the reserved gesture's pending contact after a failure.
extern "C" esp_err_t cody_host_studio_touch_end(bool abort);
#endif

extern "C" {
extern const uint8_t cody_surprise_portrait_start[], cody_surprise_portrait_end[];
extern const uint8_t cody_surprise_vietnam_start[], cody_surprise_vietnam_end[];
extern const uint8_t cody_surprise_italy_start[], cody_surprise_italy_end[];
extern const uint8_t cody_onboarding_sofia_start[], cody_onboarding_sofia_end[];
}

namespace {
using CODY::WidgetScreen;
using CODY::WidgetStep;
using LUNASHELL::DeviceSnapshotService;
constexpr char kTag[] = "LUNA_device_ui";
constexpr auto kGeometry = CODY::Waveshare::kDevice.geometry;
// The original round 1.75C layout uses a 64-cell Muse grid at five pixels per cell.
constexpr auto kMuseViewport = kGeometry.centered_grid(CODY::kPixelAvatarGridSize, 5);
constexpr int64_t kMuseFramePeriodUs = CODY::kPixelAvatarFrameMs * 1000LL;
static_assert(kMuseViewport.width != 0, "The Cody display must admit a full Muse grid");
constexpr uint32_t kFrameBudgetMs = 500;
constexpr uint32_t kPollMs = 100;
constexpr uint32_t kPowerTimeoutMs = 2500;
constexpr uint32_t kBirthFrameCount = 140;
constexpr uint32_t kBirthFrameMs = 50;
constexpr int64_t kStartupCueWaitUs = 3000000;
constexpr int64_t kMuseCueWaitUs = 4000000;
constexpr uint32_t kBirthWhiteHoldFrames = 17;
constexpr uint32_t kBirthMuseStartFrame = 56;
constexpr uint32_t kBirthMuseFadeFrames = 44;
constexpr uint32_t kBirthWelcomeFrame = 101;
// Ready includes one complete branding cycle after the radio owners release the display.
constexpr uint32_t kReadyTimeoutMs = kPowerTimeoutMs + VIBEWARE_BOOT_DURATION_MS +
    VIBEWARE_BOOT_FRAME_COUNT * (kFrameBudgetMs + kPollMs) +
    kBirthFrameCount * (kFrameBudgetMs + kBirthFrameMs);
// The onboarding and CCP projections still use the existing display task stack budget.
constexpr uint32_t kWorkerStackBytes = 12288;

enum class Layer : uint8_t {
    Branding, Birth, Mascot, Animation, Message, Scene, Dashboard, AboutLink, Voice, Diagnostics,
    VoucherWarning, Events, CurrentTurn, CodexBrowser, Screensaver, Blank, Audio, Surprise, Counter, UsageAlarm, BootMenu, EventCody, ThirdParty, Prices
};
enum class Command : uint8_t { Ready = 1, Sleep, Wake, Shutdown, ShutdownFailed };
enum class ActivityPage : uint8_t { Events, CurrentTurn, Statistics };
enum class BootSection : uint8_t { Default, Events, Counters };
enum class BrandingPhase : uint8_t { Inactive, AwaitingReady, Playing };
enum class Target : uint8_t {
    None, Previous, Next,
    Interval, FirstAction, SecondAction, VoiceGesture, VoucherScroll, VoucherDismiss,
    EventsGesture, ActivityBack, CodexBrowserControl, CodexBrowserBack, AudioClose,
    AudioVolumeDown, AudioVolumeUp, AudioGainDown, AudioGainUp, ThirdPartyBack
};

struct FrameView {
    CODY::DeviceFrameContent presentation{};
    int64_t voice_ribbon_at_us = 0;
    Layer layer = Layer::Blank;
    uint8_t navigation_opacity = 0;
    uint8_t connection_activity_phase = 0;
    bool arrows = false;
    bool statistics_unavailable = false;
    bool pc_alert_detail = false;
    bool guide_mascot = false;
    bool waiting_mascot = false;
    bool failure_mascot = false;
    CODY::SceneContent failure_content{};
};

struct RetiredFrame {
    Layer layer = Layer::Blank;
    WidgetScreen screen = WidgetScreen::Device;
    uint32_t display_revision = 0;
    uint32_t content_revision = 0;
    uint32_t battery_sequence = 0;
    uint32_t connection_revision = 0;
    uint16_t allowance = 0;
    int8_t battery_percent = -1;
    uint8_t navigation_opacity = 0;
    uint8_t connection_activity_phase = 0;
    bool statistics_unavailable = false;
    bool next_navigation = false;
    bool valid = false;
    CODY2D::Rect voucher_bounds{};
    int64_t voice_ribbon_at_us = 0;
    CODY::DamageRect content_bounds{};
    CODY::DamageRect bottom_action_bounds{};
    uint16_t selected_label_offset = UINT16_MAX;
    uint16_t page_mark_mask = 0;
    uint8_t settings_notice = 0;
    uint8_t setup_phase = 0;
    uint8_t setup_opacity = 0;
    uint8_t qr_opacity = 0;
    CODY::DamageRect heading_bounds{};
    uint64_t heading_key = 0;
    int64_t heading_started_us = 0;
    uint16_t heading_offset = 0;
    bool heading_scrolls = false;
    CODY::ModelBackground model_background = CODY::ModelBackground::None;
    bool failure_mascot = false;
    bool onboarding_chrome = false;
};

// heading: Current borrowed title; hash the bytes because view owners can reuse their buffers.
uint64_t heading_key(const CODY::StyledTitle &heading)
{
    if (heading.scale == 0) return 0;
    uint64_t key = 14695981039346656037ULL ^ heading.scale;
    for (uint16_t index = 0; index < heading.text.length; ++index) {
        key ^= static_cast<uint8_t>(heading.text.characters[index]);
        key *= 1099511628211ULL;
    }
    return key;
}

// Observed preference projections, refreshed from their service owners. These
// are read-only inputs to presentation, never an independently persisted UI store.
struct RuntimeSettingsProjection {
    LUNASHELL::CodyOptionsObservation audio_options{};
    esp_err_t audio_options_error = ESP_ERR_NOT_FINISHED;
    bool counter_mode_enabled = false;
    bool counter_mode_valid = false;
    esp_err_t counter_mode_error = ESP_ERR_NOT_FINISHED;
    esp_err_t idle_error = ESP_ERR_NOT_FINISHED;
    LUNASHELL::CodyIdleObservation idle_policy{};
};

// Character presentation and cue identities are independent of navigation and
// persisted settings. Pet facts remain owned by the domain pet service.
struct CharacterModel {
    CODY::DeviceEffectsModel effects{};
    CODY::ParallaxState parallax{};
    uint8_t applied_character_theme = UINT8_MAX;
    esp_err_t theme_error = ESP_ERR_NOT_FINISHED;
    CODY::PixelAvatarFrame birth_avatar{};
    bool pet_tap_cue_pending = false;
    CODY::PixelDemoView pixel_demo{kMuseViewport};
    CODY::MuseSymbol announced_symbol = CODY::MuseSymbol::None;
    Layer announced_cue_layer = Layer::Blank;
    bool announced_working = false;
    bool announced_surprise = false;
    bool symbol_cue_pending = false;
    bool working_cue_pending = false;
    int64_t symbol_cue_deadline_us = 0;
    int64_t working_cue_deadline_us = 0;
    CODY::PixelDemoView animation_preview{kMuseViewport};
#if CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    LUNASHELL::CodexAnimationScene animation_scene = LUNASHELL::CodexAnimationScene::Idle;
#endif
    int64_t animation_deadline_us = 0;
    bool animation_active = false;
    CODY::UsageAlarmState usage_alarm_state{};
    CODY::UsageAlarmNotice usage_alarm_notice{};
    uint32_t event_visual_seen = 0;
    int64_t event_cody_started_us = 0;
    LUNASHELL::CodexEventKind event_cody_kind = LUNASHELL::CodexEventKind::None;
    LUNASHELL::CodexEventSeverity event_cody_severity = LUNASHELL::CodexEventSeverity::None;
    char event_cody_request[33]{};
    char event_cody_title[25]{};
    char event_cody_origin[64]{};
    CODY2D::TextLine event_cody_origin_line{};
    CODY::SceneContent event_cody_avatar{};
    bool event_cody_active = false;
    bool usage_alarm_active = false;
    bool usage_alarm_presented = false;
    int64_t usage_alarm_started_us = 0;
    char usage_alarm_title[24]{};
    char usage_alarm_peer[33]{};
    int64_t muse_frame_due_us = 0;
};

// Display-owned command identity, not a second copy of persisted settings.
struct LifecycleRequest {
    uint32_t sequence = 0;
    uint32_t expected_revision = 0;
    int64_t retry_at_us = 0;
    esp_err_t error = ESP_OK;
    bool requested = false;
};

// Display navigation, interaction drafts and retired raster state only.
struct UiState : CODY::DeviceUiModel {
    CODY::IndexedImage artwork{};
    CODY::SceneFrame scene{};
    CODY::SceneFrame completed_scene{};
    CODY::AudioView audio_view{};
    esp_err_t audio_setting_error = ESP_OK;
    bool audio_setting_pending = false;
    bool audio_refresh_needed = false;
    BootSection boot_section = BootSection::Default;
    CODY::BootMenuView boot_menu{};
    CODY::ThirdPartyView third_party{};
    bool third_party_open = false;
    CODY::PricesView prices{};
    bool prices_open = false;
    CODY2D::Point prices_start{};
    CODY2D::Point prices_last{};
    uint32_t prices_touch_revision = 0;
    int8_t prices_touch_row = -1;
    bool boot_menu_open = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    int64_t voice_boot_error_until_us = 0;
    bool voice_boot_route_hold = false;
    bool voice_boot_phase_seen = false;
#endif
    CODY::BootMenuAction boot_menu_contact = CODY::BootMenuAction::None;
    CODY2D::Point boot_menu_start{};
    CODY2D::Point boot_menu_last{};
    uint32_t boot_menu_revision = 0;
    int64_t boot_menu_animation_tick = -1;
    uint8_t counter_page = 0;
    uint8_t counter_mask = 0;
    char counter_model_name[129]{};
    CODY::StyledTitle counter_model_title{};
    CODY::ModelBackground counter_model_background = CODY::ModelBackground::None;
    int64_t audio_refresh_retry_us = 0;
    CODY::StatusCaption settings_retry_notice{};
    CODY::StatusCaption settings_music_notice{};
    CODY::StatusCaption whatsapp_notice{};
    uint32_t whatsapp_seen = 0;
    int64_t whatsapp_notice_until_us = 0;
    bool whatsapp_resume_attempted = false;
    CODY::StatusOverlay status{};
    CODY::StatusOverlay retired_status{};
    bool status_retired = false;
    int64_t idle_eligible_since_us = -1;
    bool idle_applied = false;
    int64_t chrome_imu_at_us = 0;
    int64_t screensaver_last_touch_us = 0;
    float chrome_acceleration[3]{};
    CODY::BootBranding::Presentation branding{};
    CODY::BootBranding::Scratch branding_scratch{};
    int64_t branding_due_us = 0;
    int64_t boot_ding_deadline_us = 0;
    int64_t first_boot_melody_deadline_us = 0;
    int64_t first_boot_storage_deadline_us = 0;
    uint32_t first_boot_melody_sequence = 0;
    uint16_t branding_frame = 0;
    BrandingPhase branding_phase = BrandingPhase::Inactive;
    CODY::StyledTitle birth_welcome{};
    int64_t birth_due_us = 0;
    int64_t birth_rise_deadline_us = 0;
    uint32_t birth_frame = 0;
    uint16_t birth_background = 0;
    bool birth_requested = false;
    bool birth_active = false;
    bool first_boot_melody = false;
    bool boot_ding_pending = false;
    bool birth_rise_pending = false;

    CODY::DeviceView view{};
    CODY::CodexPanelView codex_panel{};
    CODY::CodexBrowser codex_browser{};
    CODY::CodexBrowserDestination browser_destination = CODY::CodexBrowserDestination::None;
    CODY::WidgetVisibility available_statistics{};
    CODY::NotificationKey touched_event{};
    CODY::NotificationsPage local_notifications{};
    CODY::NotificationListPage notifications{};
    uint16_t notification_offset = 0;
    uint16_t notification_pc_requested = UINT16_MAX;
    esp_err_t notification_action_error = ESP_OK;
    ActivityPage activity_page = ActivityPage::Events;
    bool questions_navigation_sound_pending = false;
    bool activity_navigation_sound_pending = false;
    bool statistics_entry = false;
    bool current_turn_available = false;
    RetiredFrame retired{};
    CODY2D::Point activity_touch_start{};
    CODY2D::Point activity_touch_last{};
    CODY2D::Point voucher_touch_start{};
    CODY2D::Point voucher_touch_last{};
    CODY2D::Point home_swipe_start{};
    CODY2D::Point home_swipe_last{};
    uint32_t home_swipe_revision = 0;
    uint16_t home_swipe_rotation = 0;
    bool home_swipe_active = false;
    bool home_swipe_valid = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    int64_t presented_voice_session = 0;
    CODY::VoiceRibbonFrame voice_ribbon{};
    uint16_t voice_ribbon_energy = 0;
#endif
    int64_t last_active_us = 0;
    int64_t awake_us = 0;
    uint32_t standby_override = 0;
    Target contact = Target::None;
    bool pressed = false;
    bool scene_dirty = true;
    bool asleep = false;
    bool diagnostics = false;
    bool audio_open = false;
    bool audio_demand_requested = false;
    bool audio_worker_started = false;
    int64_t audio_demand_started_us = 0;
    int64_t audio_worker_retry_at_us = 0;
    LifecycleRequest music_request{};
    LifecycleRequest birth_request{};
    LifecycleRequest setup_request{};
    int64_t qr_reveal_at_us = 0;
    const CODY::SceneDefinition *qr_scene_identity = nullptr;
    Layer qr_layer = Layer::Blank;
    bool qr_touch_active = false;
    CODY2D::Point qr_touch_start{};
    CODY2D::Point qr_touch_last{};
    uint32_t qr_touch_revision = 0;
    uint16_t qr_touch_rotation = 0;
    const CODY::SceneDefinition *qr_touch_scene = nullptr;
    uint8_t surprise_page = 0;
    bool mascot_home = false;
    bool about_from_options = false;
    bool about_from_boot_menu = false;
    bool about_return_settings = false;
    uint8_t about_return_settings_page = 0;
    bool about_return_third_party = false;
    bool about_return_prices = false;
    bool about_return_home = false;
    BootSection about_return_section = BootSection::Default;
    ActivityPage about_return_activity = ActivityPage::Events;
    WidgetScreen about_return_screen = WidgetScreen::Device;
    CODY::InfoPage about_return_info = CODY::InfoPage::System;
    bool options_return_valid = false;
    bool options_return_home = false;
    ActivityPage options_return_activity = ActivityPage::Events;
    WidgetScreen options_return_screen = WidgetScreen::Device;
    CODY::InfoPage options_return_info = CODY::InfoPage::System;
};

// The display controller composes distinct owners; scenes borrow projections.
struct DeviceRuntime {
    CODY::DeviceModel model{};
    UiState state{};
    CharacterModel character{};
    RuntimeSettingsProjection settings{};
};

// Data is owned by the display worker and stays off its stack and the DMA heap.
alignas(DeviceRuntime) EXT_RAM_BSS_ATTR uint8_t s_ui_storage[sizeof(DeviceRuntime)]{};
DeviceRuntime *s_ui = nullptr;
std::atomic<bool> s_ready{false};
std::atomic<bool> s_options_requested{false};
std::atomic<bool> s_home_requested{false};
std::atomic<bool> s_boot_menu_requested{false};
std::atomic<bool> s_boot_activity{false};
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
std::atomic<bool> s_boot_voice_requested{false};
std::atomic<int64_t> s_boot_voice_requested_at_us{0};
#endif
std::atomic<bool> s_surprise_requested{false};
std::atomic<uint32_t> s_ticket{0};
std::atomic<uint32_t> s_command{0};
std::atomic<uint32_t> s_ack{0};
std::atomic<esp_err_t> s_command_result{ESP_OK};
std::atomic<esp_err_t> s_terminal_error{ESP_OK};
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
struct StudioMailbox {
    std::mutex mutex;
    std::condition_variable changed;
    CodyHostStudioCase request{};
    CodyHostStudioRouteRequest route_request{};
    CodyHostStudioWorkerRequest worker_request{};
    bool replay = false;
    bool worker = false;
    bool live = false;
    uint32_t live_sounds = 0;
    uint32_t live_volume = 90;
    bool pending = false;
    bool running = false;
    bool complete = false;
    esp_err_t error = ESP_ERR_NOT_FINISHED;
};
// The host display worker is detached and has process lifetime. Intentionally
// retain its mailbox: static destruction must never destroy a condition variable
// while that worker is waiting or about to wait. The library must stay loaded.
StudioMailbox *const s_studio = new (std::nothrow) StudioMailbox{};
CodyHostStudioFrameIdentity s_studio_identity{};
// Only the display owner mutates this session. It can never be reset within a
// running process: callers must start a fresh process to seek backwards.
struct StudioWorkerSession {
    bool active = false;
    bool birth = false;
    uint32_t requested_ms = 0;
    uint32_t ticks = 0;
    char scenario_revision[65]{};
};
StudioWorkerSession s_studio_worker{};
std::atomic<bool> s_studio_worker_active{false};
// Startup cue list is written only by the display owner. A release on complete
// publishes it to the host observer; no cue list is read before completion.
std::atomic<bool> s_studio_live_active{false};
std::atomic<bool> s_studio_live_complete{false};
lunashell_audio_cue_status_t s_studio_live_cues[8]{};
size_t s_studio_live_cue_count = 0;
std::atomic<esp_err_t> s_studio_live_record_error{ESP_OK};
// frame: Actual display-owner layer/page/query selected before a transaction starts.
// Exception: No exceptions; mismatched effective destinations have no Studio identity.
bool studio_frame_matches(const FrameView &frame);
#endif
// The asynchronous selector is independent of the display worker lifetime.
std::atomic<bool> s_audio_demand_desired{false};
std::atomic<bool> s_audio_demand_confirmed{false};
std::atomic<bool> s_audio_demand_may_own{false};
std::atomic<bool> s_audio_demand_inflight{false};
std::atomic<bool> s_audio_demand_stop{false};
std::atomic<esp_err_t> s_audio_demand_error{ESP_OK};
std::atomic<int64_t> s_audio_demand_confirmed_at_us{0};

// No parameters: retire old touch ownership before changing the visible route.
void invalidate()
{
    s_ui->state.retired.valid = false;
    s_ui->state.contact = Target::None;
    CODY::hide_pixel_demo(s_ui->character.pixel_demo);
    CODY::hide_pixel_demo(s_ui->character.animation_preview);
    s_ui->state.view.account.chat_title.line.valid = false;
    s_ui->state.codex_panel.chat_title.line.valid = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    s_ui->state.voice_tapped_us = 0;
#endif
    s_ui->state.scene_dirty = true;
    s_ui->state.view.countdown.reset_animation();
    CODY::cancel_onboarding_touch();
}

// ui: Serialized display owner; changes apply only after a confirmed theme observation.
// Exception: An unavailable preference keeps the last valid visual theme.
void synchronize_character_theme(DeviceRuntime &ui)
{
    LUNASHELL::CodyCharacterThemeObservation theme{};
    const auto error = DeviceSnapshotService::instance().readCodyCharacterThemeObservation(theme);
    ui.character.theme_error = error != ESP_OK ? error : theme.last_error != ESP_OK ? theme.last_error :
        !theme.valid ? ESP_ERR_INVALID_STATE : theme.theme >= 8 ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
    if (ui.character.theme_error != ESP_OK || ui.character.applied_character_theme == theme.theme) return;
    ui.character.applied_character_theme = theme.theme;
    ui.character.pixel_demo.visual_style.character_theme = theme.theme;
    ui.character.animation_preview.visual_style.character_theme = theme.theme;
    ui.character.pixel_demo.manual_needs_compose = true;
    ui.character.animation_preview.manual_needs_compose = true;
    invalidate();
}

// ui: Display owner of an optional usage alert; preserve the underlying route.
// Exception: None; the fixed default viewport is known valid.
void close_usage_alarm(DeviceRuntime &ui)
{
    ui.character.usage_alarm_active = false;
    ui.character.usage_alarm_presented = false;
    CODY::hide_pixel_demo(ui.character.animation_preview);
    ui.character.animation_preview.manual = false;
    ui.character.animation_preview.manual_playing = false;
    ui.character.animation_preview.manual_needs_compose = false;
    (void)CODY::set_pixel_demo_viewport(ui.character.animation_preview, kMuseViewport);
    invalidate();
}

// ui: Display owner of the optional event overlay; underlying navigation is retained.
// Exception: None; the fixed default viewport is known valid.
void close_event_cody(DeviceRuntime &ui)
{
    ui.character.event_cody_active = false;
    CODY::hide_pixel_demo(ui.character.animation_preview);
    ui.character.animation_preview.manual = false;
    ui.character.animation_preview.manual_playing = false;
    ui.character.animation_preview.manual_needs_compose = false;
    (void)CODY::set_pixel_demo_viewport(ui.character.animation_preview, kMuseViewport);
    invalidate();
}

// character: Display-owned event cue, including the exact originating request.
// origin: Complete admitted origin title; only this transient preview is shortened.
// Exception: Preserves the native text-measurement or preparation error.
esp_err_t prepare_event_cue_origin(CharacterModel &character, const char *origin)
{
    auto &text = character.event_cody_origin;
    const auto &font = CODY::SceneData::body_font;
    const size_t length = std::strlen(origin);
    size_t count = std::min(length, sizeof(text) - 1U);
    std::memcpy(text, origin, count);
    text[count] = '\0';
    uint32_t width = 0;
    auto error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(count)}, width);
    if (error != ESP_OK) return error;
    if (count < length || width > 320) {
        count = std::min(count, sizeof(text) - 4U);
        do {
            std::memcpy(text + count, "...", 4);
            error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(count + 3U)}, width);
            if (error != ESP_OK) return error;
            if (width <= 320) break;
        } while (count-- > 0);
    }
    if (width > 320) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(std::strlen(text))},
        {static_cast<int16_t>(73U + (320U - width) / 2U), 337}, CODY::Theme::text_bright,
        character.event_cody_origin_line);
}

// strip: Borrowed native raster tile.
// row: First logical row represented by the tile.
// column: First logical column represented by the tile.
// Exception: Preserves the native fill error when the notice intersects this tile.
esp_err_t draw_event_cue_card(CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    constexpr CODY2D::Rect card{73, 326, 320, 48};
    const int left = std::max<int>(column, card.x);
    const int right = std::min<int>(column + strip.width, card.x + card.width);
    const int top = std::max<int>(row, card.y);
    const int bottom = std::min<int>(row + strip.height, card.y + card.height);
    if (left >= right || top >= bottom) return ESP_OK;
    return CODY2D::fill_rect(strip, {static_cast<uint16_t>(left - column), static_cast<uint16_t>(top - row),
        static_cast<uint16_t>(right - left), static_cast<uint16_t>(bottom - top)}, CODY::Theme::surface_review_bottom);
}

// state: Retained event cue and avatar projection owned by the display worker.
// strip: Borrowed native raster tile.
// row: Logical tile row.
// column: Logical tile column.
// Exception: Preserves the first original avatar or text raster error.
esp_err_t draw_event_cue_rows(const void *state, CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    const auto &character = *static_cast<const CharacterModel *>(state);
    const auto &avatar = character.event_cody_avatar;
    auto error = avatar.draw_rows(avatar.state, strip, row, column);
    if (error == ESP_OK) error = draw_event_cue_card(strip, row, column);
    if (error == ESP_OK) error = CODY2D::draw_text_rows(character.event_cody_origin_line,
        character.event_cody_origin_line.text.length, strip, row, column);
    return error;
}

// No parameters: choose the first enabled statistics page, with Device always available.
void select_statistics()
{
    s_ui->state.activity_page = ActivityPage::Statistics;
    s_ui->model.vouchers.warning_expiry = 0;
    s_ui->state.voucher_first_row = 0;
    s_ui->state.info_page = CODY::InfoPage::System;
    s_ui->state.statistics_entry = true;
    invalidate();
}

#if !CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// No parameters: physical BOOT posts an intent for the display owner.
void queue_options()
{
    s_boot_activity.store(true, std::memory_order_release);
    s_options_requested.store(true, std::memory_order_release);
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// No parameters: the power owner posts a BOOT long-hold intent to the display owner.
void queue_boot_voice()
{
    s_boot_activity.store(true, std::memory_order_release);
    s_boot_voice_requested_at_us.store(esp_timer_get_time(), std::memory_order_release);
    s_boot_voice_requested.store(true, std::memory_order_release);
}
#endif

// No parameters: post a Home intent for the display owner.
void queue_home()
{
    s_boot_activity.store(true, std::memory_order_release);
    s_home_requested.store(true, std::memory_order_release);
}

// No parameters: count physical short presses in the power owner's callback.
void queue_boot_home()
{
    static int64_t first_press_us = 0;
    static uint8_t presses = 0;
    const int64_t now_us = esp_timer_get_time();
    if (presses == 0 || now_us < first_press_us || now_us - first_press_us > 1000000) {
        first_press_us = now_us;
        presses = 1;
    } else if (++presses == 3) {
        presses = 0;
        s_boot_menu_requested.store(false, std::memory_order_release);
        s_boot_activity.store(true, std::memory_order_release);
        s_surprise_requested.store(true, std::memory_order_release);
        return;
    }
    s_boot_activity.store(true, std::memory_order_release);
    s_boot_menu_requested.store(true, std::memory_order_release);
}

// page: One-based display image selection.
// strip: Validated DMA strip owned by the display transaction.
// row: Logical top row of the strip.
// left: Logical left column of the strip.
// Exception: Returns an invalid asset or raster surface error.
esp_err_t draw_surprise_rows(uint8_t page, CODY2D::Surface strip, uint16_t row, uint16_t left)
{
    const uint8_t *start = nullptr;
    const uint8_t *end = nullptr;
    switch (page) {
    case 1: start = cody_surprise_portrait_start; end = cody_surprise_portrait_end; break;
    case 2: start = cody_surprise_vietnam_start; end = cody_surprise_vietnam_end; break;
    case 3: start = cody_surprise_italy_start; end = cody_surprise_italy_end; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    auto &image = s_ui->state.artwork;
    const auto error = CODY::prepare_indexed_image(image, start, end, 466, 466, strip.byte_order);
    return error != ESP_OK ? error : CODY::draw_indexed_image_rows(image, {0, 0, 466, 466}, strip, row, left);
}

// art: Explicit current guide artwork; never used for QR or connection pages.
// strip: Display-owner tile receiving immutable palette-indexed RGB565 pixels.
// row: Logical top row of the tile.
// left: Logical left column of the tile.
// Exception: Returns the original surface error or rejects mismatched asset geometry.
esp_err_t draw_onboarding_art_rows(CODY::OnboardingArtwork art, CODY2D::Surface strip,
                                   uint16_t row, uint16_t left)
{
    if (art != CODY::OnboardingArtwork::Sofia) return ESP_ERR_INVALID_ARG;
    auto &image = s_ui->state.artwork;
    const auto error = CODY::prepare_indexed_image(image, cody_onboarding_sofia_start,
        cody_onboarding_sofia_end, 466, 466, strip.byte_order);
    return error != ESP_OK ? error : CODY::draw_indexed_image_rows(image, {0, 0, 466, 466}, strip, row, left);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
using VoicePhase = LUNASHELL::VoiceInputPhase;

// ui: Current route and its display-owned overlays. The same route is retained through standby.
// Exception: None.
bool voice_route_available(const DeviceRuntime &ui)
{
    return CODY::Waveshare::kDevice.voice_recording && ui.state.surprise_page == 0 &&
        !ui.state.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.state.audio_open && !ui.state.diagnostics &&
        ui.state.screensaver.phase != CODY::ScreensaverPhase::Visible &&
        !(ui.state.widgets.selected_screen == WidgetScreen::About && ui.state.info_page == CODY::InfoPage::ProjectLink) &&
        !s_options_requested.load(std::memory_order_acquire);
}

// now_us: Monotonic time for the bounded terminal audio-delivery caption.
bool voice_visible(int64_t now_us)
{
    const auto &input = s_ui->model.voice.input;
    switch (input.phase) {
    case VoicePhase::Idle: return false;
    case VoicePhase::Delivered:
    case VoicePhase::Cancelled:
    case VoicePhase::Unconfirmed:
    case VoicePhase::Failed: return voice_route_available(*s_ui) &&
        now_us - input.changed_at_us < CODY::kVoiceResultDwellUs;
    default: return true;
    }
}

// ui: Display-owned artwork and the latest canonical motion and voice projections.
// frame: Receives the ribbon timestamp used by the damage renderer.
// motion_error: Result of this tick's snapshot read. Failed reads never supply microphone gain.
void prepare_device_voice_ribbon(DeviceRuntime &ui, FrameView &frame, esp_err_t motion_error)
{
    const auto &input = ui.model.voice.input;
    const bool listening = input.phase == VoicePhase::Listening;
    if (!listening && input.phase != VoicePhase::Preparing && input.phase != VoicePhase::Sending) {
        ui.state.voice_ribbon_energy = 0;
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const auto &retired = ui.state.retired;
    const int64_t previous_us = retired.valid && retired.layer == Layer::Voice ? retired.voice_ribbon_at_us : 0;
    const int64_t elapsed_us = previous_us == 0 ? CODY::kVoiceRibbonFrameUs : now_us - previous_us;
    if (elapsed_us < CODY::kVoiceRibbonFrameUs && retired.content_revision == frame.presentation.content.revision) {
        frame.voice_ribbon_at_us = previous_us;
        return;
    }

    if (listening) {
        const auto &audio = ui.model.motion.audio;
        const auto &microphone = audio.microphone;
        const bool current = motion_error == ESP_OK && microphone.health == LUNASHELL::DeviceHealth::Ready &&
            microphone.last_error == ESP_OK && microphone.data_valid && !microphone.data_stale &&
            microphone.last_success_at_us >= input.capturing_started_at_us &&
            now_us >= microphone.last_success_at_us && now_us - microphone.last_success_at_us <= 250000 &&
            audio.channel_count > 0 && audio.channel_count <= 2;
        uint32_t mean = 0;
        if (current) {
            for (uint8_t channel = 0; channel < audio.channel_count; ++channel) {
                mean = std::max(mean, audio.channel_mean_level[channel]);
            }
        }
        const int target = static_cast<int>(static_cast<uint64_t>(mean) * 255U / (uint64_t(mean) + 800U));
        const int energy = ui.state.voice_ribbon_energy;
        const int64_t response_us = target > energy ? 60000 : 220000;
        // Smoothing changes artwork gain only. Unavailable input is never published as a sample.
        const auto weight = static_cast<int>(std::clamp(elapsed_us, int64_t{0}, response_us) * 256 / response_us);
        ui.state.voice_ribbon_energy = static_cast<uint16_t>(energy + (target - energy) * weight / 256);
    } else {
        ui.state.voice_ribbon_energy = 0;
    }

    CODY::prepare_voice_ribbon(now_us, static_cast<uint8_t>(ui.state.voice_ribbon_energy), ui.state.voice_ribbon);
    frame.voice_ribbon_at_us = now_us;
}
#endif

// ui: Display-owned presentation state and canonical motion projection.
// now_us: Current monotonic time for age admission and chrome visibility.
// motion_error: Original snapshot read result.
void observe_chrome_motion(DeviceRuntime &ui, int64_t now_us, esp_err_t motion_error)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    const auto &imu = ui.model.motion.imu;
    if (motion_error != ESP_OK || imu.health != LUNASHELL::DeviceHealth::Ready ||
        !imu.data_valid || imu.data_stale || imu.last_error != ESP_OK ||
        imu.last_success_at_us <= 0 || now_us < imu.last_success_at_us ||
        now_us - imu.last_success_at_us > 500000) {
        ui.state.chrome_imu_at_us = 0;
        return;
    }
    if (imu.last_success_at_us == ui.state.chrome_imu_at_us) return;
    for (int index = 0; index < 3; ++index) {
        if (!std::isfinite(imu.acceleration_mps2[index]) || !std::isfinite(imu.angular_velocity_dps[index])) {
            ui.state.chrome_imu_at_us = 0;
            return;
        }
    }
    const bool consecutive = ui.state.chrome_imu_at_us > 0 && imu.last_success_at_us > ui.state.chrome_imu_at_us &&
        imu.last_success_at_us - ui.state.chrome_imu_at_us <= 500000;
    float acceleration_change_squared = 0.0f;
    float angular_speed_squared = 0.0f;
    for (int index = 0; index < 3; ++index) {
        const float angular_velocity = imu.angular_velocity_dps[index];
        angular_speed_squared += angular_velocity * angular_velocity;
        if (consecutive) {
            const float change = imu.acceleration_mps2[index] - ui.state.chrome_acceleration[index];
            acceleration_change_squared += change * change;
        }
        ui.state.chrome_acceleration[index] = imu.acceleration_mps2[index];
    }
    ui.state.chrome_imu_at_us = imu.last_success_at_us;
    if (acceleration_change_squared >= 0.35f * 0.35f || angular_speed_squared >= 5.0f * 5.0f) {
        ui.state.navigation.interact(now_us);
    }
#else
    (void)ui;
    (void)now_us;
    (void)motion_error;
#endif
}

// context: Unused. The hardware selector can wait five seconds, so its retries
// must never run on the display owner. A failed call can have changed hardware
// before reporting an error; ownership is retained until release succeeds.
void audio_demand_worker(void *context)
{
    (void)context;
    bool previous_desired = false;
    int64_t retry_at_us = 0;
    esp_err_t logged_error = ESP_OK;
    while (true) {
        const bool desired = s_audio_demand_desired.load(std::memory_order_acquire);
        if (desired != previous_desired) {
            retry_at_us = 0;
            previous_desired = desired;
        }
        const bool confirmed = s_audio_demand_confirmed.load(std::memory_order_acquire);
        const bool may_own = s_audio_demand_may_own.load(std::memory_order_acquire);
        const auto previous_error = s_audio_demand_error.load(std::memory_order_acquire);
        const bool needs_change = desired ? (!confirmed || previous_error != ESP_OK) : may_own;
        const int64_t now_us = esp_timer_get_time();
        if (needs_change && now_us >= retry_at_us) {
            if (desired) s_audio_demand_may_own.store(true, std::memory_order_release);
            s_audio_demand_inflight.store(true, std::memory_order_release);
            const auto error = LUNASHELL_hardware_set_diagnostic_streams(false, desired, false);
            if (error == ESP_OK) {
                if (desired) {
                    s_audio_demand_confirmed_at_us.store(esp_timer_get_time(), std::memory_order_release);
                    s_audio_demand_confirmed.store(true, std::memory_order_release);
                } else {
                    s_audio_demand_confirmed.store(false, std::memory_order_release);
                    s_audio_demand_may_own.store(false, std::memory_order_release);
                }
                logged_error = ESP_OK;
            } else {
                if (desired) s_audio_demand_confirmed.store(false, std::memory_order_release);
                if (error != logged_error) {
                    ESP_LOGW(kTag, "Audio levels %s failed (%s); retrying",
                        desired ? "request" : "release", esp_err_to_name(error));
                    logged_error = error;
                }
            }
            s_audio_demand_error.store(error, std::memory_order_release);
            s_audio_demand_inflight.store(false, std::memory_order_release);
            retry_at_us = esp_timer_get_time() + 1000000;
        }
        if (s_audio_demand_stop.load(std::memory_order_acquire) &&
            !s_audio_demand_desired.load(std::memory_order_acquire) &&
            !s_audio_demand_may_own.load(std::memory_order_acquire)) break;
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(100)));
    }
    vTaskDeleteWithCaps(nullptr);
}

// enabled: Display intent only; confirmed ownership and failures belong to the worker.
void set_audio_demand(bool enabled)
{
    auto &ui = *s_ui;
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    (void)enabled;
    ui.state.audio_demand_requested = false;
    return;
#endif
    const int64_t now_us = esp_timer_get_time();
    if (enabled != ui.state.audio_demand_requested) {
        ui.state.audio_demand_requested = enabled;
        if (enabled) ui.state.audio_demand_started_us = now_us;
        s_audio_demand_desired.store(enabled, std::memory_order_release);
    }
    if (enabled && !ui.state.audio_worker_started && now_us >= ui.state.audio_worker_retry_at_us) {
        s_audio_demand_error.store(ESP_OK, std::memory_order_release);
        if (xTaskCreatePinnedToCoreWithCaps(audio_demand_worker, "CODY_audio_demand", 4096,
            nullptr, 2, nullptr, LUNASHELL::task_affinity::kCodyRenderCoreId,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
            ui.state.audio_worker_started = true;
        } else {
            s_audio_demand_error.store(ESP_ERR_NO_MEM, std::memory_order_release);
            ui.state.audio_worker_retry_at_us = now_us + 1000000;
        }
    }
}

// ui: Retains the selected counter page and its canonical dashboard projections.
// frame: Receives only quota, active-context or model information.
// now_us: Current monotonic time for freshness and countdown.
// Exception: Returns the original projection or rendering failure.
esp_err_t prepare_counter_view(DeviceRuntime &ui, FrameView &frame, int64_t now_us)
{
    CODY::update_device_statistics(ui.model.dashboard);
    const auto &data = ui.model.dashboard;
    frame.layer = Layer::Counter;
    frame.arrows = false;
    CODY::PanelInputs inputs{};
    const auto error = CODY::project_companion_panel_inputs(data.context.link, data.context.technical,
        now_us, data.context.firmware_version, inputs);
    if (error != ESP_OK) return error;
    const bool active = inputs.activity == CODY::PanelValueState::Current && inputs.chat_id[0] != '\0';
    const bool model_current = active && inputs.model_state == CODY::PanelValueState::Current &&
        inputs.model[0] != '\0' && std::strlen(inputs.model) < sizeof(ui.state.counter_model_name);
    // Keep the three destinations stable as observations arrive or expire.
    // Each destination renders its own unavailable or stale state.
    ui.state.counter_mask = 0x07U;
    ui.state.counter_model_background = CODY::ModelBackground::None;
    if (ui.state.counter_page == 0) {
        CODY::project_device_statistics_calendar(ui.model.dashboard, WidgetScreen::Reset, ui.state.interval);
        return CODY::prepare_device_dashboard(ui.state.view, frame.presentation, WidgetScreen::Reset,
            ui.state.interval, data, now_us, CODY::VoucherList{});
    }
    if (ui.state.counter_page == 1) {
        return CODY::prepare_device_dashboard(ui.state.view, frame.presentation, WidgetScreen::Context,
            ui.state.interval, data, now_us, CODY::VoucherList{});
    }
    const char *band = active && inputs.cost_state == CODY::PanelValueState::Current
        ? CODY::cost_band_text(inputs.cost_band) : "Price unavailable";
    auto result = CODY::prepare_device_message(ui.state.view, frame.presentation, "Model",
        model_current ? "Current model" : "Model unavailable", band, nullptr, nullptr, "Standard reference");
    if (result != ESP_OK || !model_current) return result;
    std::snprintf(ui.state.counter_model_name, sizeof(ui.state.counter_model_name), "%s", inputs.model);
    const auto length = static_cast<uint16_t>(std::strlen(ui.state.counter_model_name));
    ui.state.counter_model_background = CODY::model_background_for({ui.state.counter_model_name, length}, true);
    result = CODY::prepare_styled_title({ui.state.counter_model_name, length},
        frame.presentation.content.heading.bounds, ui.state.counter_model_title, 3, true);
    if (result == ESP_OK) frame.presentation.content.heading = ui.state.counter_model_title;
    return result;
}

// kind: Authenticated remote event category projected only for shared presentation.
CODY::NotificationType notification_type(LUNASHELL::CodexEventKind kind)
{
    using Kind = LUNASHELL::CodexEventKind;
    using Type = CODY::NotificationType;
    switch (kind) {
    case Kind::Question: return Type::Question;
    case Kind::ApprovalOnPc: return Type::Warning;
    case Kind::ChecksFailed: return Type::Error;
    case Kind::ChecksPassed: return Type::Success;
    case Kind::ReplyReady: return Type::Message;
    case Kind::None:
    case Kind::Notice:
    case Kind::PrPending:
    case Kind::ChecksRunning: return Type::Information;
    }
    return Type::Information;
}

// ui: Display-owned scratch pages and independent cursor for the merged list.
// Exception: Logs optional remote paging errors without hiding retained local rows.
void prepare_notification_list(DeviceRuntime &ui)
{
    auto &local = ui.state.local_notifications;
    const auto &remote = ui.model.inbox;
    auto &page = ui.state.notifications;
    page = {};
    page.remote_retained = remote.data_valid || remote.last_success_at_us > 0;
    page.remote_current = remote.data_valid && !remote.data_stale && remote.last_error == ESP_OK;
    page.remote_error = remote.last_error;
    page.action_error = ui.state.notification_action_error;
    const uint16_t remote_total = page.remote_retained ? remote.total : 0;
    page.total = local.total + remote_total;
    page.unread = local.unread + remote_total;
    const uint16_t maximum = page.total > CODY::kNotificationPageSize ?
        static_cast<uint16_t>(page.total - CODY::kNotificationPageSize) : 0;
    ui.state.notification_offset = std::min(ui.state.notification_offset, maximum);
    page.offset = ui.state.notification_offset;
    if (page.offset < local.total && page.offset != local.offset) {
        const auto error = CODY::read_notifications(page.offset, local);
        if (error != ESP_OK) {
            ESP_LOGW(kTag, "Local notification page failed (%s)", esp_err_to_name(error));
            return;
        }
    }
    page.revision = local.revision;
    page.dropped = local.dropped;
    const bool needs_remote = page.offset + CODY::kNotificationPageSize > local.total && remote_total != 0;
    const uint16_t remote_offset = page.offset > local.total ? page.offset - local.total : 0;
    page.remote_pending = needs_remote && page.remote_current && remote.offset != remote_offset;
    // Local records remain immediately reachable without a PC. Remote ordering
    // and acknowledgements continue to belong to the authenticated broker.
    for (uint16_t index = page.offset; index < page.total &&
            index < page.offset + CODY::kNotificationPageSize; ++index) {
        if (index < local.total) {
            const auto row_index = index - local.offset;
            if (index < local.offset || row_index >= local.count) continue;
            const auto &entry = local.rows[row_index];
            auto &row = page.rows[page.count++];
            row.key.origin = CODY::NotificationOrigin::Local;
            row.key.local_id = entry.id;
            row.type = entry.type;
            row.read = entry.read;
            row.current = true;
            // Both projections are bounded, terminated owner fields. memmove
            // also admits conservative alias analysis within the runtime aggregate.
            std::memmove(row.source, entry.source, sizeof(entry.source));
            std::memmove(row.title, entry.title, sizeof(entry.title));
        } else {
            if (remote.offset != remote_offset) continue;
            const auto remote_index = index - local.total;
            if (remote_index < remote.offset || remote_index >= remote.offset + remote.count) continue;
            const auto &entry = remote.rows[remote_index - remote.offset];
            auto &row = page.rows[page.count++];
            row.key.origin = CODY::NotificationOrigin::Remote;
            std::memcpy(row.key.remote_id, entry.request_id, sizeof(row.key.remote_id));
            row.type = notification_type(entry.kind);
            row.current = page.remote_current;
            std::memmove(row.source, entry.title, sizeof(entry.title));
            std::memmove(row.title, entry.label, sizeof(entry.label));
        }
    }
    if (remote.offset == remote_offset) ui.state.notification_pc_requested = UINT16_MAX;
    if (needs_remote && page.remote_current && remote.offset != remote_offset &&
        ui.state.notification_pc_requested != remote_offset) {
        const auto error = LUNASHELL_codex_inbox_page(static_cast<uint8_t>(remote_offset));
        if (error == ESP_OK) ui.state.notification_pc_requested = remote_offset;
        else {
            ui.state.notification_action_error = error;
            page.action_error = error;
            ESP_LOGW(kTag, "Notification page request failed (%s)", esp_err_to_name(error));
        }
    }
}

// ui: Display owner of the existing Muse renderer; no additional avatar allocation.
// frame: Retains the original error text/actions and an independent decorative layer.
// now_us: Monotonic animation scheduling timestamp.
// Exception: Returns the original viewport or composition failure.
esp_err_t prepare_failure_mascot(DeviceRuntime &ui, FrameView &frame, int64_t now_us)
{
    auto &mascot = ui.character.pixel_demo;
    mascot.override_background = true;
    mascot.background_color = CODY::Theme::surface_pairing;
    mascot.suppress_particles = true;
    if (!mascot.manual || mascot.manual_pose != CODY::PixelAvatarPose::Error ||
        mascot.manual_symbol != CODY::MuseSymbol::Sweat) {
        mascot.manual = true;
        mascot.manual_playing = true;
        mascot.manual_pose = CODY::PixelAvatarPose::Error;
        mascot.manual_symbol = CODY::MuseSymbol::Sweat;
        mascot.manual_working = false;
        mascot.manual_elapsed_us = 0;
        mascot.manual_needs_compose = true;
    }
    auto error = CODY::set_pixel_demo_viewport(mascot, {181, 134, 104, 104});
    if (error == ESP_OK) error = CODY::prepare_pixel_demo(mascot, ui.state.awake_us, frame.failure_content);
    if (error == ESP_OK) {
        frame.failure_mascot = true;
        ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
    }
    return error;
}

// ui: Owner of message glyph buffers and the shared animated Cody.
// frame: Receives a factual failure without inventing recovery actions.
// now_us: Monotonic animation timestamp.
// title: Bounded heading identifying the failed operation.
// status: Factual outcome from the operation owner.
// detail: Optional instruction or original error detail.
// Exception: Preserves text layout and character preparation errors.
esp_err_t prepare_failure_message(DeviceRuntime &ui, FrameView &frame, int64_t now_us,
    const char *title, const char *status, const char *detail = "")
{
    const auto error = CODY::prepare_device_message(ui.state.view, frame.presentation,
        title, status, detail, nullptr, nullptr, "", CODY::DeviceMessageLayout::VoiceFailure);
    return error == ESP_OK ? prepare_failure_mascot(ui, frame, now_us) : error;
}

// frame: Receives one visible surface and its native content.
// now_us: Monotonic time for the reset and recording views.
// motion_error: Result of the canonical motion/audio read in this update, if any.
// Exception: Returns original native preparation failures.
esp_err_t prepare_current_view(FrameView &frame, int64_t now_us, esp_err_t motion_error = ESP_ERR_NOT_FINISHED)
{
    auto &ui = *s_ui;
    if (ui.state.surprise_page != 0) {
        frame.layer = Layer::Surprise;
        return ESP_OK;
    }
    CODY::update_device_observations(ui.model.dashboard);
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
    if (!ui.state.whatsapp_resume_attempted) {
        const auto &data = ui.model.dashboard;
        const auto &wifi = data.hardware.wifi;
        LUNASHELL::ClockSnapshot clock{};
        const bool wifi_ready = data.hardware_error == ESP_OK && wifi.connection_state_valid &&
            !wifi.data_stale && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
        if (wifi_ready && DeviceSnapshotService::instance().readClockObservation(clock) == ESP_OK &&
            clock.data_valid && !clock.data_stale && clock.utc_epoch_seconds >= 1704067200) {
            ui.state.whatsapp_resume_attempted = true;
            const auto resume_error = CODY::resume_whatsapp();
            if (resume_error != ESP_OK) ESP_LOGW(kTag, "WhatsApp resume failed (%s)", esp_err_to_name(resume_error));
        }
    }
#endif
    CODY::SettingsModel settings{};
    const auto settings_error = CODY::read_settings_model(settings);
    if (settings_error == ESP_OK && settings.onboarding.setup_progress_pending) {
        const auto &data = ui.model.dashboard;
        const auto &wifi = data.hardware.wifi;
        const auto &link = data.codex_presence.codex_link;
        const bool ready = data.hardware_error == ESP_OK && wifi.connection_state_valid && !wifi.data_stale &&
            wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0' &&
            data.codex_presence_error == ESP_OK && link.data_valid && !link.data_stale && link.last_error == ESP_OK &&
            data.codex_presence.codex_link_state == LUNASHELL::CodexLinkState::Connected &&
            data.codex_presence.codex_app_open;
        if (ready) ui.state.setup_request.requested = true;
    }
    const auto previous_voucher_series = ui.model.vouchers.series;
    CODY::update_device_vouchers(ui.model);
    if (previous_voucher_series != ui.model.vouchers.series) ui.state.voucher_first_row = 0;
    CODY::update_device_battery(ui.model, ui.state, now_us);
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
    bool voice_busy = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
    voice_busy = voice_visible(now_us);
#else
    (void)motion_error;
#endif
    if (!ui.state.audio_open || ui.state.diagnostics) {
        set_audio_demand(false);
    }
    // Optional sound failures remain in the effects model; visual controls keep working.
    (void)CODY::update_device_effects(ui.character.effects, ui.model.dashboard.codex_presence,
        ui.model.dashboard.codex_presence_error == ESP_OK, ui.model.dashboard.hardware.wifi,
        ui.model.dashboard.hardware_error == ESP_OK, ui.state.battery_banner, voice, now_us,
        CODY::onboarding_saved_wifi_startup_offline(now_us));
    const bool voucher_attention_allowed = !ui.state.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.state.diagnostics && !ui.state.audio_open && !voice_busy &&
        !CODY::device_effects_require_attention(ui.character.effects) && ui.state.battery_banner.sequence == 0 &&
        !(ui.state.widgets.selected_screen == WidgetScreen::About && ui.state.info_page == CODY::InfoPage::ProjectLink);
    if (voucher_attention_allowed && !ui.state.asleep && !ui.state.pressed &&
        ui.model.vouchers.warning_expiry == 0) {
        const auto expiry = CODY::pending_device_voucher_warning(ui.model);
        if (expiry != 0) {
            // A visible-only notice never wakes the panel, plays audio or changes the retained dashboard route.
            ui.model.vouchers.warning_expiry = expiry;
        }
    }
    const bool saver_eligible = CODY::onboarding_dashboard_active() && !CODY::onboarding_startup_active() &&
        (ui.settings.counter_mode_enabled
        ? (ui.state.mascot_home || CODY::onboarding_dashboard_active())
        : (ui.state.mascot_home ? ui.character.pixel_demo.elapsed_us >= CODY::kPixelDemoWakingUs :
           CODY::onboarding_dashboard_active())) &&
        !ui.state.diagnostics && !ui.character.animation_active && !ui.character.usage_alarm_active && !ui.character.event_cody_active && !ui.state.boot_menu_open &&
        !ui.state.audio_open && !voice_busy &&
        ui.model.vouchers.warning_expiry == 0 &&
        !CODY::device_effects_require_attention(ui.character.effects) && ui.state.battery_banner.sequence == 0 &&
        !(!ui.state.mascot_home && ui.state.widgets.selected_screen == WidgetScreen::About &&
          ui.state.info_page == CODY::InfoPage::ProjectLink);
    CODY::update_screensaver(ui.state.screensaver, saver_eligible,
        ui.state.screensaver_last_touch_us, now_us);
    frame.navigation_opacity = ui.state.navigation.opacity(now_us);
    if (ui.state.screensaver.phase == CODY::ScreensaverPhase::Visible) {
        frame.layer = Layer::Screensaver;
        return CODY::prepare_screensaver(ui.state.screensaver, ui.state.view.screensaver, frame.presentation.content);
    }
    if (ui.character.animation_active && (ui.settings.counter_mode_enabled || voice_busy || CODY::device_effects_require_attention(ui.character.effects) ||
        ui.state.battery_banner.sequence != 0 || ui.state.diagnostics)) {
        ui.character.animation_active = false;
        CODY::reset_muse_animation(0);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (voice_visible(now_us)) {
        frame.layer = Layer::Voice;
        const auto error = CODY::prepare_device_voice(ui.state.view, frame.presentation, ui.model.voice.input);
        if (error == ESP_OK && ui.model.voice.input.phase == LUNASHELL::VoiceInputPhase::Failed)
            return prepare_failure_mascot(ui, frame, now_us);
        if (error == ESP_OK) prepare_device_voice_ribbon(ui, frame, motion_error);
        return error;
    }
    if (now_us < ui.state.voice_boot_error_until_us) {
        frame.layer = Layer::Message;
        const auto error = CODY::prepare_device_message(ui.state.view, frame.presentation,
            "Voice unavailable", "Check settings", "and connection", nullptr, nullptr, "",
            CODY::DeviceMessageLayout::VoiceFailure);
        return error == ESP_OK ? prepare_failure_mascot(ui, frame, now_us) : error;
    }
#endif
    const bool critical_event = CODY::device_effects_require_attention(ui.character.effects) &&
        ui.character.effects.event.kind != LUNASHELL::CodexEventKind::ReplyReady;
    if (ui.character.event_cody_active && (critical_event ||
        ui.state.battery_banner.sequence != 0 || ui.state.diagnostics || ui.state.audio_open)) close_event_cody(ui);
    if (ui.state.boot_menu_open) {
        frame.layer = Layer::BootMenu;
        frame.arrows = true;
        const bool in_settings = ui.state.scene.valid && ui.state.scene.definition->id == CODY::SceneId::MakeItYours;
        const bool can_change_route = CODY::onboarding_dashboard_active() ||
            (in_settings && CODY::can_open_wifi_setup_from_home());
        const bool can_open_settings = in_settings ? CODY::can_open_wifi_setup_from_home() : CODY::can_open_setup_menu();
        return CODY::prepare_boot_menu(ui.state.boot_menu, CODY::SceneData::body_font, true,
            can_change_route, can_open_settings, frame.presentation.content);
    }
    if (ui.character.animation_active) {
        frame.layer = Layer::Animation;
        ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        return CODY::prepare_pixel_demo(ui.character.animation_preview, ui.state.awake_us, frame.presentation.content);
    }
    if (ui.character.usage_alarm_active) {
        frame.layer = Layer::UsageAlarm;
        const auto elapsed = std::max<int64_t>(0, now_us - ui.character.usage_alarm_started_us);
        const int64_t half_period = 350000 - ui.character.usage_alarm_notice.intensity * 40000;
        const bool raised = (elapsed / half_period) % 2 != 0;
        const auto amplitude = static_cast<uint16_t>(4U + 4U * ui.character.usage_alarm_notice.intensity);
        const CODY::LogicalRect viewport{113, static_cast<uint16_t>(154U - (raised ? amplitude : 0U)), 240, 240};
        auto error = CODY::set_pixel_demo_viewport(ui.character.animation_preview, viewport);
        if (error != ESP_OK) return error;
        ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        error = CODY::prepare_pixel_demo(ui.character.animation_preview, ui.state.awake_us, frame.presentation.content);
        if (error != ESP_OK) return error;
        return CODY::prepare_styled_title({ui.character.usage_alarm_title,
            static_cast<uint16_t>(std::strlen(ui.character.usage_alarm_title))}, {73, 85, 320, 44},
            frame.presentation.content.heading, 3, true);
    }
    if (ui.character.event_cody_active) {
        frame.layer = Layer::EventCody;
        const auto severity = ui.character.event_cody_severity;
        const int64_t period = severity == LUNASHELL::CodexEventSeverity::Critical ? 120000 :
            severity == LUNASHELL::CodexEventSeverity::Warning ? 240000 : 360000;
        const bool raised = ((now_us - ui.character.event_cody_started_us) / period) % 2 != 0;
        auto error = CODY::set_pixel_demo_viewport(ui.character.animation_preview,
            {137, static_cast<uint16_t>(raised ? 160U : 174U), 192, 192});
        if (error != ESP_OK) return error;
        ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        error = CODY::prepare_pixel_demo(ui.character.animation_preview, ui.state.awake_us, frame.presentation.content);
        if (error != ESP_OK) return error;
        ui.character.event_cody_avatar = frame.presentation.content;
        frame.presentation.content.state = &ui.character;
        frame.presentation.content.draw_rows = draw_event_cue_rows;
        frame.presentation.content.bounds = {73, 132, 393, 380};
        const char *title = ui.character.event_cody_title[0] != '\0' ? ui.character.event_cody_title : "NOTICE";
        return CODY::prepare_styled_title({title, static_cast<uint16_t>(std::strlen(title))},
            {73, 85, 320, 44}, frame.presentation.content.heading, 3, true);
    }
    if (ui.state.third_party_open) {
        frame.layer = Layer::ThirdParty;
        frame.arrows = true;
        return CODY::prepare_third_party_view(ui.state.third_party, CODY::SceneData::body_font, frame.presentation.content);
    }
    if (ui.state.prices_open) {
        frame.layer = Layer::Prices;
        frame.arrows = true;
        return CODY::prepare_prices_view(ui.state.prices, CODY::SceneData::body_font, now_us, frame.presentation.content);
    }
    if (ui.state.diagnostics) {
        frame.layer = Layer::Diagnostics;
        const auto error = DeviceSnapshotService::instance().readDiagnosticsObservation(ui.model.diagnostics);
        return CODY::prepare_device_diagnostics(ui.state.view, frame.presentation, ui.model.diagnostics, error == ESP_OK);
    }
    if (ui.state.audio_open) {
        frame.layer = Layer::Audio;
        frame.arrows = true;
        set_audio_demand(true);
#if CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
        const auto demand_error = ESP_ERR_NOT_SUPPORTED;
#else
        auto demand_error = s_audio_demand_error.load(std::memory_order_acquire);
        if (demand_error == ESP_OK && (!s_audio_demand_confirmed.load(std::memory_order_acquire) ||
            s_audio_demand_inflight.load(std::memory_order_acquire))) {
            demand_error = ESP_ERR_NOT_FINISHED;
        }
#endif
        motion_error = DeviceSnapshotService::instance().readMotionPowerObservation(ui.model.motion);
        const auto playback_error = motion_error;
        const auto &sample = ui.model.motion.audio.microphone;
        const int64_t admitted_at = std::max(ui.state.audio_demand_started_us,
            s_audio_demand_confirmed_at_us.load(std::memory_order_acquire));
        if (demand_error == ESP_OK && motion_error == ESP_OK && sample.data_valid &&
            sample.last_error == ESP_OK && sample.last_success_at_us <= admitted_at) {
            motion_error = ESP_ERR_NOT_FINISHED;
        }
        ui.settings.audio_options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(ui.settings.audio_options);
        return CODY::prepare_audio_view(ui.model.motion.audio,
            demand_error == ESP_OK ? motion_error : demand_error, playback_error, ui.settings.audio_options.preferences,
            ui.settings.audio_options_error, ui.state.audio_setting_pending, ui.state.audio_setting_error, esp_timer_get_time(),
            CODY::SceneData::title_font, CODY::SceneData::body_font, ui.state.audio_view, frame.presentation.content);
    }
    if (CODY::onboarding_startup_active() && ui.state.scene.valid) {
        // The saved Home preference never hides an active connection decision.
        // Voice, diagnostics and explicit menus retain their existing priority.
        frame.layer = Layer::Scene;
        frame.connection_activity_phase = CODY::onboarding_connection_activity_phase(now_us);
        return ESP_OK;
    }
    if (!ui.settings.counter_mode_enabled && ui.state.codex_browser.route != CODY::CodexBrowserRoute::Closed &&
        ui.state.browser_destination == CODY::CodexBrowserDestination::None &&
        CODY::onboarding_codex_tools_active()) {
        frame.layer = Layer::CodexBrowser;
        frame.arrows = true;
        return CODY::prepare_codex_browser(ui.state.codex_browser, now_us, frame.presentation.content);
    }
    if (
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        !s_boot_voice_requested.load(std::memory_order_acquire) && !ui.state.voice_boot_route_hold &&
#endif
        (ui.state.boot_section == BootSection::Counters ||
        (ui.state.boot_section == BootSection::Default && ui.settings.counter_mode_enabled)) && !ui.state.about_from_options &&
        CODY::onboarding_dashboard_active() && !CODY::onboarding_startup_active()) {
        return prepare_counter_view(ui, frame, now_us);
    }
    const bool mascot_waking = ui.character.pixel_demo.elapsed_us < CODY::kPixelDemoWakingUs;
    if (ui.state.mascot_home && CODY::onboarding_dashboard_active() && !CODY::onboarding_startup_active() &&
        (mascot_waking || !CODY::device_effects_require_attention(ui.character.effects))) {
        frame.layer = Layer::Mascot;
        ui.character.pixel_demo.manual = false;
        ui.character.pixel_demo.override_background = false;
        ui.character.pixel_demo.suppress_particles = false;
        ui.character.pixel_demo.fixed_touch_bounds = true;
        const CODY::LogicalRect viewport{
            static_cast<uint16_t>(static_cast<int>(kMuseViewport.x) + ui.character.parallax.x),
            static_cast<uint16_t>(static_cast<int>(kMuseViewport.y) + ui.character.parallax.y),
            kMuseViewport.width, kMuseViewport.height};
        const auto viewport_error = CODY::set_pixel_demo_viewport(ui.character.pixel_demo, viewport);
        if (viewport_error != ESP_OK) return viewport_error;
        ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        CODY::PixelDemoMotion motion{};
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
        const auto &imu = ui.model.motion.imu;
        if (motion_error == ESP_OK && imu.health == LUNASHELL::DeviceHealth::Ready &&
            imu.data_valid && !imu.data_stale && imu.last_error == ESP_OK) {
            motion.acceleration_x = imu.acceleration_mps2[0];
            motion.acceleration_y = imu.acceleration_mps2[1];
            motion.angular_velocity_x = imu.angular_velocity_dps[0];
            motion.angular_velocity_y = imu.angular_velocity_dps[1];
            motion.angular_velocity_z = imu.angular_velocity_dps[2];
            motion.angular_valid = std::isfinite(motion.angular_velocity_x) &&
                std::isfinite(motion.angular_velocity_y) && std::isfinite(motion.angular_velocity_z);
            motion.sampled_at_us = imu.last_success_at_us;
            motion.rendering_rotation_degrees = ui.model.motion.rendering_rotation_degrees;
            motion.valid = true;
        }
#endif
        CODY::observe_pixel_demo_motion(ui.character.pixel_demo, motion, esp_timer_get_time());
        return CODY::prepare_pixel_demo(ui.character.pixel_demo, ui.state.awake_us, frame.presentation.content);
    }
    if (CODY::onboarding_dashboard_active()) {
        auto &model = ui.model;
        CODY::update_device_statistics(model.dashboard);
        const auto &turn = model.dashboard.context;
        ui.state.current_turn_available = CODY::current_turn_has_data(turn, now_us);
        const bool browser_child = ui.state.browser_destination != CODY::CodexBrowserDestination::None;
        if (ui.state.activity_page == ActivityPage::Events) {
            frame.layer = Layer::Events;
            frame.arrows = browser_child;
            prepare_notification_list(ui);
            return CODY::prepare_codex_events(ui.state.codex_panel, ui.state.notifications, now_us, frame.presentation.content);
        }
        if (ui.state.activity_page == ActivityPage::CurrentTurn) {
            frame.layer = Layer::CurrentTurn;
            frame.arrows = true;
            return CODY::prepare_current_turn(ui.state.codex_panel, turn, now_us, frame.presentation.content);
        }
        ui.state.available_statistics = CODY::available_device_statistics(model.dashboard, ui.state.widgets.visibility, now_us);
        if (ui.state.about_from_options) {
            // Opening About from Options is independent of dashboard visibility preferences.
            ui.state.available_statistics.screen_mask |= CODY::WidgetStateDetail::bit(WidgetScreen::About);
        }
        if (ui.state.statistics_entry || !CODY::widget_visible(ui.state.available_statistics, ui.state.widgets.selected_screen)) {
            auto usage = ui.state.available_statistics;
            usage.screen_mask &= ~CODY::WidgetStateDetail::bit(WidgetScreen::About);
            const auto first = CODY::first_visible_widget(usage, CODY::WidgetScope::Statistics);
            CODY::select_widget_screen(ui.state.widgets, first.value_or(WidgetScreen::Device));
            ui.state.info_page = CODY::InfoPage::System;
            ui.state.statistics_entry = false;
            invalidate();
        }
        if (ui.model.vouchers.warning_expiry != 0 && voucher_attention_allowed) {
            frame.layer = Layer::VoucherWarning;
            frame.arrows = true;
            return CODY::prepare_device_voucher_warning(ui.state.view, frame.presentation, ui.model);
        }
        if (ui.state.widgets.selected_screen == WidgetScreen::About &&
            ui.state.info_page == CODY::InfoPage::ProjectLink) {
            frame.layer = Layer::AboutLink;
            frame.arrows = false;
            const auto error = CODY::prepare_about_project_link(frame.presentation.content);
            if (error == ESP_OK) return ESP_OK;
            return prepare_failure_message(ui, frame, now_us, "QR unavailable", "Could not show code");
        }
        frame.layer = Layer::Dashboard;
        const auto screen = ui.state.widgets.selected_screen;
        frame.arrows = browser_child || screen == WidgetScreen::About ||
            CODY::next_visible_widget(ui.state.available_statistics, screen, WidgetStep::Next) != screen;
        if (screen == WidgetScreen::About) {
            // Per-field source errors are rendered locally; uptime and navigation remain usable.
            (void)CODY::read_device_info(model, ui.state.info_page);
            return CODY::prepare_device_info(ui.state.view, frame.presentation, model, ui.state.info_page);
        }
        if (screen == WidgetScreen::TokenHistory && !CODY::device_history_has_data(model.dashboard, ui.state.interval)) {
            ui.state.interval = CODY::TokenHistoryInterval::TwentyFourHours;
        }
        CODY::project_device_statistics_calendar(model.dashboard, screen, ui.state.interval);
        const auto vouchers = screen == WidgetScreen::Credits ? CODY::project_device_voucher_list(model, ui.state.voucher_first_row) : CODY::VoucherList{};
        const auto error = CODY::prepare_device_dashboard(ui.state.view, frame.presentation,
            screen, ui.state.interval, model.dashboard, now_us, vouchers);
        if (error == ESP_OK) return ESP_OK;
        // An optional data provider must leave navigation and power controls usable.
        if (!ui.state.retired.statistics_unavailable) {
            ESP_LOGW(kTag, "Dashboard unavailable (%s)", esp_err_to_name(error));
        }
        frame.statistics_unavailable = true;
        frame.presentation = {};
        return prepare_failure_message(ui, frame, now_us, "Statistics", "Data unavailable", "BOOT opens Menu");
    }
    if (ui.state.scene.valid) {
        frame.layer = Layer::Scene;
        frame.connection_activity_phase = CODY::onboarding_connection_activity_phase(now_us);
        const auto guide_visual = CODY::onboarding_guide_visual();
        const bool wifi_wait = CODY::onboarding_wifi_wait_mascot();
        const auto connection_visual = CODY::onboarding_connection_mascot();
        if (guide_visual != 0 || wifi_wait || connection_visual != 0) {
            frame.guide_mascot = guide_visual != 0;
            frame.waiting_mascot = wifi_wait || connection_visual != 0;
            if (connection_visual == 1) frame.connection_activity_phase = 0;
            auto &mascot = ui.character.pixel_demo;
            mascot.override_background = true;
            mascot.suppress_particles = !(wifi_wait || connection_visual == 1);
            mascot.background_color = frame.guide_mascot ? CODY::Theme::surface_pairing : ui.state.scene.definition->background_rgb565;
            const auto pose = connection_visual == 2 ? CODY::PixelAvatarPose::Error :
                (wifi_wait || connection_visual == 1) ? CODY::PixelAvatarPose::Thinking : guide_visual == 2 ? CODY::PixelAvatarPose::Listening :
                guide_visual == 3 ? CODY::PixelAvatarPose::Reply : CODY::PixelAvatarPose::Idle;
            if (!mascot.manual || mascot.manual_pose != pose) {
                mascot.manual = true;
                mascot.manual_playing = true;
                mascot.manual_pose = pose;
                mascot.manual_symbol = connection_visual == 2 ? CODY::MuseSymbol::Sweat : CODY::MuseSymbol::None;
                mascot.manual_working = false;
                mascot.manual_elapsed_us = 0;
                mascot.manual_needs_compose = true;
            }
            const auto viewport = wifi_wait ? CODY::LogicalRect{169, 135, 128, 128} :
                (connection_visual == 1 || connection_visual == 3) ? CODY::LogicalRect{193, 134, 80, 80} :
                connection_visual == 2 ? CODY::LogicalRect{181, 130, 104, 104} :
                CODY::LogicalRect{153, 113, 160, 160};
            const auto error = CODY::set_pixel_demo_viewport(mascot, viewport);
            if (error != ESP_OK) return error;
            ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
            return CODY::prepare_pixel_demo(mascot, ui.state.awake_us, frame.presentation.content);
        }
        ui.character.pixel_demo.manual = false;
        ui.character.pixel_demo.override_background = false;
        ui.character.pixel_demo.suppress_particles = false;
        if (CODY::onboarding_pc_alert_detail_active()) {
            frame.pc_alert_detail = true;
            // The ordinary animated mascot remains underneath the detail's readable text panel.
            const auto error = CODY::set_pixel_demo_viewport(ui.character.pixel_demo, {113, 112, 240, 240});
            if (error != ESP_OK) return error;
            ui.character.muse_frame_due_us = now_us + kMuseFramePeriodUs;
            return CODY::prepare_pixel_demo(ui.character.pixel_demo, ui.state.awake_us, frame.presentation.content);
        }
        return ESP_OK;
    }
    frame.layer = Layer::Message;
    return CODY::prepare_device_message(ui.state.view, frame.presentation, "Device", "Starting services");
}

// metrics: Canonical result published synchronously by this frame's retirement.
// draw_error: First local raster or native transfer result, before frame retirement.
// retired_error: Complete frame result, including the BSP drain and deadline check.
// native_timeout: True only when the first timeout came from native buffer/submission APIs.
// admitted_at_us: Clock sampled after this frame acquired its exclusive display ownership.
// retired_at_us: Clock sampled immediately after this frame released its ownership.
// Exception: None; stale evidence, raster timeouts and uncertain ownership never qualify.
bool can_defer_frame_timeout(const LUNASHELL::DisplayFrameSnapshot &metrics,
    esp_err_t draw_error, esp_err_t retired_error, bool native_timeout,
    int64_t admitted_at_us, int64_t retired_at_us)
{
    return retired_error == ESP_ERR_TIMEOUT &&
        (draw_error == ESP_OK || (draw_error == ESP_ERR_TIMEOUT && native_timeout)) &&
        admitted_at_us > 0 && retired_at_us >= admitted_at_us && metrics.measured &&
        metrics.completed_at_us > admitted_at_us && metrics.completed_at_us <= retired_at_us &&
        metrics.deadline_expired && metrics.last_duration_us >= kFrameBudgetMs * 1000U &&
        metrics.last_error == ESP_ERR_TIMEOUT && metrics.last_completion_error == ESP_OK &&
        !metrics.reset_required && metrics.retained_dma_bytes == 0 &&
        metrics.retained_consumer_bytes == 0 && metrics.retained_consumer_error == ESP_OK;
}

enum class RenderAdmission : uint8_t { Scheduled, LifecycleHandoff };

// admission: Only explicit startup/power handoffs bypass an outstanding retry delay.
// now_us: Current monotonic time sampled by the serialized display owner.
// retry_at_us: Earliest scheduled retry after a certified drained deadline.
// Exception: None; forcing a full redraw does not change admission policy.
bool frame_retry_pending(RenderAdmission admission, int64_t now_us, int64_t retry_at_us)
{
    return admission != RenderAdmission::LifecycleHandoff && now_us < retry_at_us;
}

struct OnboardingPerimeterChord {
    uint16_t outer = 0;
    uint16_t inner = 0;
};

// square: Nonnegative doubled-coordinate squared distance, bounded by 4 * 231 * 231.
// Exception: None; returns the exact integer floor without runtime floating point.
constexpr uint16_t onboarding_integer_root(unsigned square)
{
    unsigned low = 0, high = 463;
    while (low + 1U < high) {
        const unsigned middle = (low + high) / 2U;
        if (middle * middle <= square) low = middle;
        else high = middle;
    }
    return static_cast<uint16_t>(low);
}

// No parameters: builds the bounded panel-edge chords at compile time in read-only storage.
// Exception: None; rows outside the outer circle have an empty chord.
constexpr std::array<OnboardingPerimeterChord, 466> onboarding_perimeter_chords()
{
    std::array<OnboardingPerimeterChord, 466> chords{};
    for (int y = 0; y < 466; ++y) {
        const int dy = 2 * y + 1 - 466;
        const int outer_squared = 4 * 231 * 231 - dy * dy;
        const int inner_squared = 4 * 226 * 226 - dy * dy;
        if (outer_squared > 0) chords[y].outer = onboarding_integer_root(static_cast<unsigned>(outer_squared));
        if (inner_squared > 0) chords[y].inner = onboarding_integer_root(static_cast<unsigned>(inner_squared));
    }
    return chords;
}

inline constexpr auto kOnboardingPerimeterChords = onboarding_perimeter_chords();

// strip: Borrowed RGB565 tile, in the display's explicit storage byte order.
// row: First logical row of the tile.
// column: First logical column of the tile.
// opacity: Whole-ring visibility, including the temporary reveal on QR pages.
// Exception: Returns the original surface error or rejects an out-of-panel tile.
esp_err_t draw_onboarding_perimeter(CODY2D::Surface strip, uint16_t row, uint16_t column, uint8_t opacity)
{
    if (static_cast<uint32_t>(row) + strip.height > 466U ||
        static_cast<uint32_t>(column) + strip.width > 466U) return ESP_ERR_INVALID_ARG;
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK || opacity == 0) return error;
    // The fixed ring is decorative, not a fabricated completion percentage.
    // It is bounded to the panel edge and never covers QR or content hit targets.
    constexpr uint16_t colors[]{CODY::Theme::branding_rainbow_0, CODY::Theme::branding_rainbow_1,
        CODY::Theme::accent_violet, CODY::Theme::accent_violet, CODY::Theme::branding_rainbow_5,
        CODY::Theme::accent_active, CODY::Theme::accent_active, CODY::Theme::branding_rainbow_0,
        CODY::Theme::branding_rainbow_0};
    constexpr int outer = 231;
    constexpr int inner = 226;
    // The maximum distance within a rectangle is at one of its corners.
    // Mascot and caption damage normally lies wholly inside the clear center.
    const int x0 = 2 * column + 1 - 466;
    const int x1 = 2 * (column + strip.width - 1) + 1 - 466;
    const int y0 = 2 * row + 1 - 466;
    const int y1 = 2 * (row + strip.height - 1) + 1 - 466;
    const int far_x = std::max(std::abs(x0), std::abs(x1));
    const int far_y = std::max(std::abs(y0), std::abs(y1));
    if (far_x * far_x + far_y * far_y < 4 * inner * inner) return ESP_OK;
    for (int y = row; y < row + strip.height; ++y) {
        const int dy = 2 * y + 1 - 466;
        const int outer_x = kOnboardingPerimeterChords[y].outer;
        const int inner_x = kOnboardingPerimeterChords[y].inner;
        if (outer_x == 0) continue;
        const int left = std::max<int>(column, (465 - outer_x) / 2);
        const int right = std::min<int>(column + strip.width, (467 + outer_x) / 2);
        for (int x = left; x < right; ++x) {
            const int dx = 2 * x + 1 - 466;
            const int radius_squared = dx * dx + dy * dy;
            if (radius_squared < 4 * inner * inner) {
                // The inside of the ring is transparent; skip its entire chord.
                if (dx < 0 && inner_x > 0) x = std::max(x, (465 + inner_x) / 2 - 1);
                continue;
            }
            if (radius_squared >= 4 * outer * outer) continue;
            const int ax = std::abs(dx), ay = std::abs(dy);
            const int fraction = ax * 256 / (ax + ay);
            const int phase = dy < 0 ? (dx >= 0 ? fraction : 1024 - fraction) :
                (dx >= 0 ? 512 - fraction : 512 + fraction);
            const unsigned segment = static_cast<unsigned>(phase / 128) % 8U;
            const uint8_t blend = static_cast<uint8_t>((phase % 128) * 2);
            const uint16_t ink = CODY2D::blend_rgb565(colors[segment + 1U], colors[segment], blend);
            auto &pixel = strip.pixels[(y - row) * strip.stride + x - column];
            const auto background = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            pixel = CODY2D::rgb565_native_to_stored(CODY2D::blend_rgb565(ink, background, opacity), strip.byte_order);
        }
    }
    return ESP_OK;
}

// strip: Borrowed RGB565 tile with its declared byte order.
// row: First logical row of the tile.
// column: First logical column of the tile.
// top: Admitted accent row below the heading and above the character.
// Exception: Returns the original fill or surface error.
esp_err_t draw_onboarding_title_accent(CODY2D::Surface strip, uint16_t row, uint16_t column, uint16_t top)
{
    constexpr uint16_t left = 173, right = 293, height = 2;
    const auto x0 = std::max<uint16_t>(left, column);
    const auto x1 = std::min<uint16_t>(right, column + strip.width);
    const auto y0 = std::max<uint16_t>(top, row);
    const auto y1 = std::min<uint16_t>(top + height, row + strip.height);
    if (x0 >= x1 || y0 >= y1) return ESP_OK;
    return CODY2D::fill_rect(strip, {static_cast<uint16_t>(x0 - column), static_cast<uint16_t>(y0 - row),
        static_cast<uint16_t>(x1 - x0), static_cast<uint16_t>(y1 - y0)}, CODY::Theme::secondary_bright);
}

// frame: Prepared view borrowed until synchronous DMA retirement.
// force: Render the complete panel instead of only the damaged region.
// admission: Explicit lifecycle handoffs may bypass backoff but still require retirement.
// Exception: Only a certified, fully drained deadline returns NOT_FINISHED for retry;
// all other lease, raster, transfer and retirement errors retain their original value.
esp_err_t render(const FrameView &frame, bool force = false,
    RenderAdmission admission = RenderAdmission::Scheduled)
{
    // Only this serialized display owner accesses the retry clock. The failed
    // frame remains invalid until a later complete frame actually retires.
    static int64_t frame_retry_at_us = 0;
    if (frame_retry_pending(admission, esp_timer_get_time(), frame_retry_at_us)) return ESP_ERR_NOT_FINISHED;
    auto &ui = *s_ui;
    if (frame.layer != Layer::Mascot && !frame.pc_alert_detail && !frame.guide_mascot && !frame.waiting_mascot && !frame.failure_mascot) CODY::hide_pixel_demo(ui.character.pixel_demo);
    if (frame.layer != Layer::Animation && frame.layer != Layer::UsageAlarm && frame.layer != Layer::EventCody)
        CODY::hide_pixel_demo(ui.character.animation_preview);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    // Standby retains the route so an eligible dashboard can still wake by voice.
    const bool keep_voice_route = frame.layer == Layer::Blank && voice_route_available(ui);
    const bool admit_voice = voice_route_available(ui) &&
        (frame.layer == Layer::Dashboard || frame.layer == Layer::Events || frame.layer == Layer::CurrentTurn);
    if (!keep_voice_route && !admit_voice) LUNASHELL::setVoiceInputRouteAvailable(false);
#endif
    auto &old = ui.state.retired;
    const auto &view = frame.presentation;
    const auto revision = LUNASHELL_display_runtime_content_revision();
    const auto screen = ui.state.widgets.selected_screen;
    const bool qr_view = frame.layer == Layer::AboutLink ||
        (frame.layer == Layer::Scene && CODY::onboarding_qr_visible());
    const auto *qr_scene = frame.layer == Layer::Scene && qr_view ? ui.state.scene.definition : nullptr;
    if (!qr_view || ui.state.qr_layer != frame.layer || ui.state.qr_scene_identity != qr_scene) {
        ui.state.qr_reveal_at_us = 0;
        ui.state.qr_layer = qr_view ? frame.layer : Layer::Blank;
        ui.state.qr_scene_identity = qr_scene;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t reveal_age = ui.state.qr_reveal_at_us == 0 ? -1 : now_us - ui.state.qr_reveal_at_us;
    const uint8_t qr_opacity = reveal_age < 0 || reveal_age >= 3500000 ? 0 :
        reveal_age <= 3000000 ? 255 : static_cast<uint8_t>((3500000 - reveal_age) * 255 / 500000);
    CODY::SettingsModel settings{};
    const auto settings_error = CODY::read_settings_model(settings);
    const bool setup_pending = settings_error == ESP_OK &&
        (settings.onboarding.setup_progress_pending || !settings.storage.setup_progress.valid);
    const bool onboarding_chrome = frame.failure_mascot || (frame.layer == Layer::Scene &&
        (CODY::onboarding_first_run_active() || frame.guide_mascot || frame.waiting_mascot));
    const bool retrying_settings = ui.state.birth_request.error != ESP_OK || ui.state.setup_request.error != ESP_OK;
    const bool failed_music_settings = ui.state.music_request.error != ESP_OK;
    const bool notice_allowed = !qr_view && frame.layer != Layer::Blank && frame.layer != Layer::Branding &&
        frame.layer != Layer::Birth && frame.layer != Layer::Surprise && frame.layer != Layer::Screensaver;
    const uint8_t settings_notice = !notice_allowed ? 0 : retrying_settings ? 1 : failed_music_settings ? 2 :
        now_us < ui.state.whatsapp_notice_until_us ? 3 : 0;
    const bool setup_visible = !onboarding_chrome && setup_pending && frame.layer != Layer::Blank &&
        frame.layer != Layer::Branding && frame.layer != Layer::Birth && frame.layer != Layer::Screensaver &&
        frame.layer != Layer::Surprise;
    const uint8_t setup_opacity = setup_visible ? (qr_view ? qr_opacity : 255) : 0;
    const uint8_t setup_phase = static_cast<uint8_t>((now_us / CODY::kSetupProgressFrameUs) % CODY::kSetupProgressSteps);
    const bool show_connection = (frame.layer == Layer::Dashboard && screen != WidgetScreen::Device &&
        !(screen == WidgetScreen::About && ui.state.info_page == CODY::InfoPage::VibeWare)) ||
        frame.layer == Layer::VoucherWarning;
    const bool show_battery = frame.layer != Layer::Blank && frame.layer != Layer::Branding && frame.layer != Layer::Birth &&
        frame.layer != Layer::Screensaver && frame.layer != Layer::Mascot && frame.layer != Layer::Animation && frame.layer != Layer::UsageAlarm && frame.layer != Layer::EventCody &&
        frame.layer != Layer::Surprise &&
        (!qr_view || qr_opacity != 0);
    const auto saved_epoch = frame.layer == Layer::Dashboard && !frame.statistics_unavailable
        ? CODY::device_saved_page_epoch(ui.model.dashboard, screen) : 0;
    const auto chrome_error = CODY::prepare_device_chrome(ui.state.view.chrome, ui.model.dashboard, show_connection,
        show_battery ? ui.state.battery_banner : CODY::BatteryAlertNotice{}, saved_epoch);
    if (chrome_error != ESP_OK) return chrome_error;
    const auto &chrome = ui.state.view.chrome;
    // QR pages reserve their complete surface for the title and scannable code.
    const bool show_status = frame.layer != Layer::Blank && frame.layer != Layer::Branding &&
        frame.layer != Layer::Birth && frame.layer != Layer::Surprise && (!qr_view || qr_opacity != 0);
    if (show_status) {
        const auto status_error = CODY::prepare_status_overlay(ui.model.dashboard.hardware, ui.state.status);
        if (status_error != ESP_OK) return status_error;
    }
    const bool status_changed = show_status != ui.state.status_retired ||
        (show_status && !CODY::status_overlay_equal(ui.state.status, ui.state.retired_status));
    // Settings are read only from the canonical observation. A failed or invalid
    // preference does not invent an enabled ring or alter status/battery alerts.
    static_assert(sizeof(LUNASHELL::CodyOptionsObservation) <= 256U);
    LUNASHELL::CodyOptionsObservation options{};
    const auto options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(options);
    const bool quota_ring = options_error == ESP_OK && options.preferences.assistant_quota_ring_valid &&
        options.preferences.assistant_quota_ring_error == ESP_OK && options.preferences.assistant_quota_ring_enabled;
    const bool battery_ring = options_error == ESP_OK && options.preferences.battery_ring_valid &&
        options.preferences.battery_ring_error == ESP_OK && options.preferences.battery_ring_enabled;
    const uint16_t allowance = show_battery && quota_ring ? CODY::device_allowance_ring(ui.model.dashboard) : 0;
    const auto &battery = ui.model.dashboard.hardware.battery;
    const bool battery_current = show_battery && battery_ring && ui.model.dashboard.hardware_error == ESP_OK &&
        battery.data_valid && !battery.data_stale && battery.last_error == ESP_OK &&
        battery.level_percent_valid && battery.level_percent >= 0 && battery.level_percent <= 100;
    const int8_t battery_percent = battery_current ? static_cast<int8_t>(battery.level_percent) : -1;
    const auto model_background = frame.layer == Layer::Counter && ui.state.counter_page == 2
        ? ui.state.counter_model_background : CODY::ModelBackground::None;
    CODY::DamageRect startup_animation_bounds{};
    const bool startup_animation_changed = frame.layer == Layer::Scene &&
        ui.state.completed_scene.content.revision != ui.state.scene.content.revision &&
        CODY::onboarding_startup_animation_damage(startup_animation_bounds);
    const bool full = force || !old.valid || old.display_revision != revision ||
        old.layer != frame.layer || old.screen != screen ||
        old.model_background != model_background || old.onboarding_chrome != onboarding_chrome ||
        old.allowance != allowance || old.battery_percent != battery_percent ||
        old.statistics_unavailable != frame.statistics_unavailable || old.qr_opacity != qr_opacity ||
        old.setup_opacity != setup_opacity || old.settings_notice != settings_notice ||
        (frame.layer == Layer::Scene && ui.state.scene_dirty);
    const bool draws_navigation = frame.arrows || frame.layer == Layer::Scene || show_status;
    // Menu Back remains visible and tappable instead of disappearing with idle chrome.
    const bool persistent_menu_back = frame.layer == Layer::BootMenu || frame.layer == Layer::Prices ||
        frame.layer == Layer::ThirdParty || frame.layer == Layer::CodexBrowser ||
        (frame.layer == Layer::Scene && ui.state.scene.valid &&
         ui.state.scene.definition->id == CODY::SceneId::MakeItYours);
    const uint8_t opacity = qr_view ? qr_opacity : persistent_menu_back ? 255 :
        draws_navigation ? frame.navigation_opacity : 0;
    const bool dashboard_next = frame.layer != Layer::Dashboard || screen == WidgetScreen::About ||
        CODY::next_visible_widget(ui.state.available_statistics, screen, WidgetStep::Next) != screen;
    const bool next_navigation = frame.arrows && dashboard_next && frame.layer != Layer::Dashboard &&
        frame.layer != Layer::AboutLink &&
        frame.layer != Layer::VoucherWarning && frame.layer != Layer::CurrentTurn &&
        frame.layer != Layer::CodexBrowser && frame.layer != Layer::Events &&
        frame.layer != Layer::Audio && frame.layer != Layer::BootMenu && frame.layer != Layer::ThirdParty && frame.layer != Layer::Prices;
    const bool navigation_changed = old.navigation_opacity != opacity || old.next_navigation != next_navigation;
    const auto same_bounds = [](const CODY::DamageRect &first, const CODY::DamageRect &second) {
        return first.left == second.left && first.top == second.top &&
            first.right == second.right && first.bottom == second.bottom;
    };
    const auto &heading = (frame.layer == Layer::Scene ? ui.state.scene.content : view.content).heading;
    const auto &slot = heading.bounds;
    const CODY::DamageRect heading_bounds{slot.x, slot.y, static_cast<uint16_t>(slot.x + slot.width),
        static_cast<uint16_t>(slot.y + slot.height)};
    const auto key = heading_key(heading);
    const bool same_heading = key == old.heading_key && same_bounds(heading_bounds, old.heading_bounds);
    const bool same_heading_route = old.valid && old.layer == frame.layer && old.screen == screen &&
        (frame.layer != Layer::Scene || ui.state.completed_scene.definition == ui.state.scene.definition);
    const auto heading_started_us = same_heading && same_heading_route ? old.heading_started_us : ui.state.awake_us;
    const auto heading_width = CODY::TitleStyleDetail::width(heading.text.length, heading.scale);
    const bool heading_scrolls = heading_width > slot.width;
    const auto heading_offset = CODY::styled_title_scroll_offset(heading_width, slot.width,
        ui.state.awake_us - heading_started_us);
    const bool heading_changed = !same_heading || heading_offset != old.heading_offset;
    const bool content_changed = old.content_revision != view.content.revision ||
        !same_bounds(old.content_bounds, view.content.bounds) ||
        !same_bounds(old.bottom_action_bounds, view.content.bottom_action_bounds);
    uint8_t carousel_page = 0;
    uint8_t carousel_count = 0;
    uint16_t carousel_mask = 0;
    if (frame.layer == Layer::Counter) {
        carousel_mask = ui.state.counter_mask;
        for (uint8_t page = 0; page < 3; ++page) {
            if ((ui.state.counter_mask & (1U << page)) == 0) continue;
            if (page == ui.state.counter_page) carousel_page = carousel_count;
            ++carousel_count;
        }
    } else if (frame.layer == Layer::Dashboard && screen != WidgetScreen::About) {
        for (const auto &entry : CODY::kWidgetScreens) {
            if (!CODY::widget_visible(ui.state.available_statistics, entry.id)) continue;
            if (entry.id == screen) carousel_page = carousel_count;
            ++carousel_count;
            carousel_mask |= CODY::WidgetStateDetail::bit(entry.id);
        }
    }
    const bool carousel_changed = old.page_mark_mask != carousel_mask;
    const bool setup_phase_changed = setup_opacity != 0 && old.setup_phase != setup_phase;
    const bool battery_changed = old.battery_sequence != chrome.battery_sequence;
    const bool connection_changed = old.connection_revision != chrome.connection_revision;
    const bool connection_activity_changed = old.connection_activity_phase != frame.connection_activity_phase;
    const bool voice_ribbon_changed = old.voice_ribbon_at_us != frame.voice_ribbon_at_us;
    auto &pixel_view = frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody ? ui.character.animation_preview : ui.character.pixel_demo;
    const bool pixel_demo_changed = (frame.pc_alert_detail || frame.guide_mascot || frame.waiting_mascot || frame.failure_mascot || frame.layer == Layer::Mascot || frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody) &&
        pixel_view.damage.count != 0;
    CODY::DamageRect selected_label_bounds{};
    uint16_t selected_label_offset = UINT16_MAX;
    const bool has_selected_label = frame.layer == Layer::Scene &&
        CODY::onboarding_selected_label_damage(selected_label_bounds, selected_label_offset);
    const bool selected_label_changed = has_selected_label && old.selected_label_offset != selected_label_offset;
    const bool scene_title_moved = frame.layer == Layer::Scene &&
        CODY::scene_title_scroll_offset(ui.state.completed_scene) != CODY::scene_title_scroll_offset(ui.state.scene);
    if (!full && !content_changed && !navigation_changed && !battery_changed && !connection_changed &&
        !connection_activity_changed && !voice_ribbon_changed && !pixel_demo_changed && !status_changed &&
        !selected_label_changed && !scene_title_moved &&
        !heading_changed && !carousel_changed && !setup_phase_changed && !startup_animation_changed) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        if (admit_voice) LUNASHELL::setVoiceInputRouteAvailable(true);
#endif
        return ESP_OK;
    }

    bool parked = false;
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
    const bool studio_target = s_studio_identity.scenario_revision[0] != '\0' && studio_frame_matches(frame);
    cody_host_display_stage_studio_identity(studio_target ? &s_studio_identity : nullptr);
#endif
    esp_err_t error = LUNASHELL_display_frame_begin(kFrameBudgetMs, &parked);
    if (error != ESP_OK) {
        old.valid = false;
        return parked ? ESP_ERR_NOT_FINISHED : error;
    }

    const int64_t admitted_at_us = esp_timer_get_time();
    bool native_timeout = false;
    const uint32_t frame_revision = LUNASHELL_display_runtime_content_revision();
    const bool redraw_all = full || frame_revision != revision;
    constexpr CODY::DamageRect navigation_bounds{0, 202, kGeometry.width, 264};
    auto bounds = redraw_all ? CODY::DamageRect{0, 0, kGeometry.width, kGeometry.height} : CODY::DamageRect{};
    const auto include_damage = [&bounds](CODY::DamageRect changed) {
        if (changed.empty()) return;
        if (bounds.empty()) bounds = changed;
        else bounds = {std::min(bounds.left, changed.left), std::min(bounds.top, changed.top),
            std::max(bounds.right, changed.right), std::max(bounds.bottom, changed.bottom)};
    };
    if (!redraw_all) {
        if (startup_animation_changed) include_damage(startup_animation_bounds);
        if (content_changed) {
            include_damage(old.content_bounds);
            include_damage(old.bottom_action_bounds);
            include_damage(view.content.bounds);
            include_damage(view.content.bottom_action_bounds);
        }
        if (carousel_changed) include_damage({170, 435, 296, 439});
        if (setup_phase_changed) {
            include_damage(CODY::setup_progress_damage(old.setup_phase));
            include_damage(CODY::setup_progress_damage(setup_phase));
        }
        if (navigation_changed) {
            include_damage(navigation_bounds);
            if (show_status) include_damage(CODY::status_overlay_damage_bounds());
            if (frame.layer == Layer::Scene) include_damage(ui.state.scene.content.bottom_action_bounds);
        }
        if (battery_changed) include_damage({105, 397, 361, 427});
        if (connection_changed) include_damage(CODY::kDeviceChromeConnectionBounds);
        if (connection_activity_changed) include_damage(CODY::kConnectionActivityBounds);
        if (status_changed) include_damage(CODY::status_overlay_damage_bounds());
        if (selected_label_changed) include_damage(selected_label_bounds);
        if (heading_changed) {
            include_damage(old.heading_bounds);
            include_damage(heading_bounds);
        }
        if (scene_title_moved) {
            const auto &title = ui.state.scene.title.bounds;
            include_damage({title.x, title.y, static_cast<uint16_t>(title.x + title.width),
                static_cast<uint16_t>(title.y + title.height)});
        }
        if (voice_ribbon_changed) {
            include_damage({CODY::kVoiceRibbonLeft, CODY::kVoiceRibbonDamageTop,
                CODY::kVoiceRibbonLeft + CODY::kVoiceRibbonWidth, CODY::kVoiceRibbonDamageBottom});
        }
    }
    CODY::set_home_wifi_chrome_opacity(opacity);
    // region: One half-open display rectangle; shares the current frame's DMA lease.
    const auto draw_region = [&](CODY::DamageRect region) {
        const uint16_t left = region.left & ~1U;
        const uint16_t right = (region.right + 1U) & ~1U;
        const uint16_t top = region.top & ~1U;
        const uint16_t bottom = (region.bottom + 1U) & ~1U;
        const uint16_t width = right - left;
        if (!kGeometry.admits_transfer(left, top, right, bottom)) error = ESP_ERR_INVALID_SIZE;
        for (uint16_t row = top; error == ESP_OK && row < bottom;) {
            void *pixels = nullptr;
            size_t capacity = 0;
            error = LUNASHELL_display_frame_buffer(&pixels, &capacity);
            if (error != ESP_OK) {
                native_timeout = error == ESP_ERR_TIMEOUT;
                break;
            }
            // The driver admits rows by full panel width even for a narrow update.
            const size_t max_rows = (capacity / (kGeometry.width * sizeof(uint16_t))) & ~size_t{1};
            if (pixels == nullptr || max_rows == 0 || reinterpret_cast<uintptr_t>(pixels) % alignof(uint16_t) != 0) {
                error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const auto rows = static_cast<uint16_t>(std::min<size_t>(max_rows, bottom - row));
            const CODY2D::Surface strip{static_cast<uint16_t *>(pixels), width, rows, width,
                static_cast<uint32_t>(capacity / sizeof(uint16_t)), CODY2D::Rgb565ByteOrder::ByteSwapped};
            const uint16_t background = (frame.guide_mascot || frame.failure_mascot) ? CODY::Theme::surface_pairing : frame.layer == Layer::Scene ? ui.state.scene.definition->background_rgb565 :
                frame.layer == Layer::Birth ? ui.state.birth_background : 0;
            const auto art = frame.layer == Layer::Scene && !frame.guide_mascot ? CODY::onboarding_artwork() :
                CODY::OnboardingArtwork::None;
            // Full-panel indexed artwork is opaque and completely initializes
            // every admitted tile. Avoid clearing those same pixels first.
            error = art != CODY::OnboardingArtwork::None ? draw_onboarding_art_rows(art, strip, row, left) :
                frame.layer == Layer::Surprise ? ESP_OK : CODY2D::clear(strip, background);
            if (error == ESP_OK && model_background != CODY::ModelBackground::None) {
                // The artwork stays below all model text and inside the content damage bounds.
                error = CODY::draw_model_background_rows(model_background, {177, 270, 112, 112}, strip, row, left);
            }
            if (error == ESP_OK && frame.layer == Layer::Surprise) {
                error = draw_surprise_rows(ui.state.surprise_page, strip, row, left);
            } else if (error == ESP_OK && frame.layer == Layer::Branding) {
                error = CODY::BootBranding::draw_rows(ui.state.branding, ui.state.branding_scratch, ui.state.branding_frame, strip, row);
            } else if (error == ESP_OK && frame.layer == Layer::Birth && ui.state.birth_frame >= kBirthMuseStartFrame) {
                error = CODY::draw_pixel_avatar_rows(ui.character.birth_avatar, {105, 72, 256, 256}, strip, row, left);
                if (error == ESP_OK && ui.state.birth_frame >= kBirthWelcomeFrame) {
                    const auto &welcome = ui.state.birth_welcome;
                    const auto width = CODY::TitleStyleDetail::width(welcome.text.length, welcome.scale);
                    const auto elapsed_us = (ui.state.birth_frame - kBirthWelcomeFrame) * kBirthFrameMs * 1000ULL;
                    error = CODY::draw_styled_title_rows(welcome, strip, row, left, welcome.text.length, false,
                        CODY::styled_title_scroll_offset(width, welcome.bounds.width, elapsed_us));
                }
            } else if (error == ESP_OK && frame.layer == Layer::Scene) {
                if ((frame.pc_alert_detail || frame.guide_mascot || frame.waiting_mascot) && view.content.draw_rows != nullptr) {
                    error = view.content.draw_rows(view.content.state, strip, row, left);
                }
                if (error == ESP_OK) error = CODY::draw_scene_rows(ui.state.scene, strip, row, left, heading_offset);
                if (error == ESP_OK && frame.connection_activity_phase != 0) {
                    error = CODY::draw_connection_activity_rows(frame.connection_activity_phase, strip, row, left);
                }
            } else if (error == ESP_OK) {
                if (frame.failure_mascot && frame.failure_content.draw_rows != nullptr)
                    error = frame.failure_content.draw_rows(frame.failure_content.state, strip, row, left);
                if (error == ESP_OK && view.content.draw_rows != nullptr) {
                    error = view.content.draw_rows(view.content.state, strip, row, left);
                }
                if (error == ESP_OK) {
                    error = CODY::draw_content_heading_rows(view.content, strip, row, left, heading_offset);
                }
            }
            if (error == ESP_OK && frame.layer == Layer::Scene && ui.state.scene.definition->title_visible &&
                (frame.guide_mascot || CODY::onboarding_connection_mascot() == 2)) {
                const auto accent_top = static_cast<uint16_t>(ui.state.scene.title.bounds.y +
                    ui.state.scene.title.bounds.height + 2U);
                const uint16_t mascot_top = frame.guide_mascot ? 113U : 130U;
                if (accent_top + 2U <= mascot_top) error = draw_onboarding_title_accent(strip, row, left, accent_top);
            } else if (error == ESP_OK && frame.failure_mascot && view.content.heading.scale != 0) {
                const auto accent_top = static_cast<uint16_t>(view.content.heading.bounds.y +
                    view.content.heading.bounds.height + 2U);
                if (accent_top + 2U <= 134U) error = draw_onboarding_title_accent(strip, row, left, accent_top);
            }
            if (error == ESP_OK && carousel_count > 1) {
                error = CODY::draw_page_marks(carousel_page, carousel_count, strip, row, left, 435, true);
            }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
            if (error == ESP_OK && frame.voice_ribbon_at_us != 0) {
                error = CODY::draw_voice_ribbon_rows(strip, row, left, ui.state.voice_ribbon);
            }
#endif
            if (error == ESP_OK && !onboarding_chrome && (allowance != 0 || battery_percent >= 0)) {
                error = CODY::draw_device_perimeter_rows(allowance, battery_percent, strip, row, left,
                    qr_view ? qr_opacity : 255);
            }
            if (error == ESP_OK && onboarding_chrome && (!qr_view || qr_opacity != 0)) {
                error = draw_onboarding_perimeter(strip, row, left, qr_view ? qr_opacity : 255);
            } else if (error == ESP_OK && setup_opacity != 0) {
                error = CODY::draw_setup_progress_rows(setup_phase, setup_opacity, strip, row, left);
            } else if (error == ESP_OK && qr_view && qr_opacity != 0) {
                error = CODY::draw_setup_progress_rows(0, qr_opacity, strip, row, left, false);
            }
            if (error == ESP_OK && frame.layer == Layer::Scene) {
                error = CODY::draw_onboarding_navigation(strip, row, left, opacity, background);
            }
            const bool show_back = frame.layer == Layer::Dashboard ? ui.state.about_from_options && screen == WidgetScreen::About &&
                    ui.state.info_page == CODY::InfoPage::System : true;
            if (error == ESP_OK && frame.arrows && show_back && row < navigation_bounds.bottom &&
                row + rows > navigation_bounds.top) {
                error = CODY::draw_lateral_navigation(false, strip, row, left, opacity);
            }
            if (error == ESP_OK && show_status) {
                error = CODY::draw_status_overlay_rows(ui.state.status, opacity, background, strip, row, left);
            }
            if (error == ESP_OK && frame.layer != Layer::Birth && frame.layer != Layer::Surprise && !qr_view)
                error = CODY::draw_device_chrome(chrome, strip, row, left,
                    settings_notice == 1 ? &ui.state.settings_retry_notice :
                    settings_notice == 2 ? &ui.state.settings_music_notice :
                    settings_notice == 3 ? &ui.state.whatsapp_notice : nullptr);
            if (error == ESP_OK) {
                error = LUNASHELL_display_frame_submit_rgb565(left, row, right, row + rows, pixels);
                native_timeout = error == ESP_ERR_TIMEOUT;
            }
            row += rows;
        }
    };
    if (!bounds.empty() || !pixel_demo_changed) draw_region(bounds);
    if (!redraw_all && pixel_demo_changed) {
        for (size_t i = 0; error == ESP_OK && i < pixel_view.damage.count; ++i) {
            const auto &rect = pixel_view.damage.rectangles[i];
            draw_region({rect.x, rect.y, static_cast<uint16_t>(rect.x + rect.width),
                static_cast<uint16_t>(rect.y + rect.height)});
        }
    }

    const esp_err_t draw_error = error;
    error = LUNASHELL_display_frame_end(error);
    const int64_t retired_at_us = esp_timer_get_time();
    if (error != ESP_OK) {
        old.valid = false;
        if (error == ESP_ERR_TIMEOUT &&
            (draw_error == ESP_OK || (draw_error == ESP_ERR_TIMEOUT && native_timeout))) {
            static_assert(sizeof(LUNASHELL::DisplayFrameSnapshot) <= 128U);
            LUNASHELL::DisplayFrameSnapshot metrics{};
            const auto observation_error = DeviceSnapshotService::instance().readDisplayFrameObservation(metrics);
            if (observation_error == ESP_OK && can_defer_frame_timeout(metrics, draw_error, error,
                    native_timeout, admitted_at_us, retired_at_us)) {
                // Keep the original TIMEOUT in the published metrics. A deferred
                // frame cannot acknowledge Wake/Sleep, play its cue or commit UI state.
                invalidate();
                frame_retry_at_us = esp_timer_get_time() + 100000;
                ESP_LOGW(kTag, "Frame deadline deferred after confirmed DMA drain; full redraw queued");
                return ESP_ERR_NOT_FINISHED;
            }
        }
        return error;
    }
    frame_retry_at_us = 0;
    if (!old.valid || old.display_revision != frame_revision || old.layer != frame.layer || old.screen != screen ||
        (content_changed &&
         (frame.layer == Layer::CodexBrowser ||
          ui.state.contact == Target::FirstAction || ui.state.contact == Target::SecondAction ||
          ui.state.contact == Target::VoucherScroll || ui.state.contact == Target::VoucherDismiss))) {
        ui.state.contact = Target::None;
    }
    if (old.navigation_opacity == 255 && opacity < 255 && !qr_view) {
        ui.state.contact = Target::None;
        CODY::cancel_onboarding_touch();
    }
    if (chrome.battery_sequence != 0 && battery_changed) {
        (void)CODY::acknowledge_battery_alert(ui.model.battery, ui.state.battery_banner);
    }
    const bool failure_visible = frame.failure_mascot ||
        (frame.layer == Layer::Scene && CODY::onboarding_connection_mascot() == 2);
    if (failure_visible && (!old.valid || !old.failure_mascot || old.layer != frame.layer)) {
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        (void)CODY::play_device_symbol_cue(ui.character.effects, CODY::MuseSymbol::Sweat, voice);
    }
    if (frame.guide_mascot && (!old.valid || old.layer != Layer::Scene ||
        ui.state.completed_scene.content.control_revision != ui.state.scene.content.control_revision)) {
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        const auto visual = CODY::onboarding_guide_visual();
        const auto cue = visual == 3 ? CODY::PetCue::Jump :
            visual == 2 ? CODY::PetCue::Surprise : CODY::PetCue::Tap;
        (void)CODY::play_pet_cue(ui.character.effects, cue, voice);
    }
    old = {frame.layer, screen, frame_revision, view.content.revision, chrome.battery_sequence,
        chrome.connection_revision, allowance, battery_percent, opacity, frame.connection_activity_phase,
        frame.statistics_unavailable, next_navigation, true};
    old.voucher_bounds = frame.layer == Layer::Dashboard && screen == WidgetScreen::Credits &&
        !frame.statistics_unavailable ? ui.state.view.account.voucher_bounds : CODY2D::Rect{};
    old.voice_ribbon_at_us = frame.voice_ribbon_at_us;
    old.content_bounds = view.content.bounds;
    old.bottom_action_bounds = view.content.bottom_action_bounds;
    old.selected_label_offset = has_selected_label ? selected_label_offset : UINT16_MAX;
    old.page_mark_mask = carousel_mask;
    old.settings_notice = settings_notice;
    old.setup_phase = setup_phase;
    old.setup_opacity = setup_opacity;
    old.qr_opacity = qr_opacity;
    old.heading_bounds = heading_bounds;
    old.heading_key = key;
    old.heading_started_us = heading_started_us;
    old.heading_offset = heading_offset;
    old.heading_scrolls = heading_scrolls;
    old.model_background = model_background;
    old.failure_mascot = failure_visible;
    old.onboarding_chrome = onboarding_chrome;
    ui.state.status_retired = show_status;
    if (show_status) ui.state.retired_status = ui.state.status;
    if (frame.layer == Layer::Mascot || frame.pc_alert_detail || frame.guide_mascot || frame.waiting_mascot || frame.failure_mascot) CODY::retire_pixel_demo(ui.character.pixel_demo);
    if (frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody)
        CODY::retire_pixel_demo(ui.character.animation_preview);
    if (frame.layer == Layer::Scene) {
        ui.state.completed_scene = ui.state.scene;
        ui.state.scene_dirty = false;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (admit_voice) LUNASHELL::setVoiceInputRouteAvailable(true);
#endif
    return ESP_OK;
}

// point: Logical position on the last fully retired page.
// Exception: None.
Target target_at(CODY2D::Point point)
{
    const auto &ui = *s_ui;
    const auto layer = ui.state.retired.layer;
    if (layer == Layer::ThirdParty) {
        return ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous
            ? Target::ThirdPartyBack : Target::None;
    }
    if (layer == Layer::AboutLink) return Target::Previous;
    if (layer == Layer::Audio) {
        // The view is mutable during preparation; admit only the last retired revision.
        if (ui.state.audio_view.revision != ui.state.retired.content_revision) return Target::None;
        if (ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::AudioClose;
        switch (CODY::audio_adjustment_at(ui.state.audio_view, point)) {
        case CODY::AudioAdjustment::VolumeDown: return Target::AudioVolumeDown;
        case CODY::AudioAdjustment::VolumeUp: return Target::AudioVolumeUp;
        case CODY::AudioAdjustment::GainDown: return Target::AudioGainDown;
        case CODY::AudioAdjustment::GainUp: return Target::AudioGainUp;
        case CODY::AudioAdjustment::None: break;
        }
        return CODY::audio_close_at(ui.state.audio_view, point) ? Target::AudioClose : Target::None;
    }
    if (layer == Layer::CodexBrowser) {
        if (ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
        return CODY::codex_browser_control_at(ui.state.codex_browser, point).action != CODY::CodexBrowserAction::None
            ? Target::CodexBrowserControl : Target::None;
    }
    if ((layer == Layer::Events || layer == Layer::Dashboard) &&
        ui.state.browser_destination != CODY::CodexBrowserDestination::None && ui.state.retired.navigation_opacity == 255 &&
        CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
    if (layer == Layer::Events) return Target::EventsGesture;
    if (layer == Layer::CurrentTurn) {
        if (ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) {
            return layer == Layer::CurrentTurn &&
                ui.state.browser_destination == CODY::CodexBrowserDestination::CurrentTurn
                ? Target::CodexBrowserBack : Target::ActivityBack;
        }
        return Target::None;
    }
    if (layer == Layer::VoucherWarning) {
        if (ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::VoucherDismiss;
        return CODY::device_action_at(ui.state.view, point) == 1 ? Target::VoucherDismiss : Target::None;
    }
    if (ui.state.retired.navigation_opacity == 255 && layer == Layer::Dashboard && ui.state.about_from_options &&
        ui.state.retired.screen == WidgetScreen::About && ui.state.info_page == CODY::InfoPage::System &&
        CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::Previous;
    if (layer == Layer::Dashboard && ui.state.retired.statistics_unavailable) return Target::None;
    if (layer == Layer::Dashboard && ui.state.retired.screen == WidgetScreen::Credits) {
        const auto &bounds = ui.state.retired.voucher_bounds;
        if (point.x >= bounds.x && point.x < bounds.x + bounds.width &&
            point.y >= bounds.y && point.y < bounds.y + bounds.height) return Target::VoucherScroll;
    }
    if (layer == Layer::Dashboard && ui.state.retired.screen == WidgetScreen::TokenHistory &&
        CODY::hit_token_history_interval(ui.state.view.history, point)) return Target::Interval;
    if (layer == Layer::Voice || layer == Layer::Diagnostics ||
        (layer == Layer::Dashboard && ui.state.retired.screen == WidgetScreen::Device)) {
        switch (CODY::device_action_at(ui.state.view, point)) {
        case 1: return Target::FirstAction;
        case 2: return Target::SecondAction;
        default: break;
        }
    }
    return Target::None;
}

// ui: Owns the retired Events view, press identity and local navigation.
// Exception: Optional selection errors are logged without stopping the display worker.
void activate_events_gesture(DeviceRuntime &ui)
{
    const int dx = ui.state.activity_touch_last.x - ui.state.activity_touch_start.x;
    const int dy = ui.state.activity_touch_last.y - ui.state.activity_touch_start.y;
    if (std::abs(dx) >= 44 && std::abs(dx) > std::abs(dy)) {
        if (dx < 0) {
            ui.state.activity_page = ActivityPage::CurrentTurn;
        } else {
            select_statistics();
        }
        invalidate();
        return;
    }
    const auto &page = ui.state.codex_panel.page;
    if (std::abs(dy) >= 34 && std::abs(dy) > std::abs(dx)) {
        const int maximum = std::max(0, int(page.total) - int(CODY::kNotificationPageSize));
        ui.state.notification_offset = static_cast<uint16_t>(std::clamp(int(page.offset) + (dy < 0 ? 1 : -1), 0, maximum));
        ui.state.notification_pc_requested = UINT16_MAX;
        ui.state.notification_action_error = ESP_OK;
        invalidate();
        return;
    }
    if (std::abs(dx) > 16 || std::abs(dy) > 16) return;
    const int row = CODY::codex_event_row_at(ui.state.activity_touch_last);
    if (ui.state.codex_panel.revision != ui.state.retired.content_revision || row < 0 || row >= page.count ||
        !(page.rows[row].key == ui.state.touched_event) || !page.rows[row].current) return;
    const auto &event = page.rows[row];
    const auto error = event.key.origin == CODY::NotificationOrigin::Local ?
        CODY::open_onboarding_local_notification(event.key.local_id) :
        CODY::open_onboarding_notification(event.key.remote_id);
    if (error == ESP_OK && event.key.origin == CODY::NotificationOrigin::Remote)
        CODY::note_device_notification_opened(ui.character.effects, event.key.remote_id);
    ui.state.notification_action_error = error;
    invalidate();
    if (error != ESP_OK) ESP_LOGW(kTag, "Notification open failed (%s)", esp_err_to_name(error));
}

// target: Released control admitted on the same complete frame.
// Exception: Returns a real voice transport admission error.
esp_err_t activate(Target target)
{
    auto &ui = *s_ui;
    if (target == Target::ThirdPartyBack) {
        ui.state.third_party_open = false;
        invalidate();
        return ESP_OK;
    }
    const auto layer = ui.state.retired.layer;
    if (target == Target::CodexBrowserBack) {
        if (layer == Layer::CodexBrowser) {
            CODY::back_codex_browser(ui.state.codex_browser);
        } else {
            const auto error = CODY::return_to_options_tools(ui.state.scene);
            if (error != ESP_OK) return error;
            if (!CODY::onboarding_codex_tools_active()) return ESP_OK;
            const auto origin = ui.state.browser_destination;
            ui.state.browser_destination = CODY::CodexBrowserDestination::None;
            if ((origin == CODY::CodexBrowserDestination::Events &&
                 ui.state.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions) ||
                (origin == CODY::CodexBrowserDestination::CurrentTurn &&
                 ui.state.codex_browser.route == CODY::CodexBrowserRoute::Turns)) {
                ui.state.codex_browser.refresh_at_us = 0;
            } else {
                CODY::open_codex_browser(ui.state.codex_browser);
            }
        }
        invalidate();
    } else if (target == Target::CodexBrowserControl && layer == Layer::CodexBrowser) {
        const auto start = CODY::codex_browser_control_at(ui.state.codex_browser, ui.state.activity_touch_start);
        const auto end = CODY::codex_browser_control_at(ui.state.codex_browser, ui.state.activity_touch_last);
        if (start.action == CODY::CodexBrowserAction::None || !(start == end)) return ESP_OK;
        const bool questions_to_events = ui.state.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions &&
            start.action == CODY::CodexBrowserAction::Events;
        const auto destination = CODY::activate_codex_browser(ui.state.codex_browser, start);
        if (destination != CODY::CodexBrowserDestination::None) {
            const auto error = CODY::open_device_dashboard(ui.state.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) return error;
            ui.state.browser_destination = destination;
            if (destination == CODY::CodexBrowserDestination::Statistics) {
                select_statistics();
            } else if (destination == CODY::CodexBrowserDestination::CurrentTurn) {
                ui.state.activity_page = ActivityPage::CurrentTurn;
                ui.state.activity_navigation_sound_pending = true;
            } else {
                ui.state.activity_page = ActivityPage::Events;
                ui.state.questions_navigation_sound_pending = questions_to_events;
            }
        }
        invalidate();
    } else if (target == Target::EventsGesture && layer == Layer::Events) {
        activate_events_gesture(ui);
    } else if (target == Target::ActivityBack) {
        ui.state.activity_page = ActivityPage::Events;
        invalidate();
    } else if (layer == Layer::VoucherWarning) {
        if (target == Target::VoucherDismiss) {
            CODY::dismiss_device_voucher_warning(ui.model);
            invalidate();
        }
    } else if (target == Target::VoucherScroll) {
        const int dx = ui.state.voucher_touch_last.x - ui.state.voucher_touch_start.x;
        const int dy = ui.state.voucher_touch_last.y - ui.state.voucher_touch_start.y;
        if (std::abs(dy) >= 32 && std::abs(dy) > std::abs(dx)) {
            CODY::step_device_voucher(ui.model.vouchers, ui.state.voucher_first_row, dy < 0 ? WidgetStep::Next : WidgetStep::Previous);
        }
    } else if (layer == Layer::Audio) {
        if (target == Target::AudioClose) {
            ui.state.audio_open = false;
            invalidate();
        } else if (target == Target::AudioVolumeDown || target == Target::AudioVolumeUp ||
                   target == Target::AudioGainDown || target == Target::AudioGainUp) {
            // A control may have become disabled while the finger was held.
            if (target_at(ui.state.activity_touch_last) != target) return ESP_OK;
            const bool volume = target == Target::AudioVolumeDown || target == Target::AudioVolumeUp;
            const bool increase = target == Target::AudioVolumeUp || target == Target::AudioGainUp;
            const uint32_t current = volume ? ui.state.audio_view.playback_volume : ui.state.audio_view.microphone_gain;
            const uint32_t step = volume ? 10U : 3U;
            const uint32_t maximum = volume ? 100U : 30U;
            const uint32_t next = increase ? std::min(maximum, current + step) :
                current < step ? 0U : current - step;
            const auto preference = volume ? CODY::PreferenceId::PlaybackVolume : CODY::PreferenceId::MicrophoneGain;
            const auto error = CODY::request_audio_preference(preference, next);
            ui.state.audio_setting_error = error;
            ui.state.audio_setting_pending = error == ESP_OK;
            if (error != ESP_OK) ESP_LOGW(kTag, "Audio setting admission failed (%s)", esp_err_to_name(error));
            invalidate();
        }
    } else if (layer == Layer::AboutLink) {
        ui.state.info_page = CODY::InfoPage::VibeWare;
        invalidate();
    } else if (target == Target::Previous || target == Target::Next) {
        if (target == Target::Previous && ui.state.about_from_options &&
            ui.state.widgets.selected_screen == WidgetScreen::About && ui.state.info_page == CODY::InfoPage::System) {
            if (ui.state.about_from_boot_menu) {
                if (ui.state.about_return_settings) {
                    const auto error = CODY::return_to_options_page(ui.state.scene, ui.state.about_return_settings_page);
                    if (error != ESP_OK) return error;
                }
                ui.state.about_from_options = false;
                ui.state.about_from_boot_menu = false;
                ui.state.third_party_open = ui.state.about_return_third_party;
                ui.state.prices_open = ui.state.about_return_prices;
                ui.state.boot_section = ui.state.about_return_section;
                ui.state.mascot_home = ui.state.about_return_home;
                ui.state.activity_page = ui.state.about_return_activity;
                ui.state.widgets.selected_screen = ui.state.about_return_screen;
                ui.state.info_page = ui.state.about_return_info;
                ui.state.boot_menu_open = true;
                invalidate();
                return ESP_OK;
            }
            const auto error = CODY::return_to_options_device(ui.state.scene);
            if (error != ESP_OK) return error;
            ui.state.about_from_options = false;
            invalidate();
            return ESP_OK;
        }
        const auto step = target == Target::Previous ? WidgetStep::Previous : WidgetStep::Next;
        const bool info_changed = ui.state.widgets.selected_screen == WidgetScreen::About &&
            CODY::step_info_page(ui.state.info_page, step);
        if (!info_changed) {
            const auto page = CODY::next_visible_widget(ui.state.available_statistics, ui.state.widgets.selected_screen, step);
            if (page) {
                CODY::select_widget_screen(ui.state.widgets, *page);
                ui.state.info_page = CODY::InfoPage::System;
            }
        }
        invalidate();
    } else if (target == Target::Interval) {
        using Interval = CODY::TokenHistoryInterval;
        const auto previous = ui.state.interval;
        do {
            switch (ui.state.interval) {
            case Interval::OneHour: ui.state.interval = Interval::FiveHours; break;
            case Interval::FiveHours: ui.state.interval = Interval::TwentyFourHours; break;
            case Interval::TwentyFourHours: ui.state.interval = Interval::OneHour; break;
            }
        } while (ui.state.interval != previous && !CODY::device_history_has_data(ui.model.dashboard, ui.state.interval));
    } else if (layer == Layer::Diagnostics) {
        ui.state.diagnostics = false;
        invalidate();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    else if (layer == Layer::Voice) {
        bool accepted = false;
        const auto phase = ui.model.voice.input.phase;
        const bool send = target == Target::FirstAction && phase == VoicePhase::Listening;
        const auto error = send ? LUNASHELL::finishVoiceInput(ui.state.presented_voice_session, accepted) :
            LUNASHELL::cancelVoiceInput(ui.state.presented_voice_session, accepted);
        if (error != ESP_OK) ESP_LOGW(kTag, "Voice action failed (%s)", esp_err_to_name(error));
    }
#endif
    return ESP_OK;
}

// ui: Retained audio state owned by the display worker.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_touch_cue(DeviceRuntime &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    if (ui.state.retired.layer == Layer::Scene && CODY::onboarding_wifi_keyboard_active()) {
        CODY::play_device_key_cue(ui.character.effects, voice);
    } else {
        CODY::play_device_tap_cue(ui.character.effects, voice);
    }
}

// ui: Display-owned effects and the current canonical voice observation.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_preference_cue(DeviceRuntime &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_preference_cue(ui.character.effects, voice);
}

// ui: Display-owned effects and current voice state for a confirmed bookmark.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_bookmark_cue(DeviceRuntime &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_bookmark_cue(ui.character.effects, voice);
}

// ui: Display-owned effects for navigation to the authenticated events list.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_questions_cue(DeviceRuntime &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_questions_cue(ui.character.effects, voice);
}

// ui: Display-owned effects for navigation to the PC-followed activity.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_activity_cue(DeviceRuntime &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_activity_cue(ui.character.effects, voice);
}

// No parameters: read touch and dispatch it only to the retired visible route.
// Exception: Returns a mandatory input acquisition or route error.
// Pet delivery is optional for display operation; its failure remains in dashboard.pet.
esp_err_t sample_touch()
{
    auto &ui = *s_ui;
    LUNASHELL_touch_point_t sample{};
    const esp_err_t read = LUNASHELL_touch_read_point(&sample);
    if (read == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (read == ESP_ERR_INVALID_RESPONSE || read == ESP_ERR_TIMEOUT) {
        ui.state.pressed = false;
        ui.state.home_swipe_active = false;
        ui.state.qr_touch_active = false;
        invalidate();
        return ESP_OK;
    }
    if (read != ESP_OK) return read;
    const bool was_pressed = ui.state.pressed;
    const bool pressed_edge = sample.pressed && !was_pressed;
    ui.state.pressed = sample.pressed;
    // The contact can reveal chrome even while a new route or orientation
    // is awaiting its first frame. Action dispatch still uses the retired frame.
    if (sample.pressed || was_pressed) {
        const int64_t touch_at_us = esp_timer_get_time();
        ui.state.navigation.interact(touch_at_us);
        ui.state.screensaver_last_touch_us = touch_at_us;
        ui.state.idle_applied = false;
    }
    const CODY2D::Point point{static_cast<int16_t>(sample.x), static_cast<int16_t>(sample.y)};
    if (ui.character.event_cody_active) {
        if (pressed_edge && ui.state.retired.valid && ui.state.retired.layer == Layer::EventCody &&
            ui.state.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            const auto open_error = CODY::open_onboarding_notification(ui.character.event_cody_request);
            if (open_error == ESP_OK) {
                CODY::note_device_notification_opened(ui.character.effects, ui.character.event_cody_request);
                ui.state.mascot_home = false;
                ui.state.activity_page = ActivityPage::Events;
                ui.state.boot_section = BootSection::Events;
            } else {
                ESP_LOGW(kTag, "Notification cue open failed (%s)", esp_err_to_name(open_error));
            }
            close_event_cody(ui);
            LUNASHELL_power_manager_note_activity();
        }
        return ESP_OK;
    }
    if (ui.state.boot_menu_open) {
        if (!ui.state.retired.valid || ui.state.retired.layer != Layer::BootMenu ||
            ui.state.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.state.retired.content_revision != ui.state.boot_menu.revision) {
            ui.state.boot_menu_contact = CODY::BootMenuAction::None;
            return ESP_OK;
        }
        if (pressed_edge) {
            ui.state.boot_menu_start = ui.state.boot_menu_last = point;
            ui.state.boot_menu_revision = ui.state.retired.display_revision;
            ui.state.boot_menu_contact = CODY::boot_menu_action_at(ui.state.boot_menu, point);
            LUNASHELL_power_manager_note_activity();
        }
        if (sample.pressed) { ui.state.boot_menu_last = point; return ESP_OK; }
        if (!was_pressed || ui.state.boot_menu_revision != ui.state.retired.display_revision) return ESP_OK;
        const int dx = ui.state.boot_menu_last.x - ui.state.boot_menu_start.x;
        const int dy = ui.state.boot_menu_last.y - ui.state.boot_menu_start.y;
        const bool back = ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(ui.state.boot_menu_start) == CODY::LateralNavigation::Previous &&
            CODY::lateral_navigation_at(ui.state.boot_menu_last) == CODY::LateralNavigation::Previous;
        if (back || (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3)) {
            ui.state.boot_menu_open = false;
            ui.state.boot_menu_contact = CODY::BootMenuAction::None;
            invalidate();
            return ESP_OK;
        }
        const auto action = ui.state.boot_menu_contact;
        ui.state.boot_menu_contact = CODY::BootMenuAction::None;
        if (action == CODY::BootMenuAction::None || CODY::boot_menu_action_at(ui.state.boot_menu, ui.state.boot_menu_last) != action) return ESP_OK;
        if (action == CODY::BootMenuAction::Settings) {
            ui.state.boot_menu_open = false;
            s_options_requested.store(true, std::memory_order_release);
            invalidate();
            return ESP_OK;
        }
        if (action == CODY::BootMenuAction::About) {
            ui.state.about_return_third_party = ui.state.third_party_open;
            ui.state.about_return_prices = ui.state.prices_open;
            ui.state.about_return_settings = ui.state.scene.valid && ui.state.scene.definition->id == CODY::SceneId::MakeItYours;
            if (ui.state.about_return_settings) ui.state.about_return_settings_page = CODY::onboarding_options_page_token();
            ui.state.about_return_home = ui.state.mascot_home;
            ui.state.about_return_section = ui.state.boot_section;
            ui.state.about_return_activity = ui.state.activity_page;
            ui.state.about_return_screen = ui.state.widgets.selected_screen;
            ui.state.about_return_info = ui.state.info_page;
        }
        if (!CODY::onboarding_dashboard_active()) {
            const auto error = CODY::open_device_dashboard(ui.state.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) return error;
        }
        ui.state.boot_menu_open = false;
        ui.state.third_party_open = false;
        ui.state.prices_open = false;
        ui.state.audio_open = false;
        ui.state.diagnostics = false;
        ui.state.surprise_page = 0;
        ui.state.about_from_options = false;
        ui.state.mascot_home = false;
        ui.state.codex_browser.route = CODY::CodexBrowserRoute::Closed;
        ui.state.browser_destination = CODY::CodexBrowserDestination::None;
        if (action == CODY::BootMenuAction::About) {
            ui.state.about_from_boot_menu = true;
            ui.state.about_from_options = true;
            ui.state.activity_page = ActivityPage::Statistics;
            ui.state.statistics_entry = false;
            ui.state.widgets.selected_screen = WidgetScreen::About;
            ui.state.info_page = CODY::InfoPage::System;
        } else if (action == CODY::BootMenuAction::Events) {
            ui.state.boot_section = BootSection::Events;
            ui.state.activity_page = ActivityPage::Events;
        } else {
            ui.state.boot_section = BootSection::Counters;
            ui.state.counter_page = 0;
            ui.state.activity_page = ActivityPage::Statistics;
        }
        invalidate();
        return ESP_OK;
    }
    if (ui.state.prices_open) {
        if (!ui.state.retired.valid || ui.state.retired.layer != Layer::Prices ||
            ui.state.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.state.retired.content_revision != ui.state.prices.revision) {
            ui.state.prices_touch_row = -1;
            return ESP_OK;
        }
        if (pressed_edge) {
            ui.state.prices_start = ui.state.prices_last = point;
            ui.state.prices_touch_revision = ui.state.retired.display_revision;
            ui.state.prices_touch_row = -1;
            if (!ui.state.prices.detail) {
                for (size_t i = 0; i < ui.state.prices.row_count; ++i)
                    if (CODY2D::capsule_contains(ui.state.prices.rows[i], point)) ui.state.prices_touch_row = static_cast<int8_t>(i);
            }
            LUNASHELL_power_manager_note_activity();
        }
        if (sample.pressed) { ui.state.prices_last = point; return ESP_OK; }
        if (!was_pressed || ui.state.prices_touch_revision != ui.state.retired.display_revision) return ESP_OK;
        const int dx = ui.state.prices_last.x - ui.state.prices_start.x;
        const int dy = ui.state.prices_last.y - ui.state.prices_start.y;
        const bool back = ui.state.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(ui.state.prices_start) == CODY::LateralNavigation::Previous &&
            CODY::lateral_navigation_at(ui.state.prices_last) == CODY::LateralNavigation::Previous;
        if (back || (dx >= 48 && dx * 2 > std::abs(dy) * 3 && ui.state.prices.detail)) {
            if (!CODY::prices_back(ui.state.prices)) ui.state.prices_open = false;
            invalidate();
        } else if (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3) {
            if (CODY::prices_move_page(ui.state.prices, dx < 0 ? 1 : -1)) invalidate();
        } else if (ui.state.prices_touch_row >= 0 &&
            CODY2D::capsule_contains(ui.state.prices.rows[static_cast<size_t>(ui.state.prices_touch_row)], ui.state.prices_last)) {
            if (CODY::prices_select_at(ui.state.prices, ui.state.prices_last)) invalidate();
        }
        ui.state.prices_touch_row = -1;
        return ESP_OK;
    }
    if (ui.character.usage_alarm_active) {
        if (pressed_edge && ui.state.retired.valid && ui.state.retired.layer == Layer::UsageAlarm &&
            ui.state.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            close_usage_alarm(ui);
            LUNASHELL_power_manager_note_activity();
        }
        return ESP_OK;
    }
    if (ui.state.surprise_page != 0) {
        if (pressed_edge && ui.state.retired.valid && ui.state.retired.layer == Layer::Surprise &&
            ui.state.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            LUNASHELL_power_manager_note_activity();
            if (ui.state.surprise_page < 3) ++ui.state.surprise_page;
            else ui.state.surprise_page = 0;
            invalidate();
        }
        return ESP_OK;
    }
    if (pressed_edge && ui.state.retired.valid && ui.state.retired.layer != Layer::Screensaver && point.y >= 0 && point.y < 73) {
        ui.state.home_swipe_active = true;
        ui.state.home_swipe_valid = true;
        ui.state.home_swipe_start = ui.state.home_swipe_last = point;
        ui.state.home_swipe_revision = ui.state.retired.display_revision;
        ui.state.home_swipe_rotation = ui.model.motion.rendering_rotation_degrees;
        ui.state.contact = Target::None;
        CODY::cancel_onboarding_touch();
    }
    if (ui.state.home_swipe_active) {
        if (!ui.state.retired.valid || ui.state.retired.display_revision != ui.state.home_swipe_revision ||
            ui.state.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.state.home_swipe_rotation != ui.model.motion.rendering_rotation_degrees) ui.state.home_swipe_valid = false;
        if (sample.pressed) ui.state.home_swipe_last = point;
        else {
            ui.state.home_swipe_active = false;
            const int dx = ui.state.home_swipe_last.x - ui.state.home_swipe_start.x;
            const int dy = ui.state.home_swipe_last.y - ui.state.home_swipe_start.y;
            if (ui.state.home_swipe_valid && dy >= 60 && dy * 2 > std::abs(dx) * 3) {
                LUNASHELL_power_manager_note_activity();
                queue_home();
            } else if (ui.state.home_swipe_valid && dx * dx + dy * dy <= 12 * 12 &&
                (ui.state.retired.layer == Layer::AboutLink ||
                 (ui.state.retired.layer == Layer::Scene && CODY::onboarding_qr_visible()))) {
                ui.state.qr_reveal_at_us = esp_timer_get_time();
            }
        }
        return ESP_OK;
    }
    if (pressed_edge && ui.character.animation_active) {
        ui.character.animation_active = false;
        CODY::reset_muse_animation(0);
        ui.state.contact = Target::None;
        LUNASHELL_power_manager_note_activity();
        invalidate();
        return ESP_OK;
    }
    if (!ui.state.retired.valid || ui.state.retired.display_revision != LUNASHELL_display_runtime_content_revision()) {
        ui.state.contact = Target::None;
        ui.state.qr_touch_active = false;
        ui.character.pixel_demo.touch_candidate = false;
        return ESP_OK;
    }
    if (pressed_edge) {
        LUNASHELL_power_manager_note_activity();
        if (ui.state.retired.layer != Layer::Mascot) play_touch_cue(ui);
    }
    if (ui.state.screensaver.phase == CODY::ScreensaverPhase::Visible || ui.state.retired.layer == Layer::Screensaver) {
        CODY::dismiss_screensaver(ui.state.screensaver, esp_timer_get_time());
        // The wake touch owns no underlying target, including its eventual release.
        ui.state.contact = Target::None;
        invalidate();
        return ESP_OK;
    }
    const bool qr_touch = ui.state.retired.layer == Layer::AboutLink ||
        (ui.state.retired.layer == Layer::Scene && CODY::onboarding_qr_visible());
    if (qr_touch) {
        if (pressed_edge) {
            ui.state.qr_touch_active = true;
            ui.state.qr_touch_start = ui.state.qr_touch_last = point;
            ui.state.qr_touch_revision = ui.state.retired.display_revision;
            ui.state.qr_touch_rotation = ui.model.motion.rendering_rotation_degrees;
            ui.state.qr_touch_scene = ui.state.retired.layer == Layer::Scene ? ui.state.scene.definition : nullptr;
        }
        if (ui.state.qr_touch_active && sample.pressed) ui.state.qr_touch_last = point;
        if (ui.state.qr_touch_active && (!ui.state.retired.valid || ui.state.qr_touch_revision != ui.state.retired.display_revision ||
            ui.state.qr_touch_rotation != ui.model.motion.rendering_rotation_degrees ||
            (ui.state.retired.layer == Layer::Scene && ui.state.qr_touch_scene != ui.state.scene.definition))) {
            ui.state.qr_touch_active = false;
            CODY::cancel_onboarding_touch();
        }
        if (!sample.pressed && ui.state.qr_touch_active) {
            ui.state.qr_touch_active = false;
            const int dx = ui.state.qr_touch_last.x - ui.state.qr_touch_start.x;
            const int dy = ui.state.qr_touch_last.y - ui.state.qr_touch_start.y;
            if (dx * dx + dy * dy <= 12 * 12) ui.state.qr_reveal_at_us = esp_timer_get_time();
            if (ui.state.retired.layer == Layer::AboutLink) {
                return dx >= 48 && dx * 2 > std::abs(dy) * 3 ? activate(Target::Previous) : ESP_OK;
            }
        }
        if (ui.state.retired.layer == Layer::AboutLink) return ESP_OK;
        // The scene owns the right-swipe and closes any pairing session before leaving.
    }
    if (ui.state.retired.layer == Layer::Mascot) {
        if (CODY::observe_pixel_demo_touch(ui.character.pixel_demo, point, sample.pressed, pressed_edge,
                sample.sample_timestamp_us, ui.state.retired.display_revision)) {
            const auto pet_error = CODY::record_pet_tap(sample.sample_timestamp_us, ui.model.dashboard.pet);
            if (pet_error != ESP_OK) {
                // The owner logs and publishes rejection. Do not acknowledge it
                // with a reaction or stop the unrelated display worker.
                return ESP_OK;
            }
            CODY::start_pixel_demo_reaction(ui.character.pixel_demo);
            ui.character.pet_tap_cue_pending = true;
        }
        return ESP_OK;
    }
    if (ui.state.retired.layer == Layer::Scene) {
        if (ui.state.retired.navigation_opacity != 255 && CODY::onboarding_lateral_control_at(point)) {
            CODY::cancel_onboarding_touch();
            return ESP_OK;
        }
        bool consumed = false;
        const auto *previous_definition = ui.state.scene.definition;
        const auto previous_content = ui.state.scene.content.revision;
        const auto previous_controls = ui.state.scene.content.control_revision;
        const auto previous_text = ui.state.scene.content.text_generation;
        const auto previous_visible = ui.state.scene.visible_characters;
        const auto previous_body = ui.state.scene.content.visible_characters;
        auto error = CODY::observe_onboarding_touch_activity(point, sample.pressed, pressed_edge, ui.state.awake_us);
        if (error == ESP_OK) {
            error = CODY::handle_onboarding_touch(ui.state.scene, point, sample.pressed, pressed_edge,
                ui.model.motion.rendering_rotation_degrees, consumed);
        }
        // Consumed means the route owns the touch, not that pixels changed.
        // A held settings row must not force a full redraw on every touch report.
        ui.state.scene_dirty = ui.state.scene_dirty || ui.state.scene.definition != previous_definition ||
            ui.state.scene.content.revision != previous_content || ui.state.scene.content.control_revision != previous_controls ||
            ui.state.scene.content.text_generation != previous_text || ui.state.scene.visible_characters != previous_visible ||
            ui.state.scene.content.visible_characters != previous_body;
        return error;
    }
    if (sample.pressed) {
        const auto target = target_at(point);
        if (pressed_edge) {
            ui.state.contact = target;
            ui.state.activity_touch_start = point;
            ui.state.touched_event = {};
            if (target == Target::EventsGesture &&
                ui.state.codex_panel.revision == ui.state.retired.content_revision) {
                const int row = CODY::codex_event_row_at(point);
                if (row >= 0 && row < ui.state.codex_panel.page.count) {
                    ui.state.touched_event = ui.state.codex_panel.page.rows[row].key;
                }
            }
        }
        ui.state.activity_touch_last = point;
        if (ui.state.retired.layer == Layer::AboutLink && ui.state.contact == Target::Previous) {
            const int dx = point.x - ui.state.activity_touch_start.x;
            const int dy = point.y - ui.state.activity_touch_start.y;
            if (dx * dx + dy * dy > 12 * 12) ui.state.contact = Target::None;
        }
        if (ui.state.contact == Target::VoucherScroll) {
            if (pressed_edge) ui.state.voucher_touch_start = point;
            ui.state.voucher_touch_last = point;
        } else if (ui.state.contact != target) {
            ui.state.contact = Target::None;
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        if (pressed_edge && target == Target::VoiceGesture) {
            ui.state.voice_touch_start = point;
            ui.state.voice_touch_started_us = esp_timer_get_time();
        } else if (ui.state.contact == Target::VoiceGesture &&
            (std::abs(point.x - ui.state.voice_touch_start.x) > 16 ||
             std::abs(point.y - ui.state.voice_touch_start.y) > 16)) {
            ui.state.contact = Target::None;
            ui.state.voice_tapped_us = 0;
        }
#endif
        return ESP_OK;
    }
    const auto contact = ui.state.contact;
    ui.state.contact = Target::None;
    if (was_pressed) {
        const int dx = ui.state.activity_touch_last.x - ui.state.activity_touch_start.x;
        const int dy = ui.state.activity_touch_last.y - ui.state.activity_touch_start.y;
        if (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3) {
            const bool previous = dx > 0;
            if (ui.state.retired.layer == Layer::Counter) {
                for (uint8_t visited = 0; visited < 3; ++visited) {
                    ui.state.counter_page = static_cast<uint8_t>((ui.state.counter_page + (previous ? 2U : 1U)) % 3U);
                    if ((ui.state.counter_mask & (1U << ui.state.counter_page)) != 0) break;
                }
                invalidate();
                return ESP_OK;
            }
            if (ui.state.retired.layer == Layer::CodexBrowser) {
                if (previous && !ui.state.codex_browser.page.previous) return activate(Target::CodexBrowserBack);
                const auto action = previous ? CODY::CodexBrowserAction::PreviousPage : CODY::CodexBrowserAction::NextPage;
                (void)CODY::activate_codex_browser(ui.state.codex_browser, {action});
                invalidate();
                return ESP_OK;
            }
            if (ui.state.retired.layer == Layer::Dashboard && contact != Target::VoucherScroll) {
                const auto first = CODY::first_visible_widget(ui.state.available_statistics, CODY::WidgetScope::Statistics)
                    .value_or(WidgetScreen::Device);
                if (previous && ui.state.browser_destination != CODY::CodexBrowserDestination::None &&
                    ui.state.retired.screen == first) return activate(Target::CodexBrowserBack);
                if (previous && ui.state.retired.screen == first && !ui.state.about_from_options &&
                    (ui.state.local_notifications.total != 0 || ui.state.current_turn_available ||
                     (ui.model.inbox.data_valid && !ui.model.inbox.data_stale))) {
                    ui.state.activity_page = ActivityPage::Events;
                    invalidate();
                    return ESP_OK;
                }
                return activate(previous ? Target::Previous : Target::Next);
            }
            if (previous && ui.state.retired.layer == Layer::AboutLink)
                return activate(Target::Previous);
            if (previous && ui.state.retired.layer == Layer::Audio) return activate(Target::AudioClose);
            if (previous && ui.state.retired.layer == Layer::ThirdParty) return activate(Target::ThirdPartyBack);
            if (previous && ui.state.retired.layer == Layer::CurrentTurn) {
                return activate(ui.state.retired.layer == Layer::CurrentTurn &&
                    ui.state.browser_destination == CODY::CodexBrowserDestination::CurrentTurn
                    ? Target::CodexBrowserBack : Target::ActivityBack);
            }
        }
    }
    if (was_pressed && ui.state.retired.layer == Layer::Dashboard &&
        (contact == Target::None || contact == Target::VoiceGesture)) {
        const int dx = ui.state.activity_touch_last.x - ui.state.activity_touch_start.x;
        const int dy = ui.state.activity_touch_last.y - ui.state.activity_touch_start.y;
        if (dx >= 44 && dx > std::abs(dy) && (ui.state.local_notifications.total != 0 || ui.state.current_turn_available ||
            (ui.model.inbox.data_valid && !ui.model.inbox.data_stale))) {
            ui.state.activity_page = ActivityPage::Events;
            invalidate();
            return ESP_OK;
        }
    }
    return contact != Target::None ? activate(contact) : ESP_OK;
}


// No parameters: apply settings outcomes and advance the canonical setup/event route.
// Exception: Returns the first route or snapshot failure.
esp_err_t update_navigation()
{
    auto &ui = *s_ui;
    bool audio_refresh = false;
    CODY::PreferenceId audio_preference = CODY::PreferenceId::PlaybackVolume;
    esp_err_t audio_result = ESP_OK;
    if (CODY::take_audio_preference_result(audio_refresh, audio_preference, audio_result)) {
        ui.state.audio_setting_pending = false;
        // A reconciliation returns its first error, so a permanent failure on one
        // control must not hide a transient busy result on the other control.
        LUNASHELL::CodyOptionsObservation audio_observed{};
        const auto observation_error = DeviceSnapshotService::instance().readCodyOptionsObservation(audio_observed);
        const bool preference_busy = observation_error == ESP_OK &&
            (audio_observed.preferences.playback_volume_error == ESP_ERR_NOT_FINISHED ||
             audio_observed.preferences.microphone_gain_error == ESP_ERR_NOT_FINISHED);
        ui.state.audio_setting_error = audio_result == ESP_ERR_NOT_FINISHED ? ESP_OK : audio_result;
        if (audio_result == ESP_ERR_NOT_FINISHED || preference_busy) {
            ui.state.audio_refresh_needed = true;
            ui.state.audio_refresh_retry_us = esp_timer_get_time() + 2000000;
        }
        if (ui.state.audio_open) invalidate();
    }
    if (ui.state.audio_refresh_needed && !ui.state.audio_setting_pending && !ui.state.asleep &&
        !s_options_requested.load(std::memory_order_acquire) &&
        !s_home_requested.load(std::memory_order_acquire) &&
        !s_boot_menu_requested.load(std::memory_order_acquire) &&
        !s_surprise_requested.load(std::memory_order_acquire) &&
        esp_timer_get_time() >= ui.state.audio_refresh_retry_us) {
        const auto error = CODY::request_audio_preferences_refresh();
        if (error == ESP_OK) {
            ui.state.audio_refresh_needed = false;
            ui.state.audio_setting_pending = true;
            ui.state.audio_setting_error = ESP_OK;
            if (ui.state.audio_open) invalidate();
        } else if (error == ESP_ERR_NOT_FINISHED) {
            ui.state.audio_refresh_retry_us = esp_timer_get_time() + 2000000;
        } else {
            ui.state.audio_refresh_needed = false;
            ui.state.audio_setting_error = error;
            ESP_LOGW(kTag, "Audio preferences refresh admission failed (%s)", esp_err_to_name(error));
        }
    }
    if (s_boot_activity.exchange(false, std::memory_order_acq_rel)) {
        const int64_t button_at_us = esp_timer_get_time();
        ui.state.navigation.interact(button_at_us);
        ui.state.screensaver_last_touch_us = button_at_us;
        ui.state.idle_applied = false;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (ui.state.voice_boot_route_hold) {
        const bool active = LUNASHELL::voice_input_phase_active(ui.model.voice.input.phase);
        if (active) ui.state.voice_boot_phase_seen = true;
        const auto requested_at = s_boot_voice_requested_at_us.load(std::memory_order_acquire);
        if (!active && (ui.state.voice_boot_phase_seen || requested_at <= 0 ||
            esp_timer_get_time() - requested_at > 15000000)) {
            ui.state.voice_boot_route_hold = false;
            ui.state.voice_boot_phase_seen = false;
        }
    }
    if (s_boot_voice_requested.load(std::memory_order_acquire)) {
        const int64_t voice_requested_at = s_boot_voice_requested_at_us.load(std::memory_order_acquire);
        const int64_t voice_now = esp_timer_get_time();
        if (voice_requested_at <= 0 || voice_now < voice_requested_at || voice_now - voice_requested_at > 15000000) {
            s_boot_voice_requested.store(false, std::memory_order_release);
            ui.state.voice_boot_error_until_us = voice_now + 3000000;
            invalidate();
        } else if (LUNASHELL::voice_input_phase_active(ui.model.voice.input.phase)) {
            // A running recording retains its own Send and Cancel controls.
            s_boot_voice_requested.store(false, std::memory_order_release);
            ESP_LOGI(kTag, "BOOT voice request ignored while recording is active");
        } else if (!voice_route_available(ui) ||
                   (ui.state.retired.layer != Layer::Dashboard && ui.state.retired.layer != Layer::Events &&
                    ui.state.retired.layer != Layer::CurrentTurn)) {
            // A voice request needs a completely retired dashboard before it can
            // freeze the destination and admit the microphone.
            const auto error = CODY::onboarding_dashboard_active() ? ESP_OK : CODY::open_device_dashboard(ui.state.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) {
                s_boot_voice_requested.store(false, std::memory_order_release);
                ui.state.voice_boot_error_until_us = voice_now + 3000000;
                invalidate();
                ESP_LOGW(kTag, "BOOT voice route unavailable (%s)", esp_err_to_name(error));
                return ESP_OK;
            }
            s_options_requested.store(false, std::memory_order_release);
            s_home_requested.store(false, std::memory_order_release);
            s_boot_menu_requested.store(false, std::memory_order_release);
            s_surprise_requested.store(false, std::memory_order_release);
            ui.state.boot_menu_open = false;
            ui.state.third_party_open = false;
            ui.state.prices_open = false;
            ui.character.animation_active = false;
            if (ui.character.usage_alarm_active) close_usage_alarm(ui);
            if (ui.character.event_cody_active) close_event_cody(ui);
            ui.state.audio_open = false;
            ui.state.diagnostics = false;
            ui.state.surprise_page = 0;
            ui.state.mascot_home = false;
            ui.state.activity_page = ActivityPage::Statistics;
            // A retained About QR page does not admit voice; leave that page
            // before waiting for the dashboard to retire its next frame.
            ui.state.about_from_options = false;
            ui.state.about_from_boot_menu = false;
            ui.state.info_page = CODY::InfoPage::System;
            CODY::dismiss_screensaver(ui.state.screensaver, esp_timer_get_time());
            invalidate();
            return ESP_OK;
        } else if (ui.state.retired.layer == Layer::Dashboard || ui.state.retired.layer == Layer::Events ||
                   ui.state.retired.layer == Layer::CurrentTurn) {
            LUNASHELL::VoiceInputAdmissionObservation admission{};
            const auto admission_error = DeviceSnapshotService::instance().readVoiceInputAdmissionObservation(admission);
            const auto &voice = ui.model.voice;
            if (admission_error == ESP_OK && admission.dictation_valid && admission.dictation_error == ESP_OK &&
                admission.dictation_enabled && (!admission.lan_ready || !voice.data_valid || voice.data_stale ||
                voice.last_error != ESP_OK || voice.state != LUNASHELL::VoiceState::Listening)) {
                // Standby wake may resume the screen before the LAN and AFE owners.
                return ESP_OK;
            }
            s_boot_voice_requested.store(false, std::memory_order_release);
            bool started = false;
            const auto error = LUNASHELL::requestVoiceInput(LUNASHELL::VoiceInputTrigger::DeviceButton, &started);
            if (error != ESP_OK || !started) {
                ui.state.voice_boot_error_until_us = voice_now + 3000000;
                invalidate();
                ESP_LOGW(kTag, "BOOT voice request unavailable: started=%u (%s)",
                    unsigned(started), esp_err_to_name(error));
            } else {
                ui.state.voice_boot_route_hold = true;
                ui.state.voice_boot_phase_seen = false;
            }
        }
    }
#endif
    bool can_open_options = !CODY::onboarding_restart_pending();
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (can_open_options && (s_options_requested.load(std::memory_order_acquire) ||
        s_home_requested.load(std::memory_order_acquire) ||
        s_boot_menu_requested.load(std::memory_order_acquire) ||
        s_surprise_requested.load(std::memory_order_acquire))) {
        // An ongoing recording retains its Cancel/Send controls before BOOT opens Options.
        can_open_options = !LUNASHELL::setVoiceInputRouteAvailable(false);
    }
#endif
    if (can_open_options && !s_home_requested.load(std::memory_order_acquire) &&
        !s_surprise_requested.load(std::memory_order_acquire) &&
        s_boot_menu_requested.exchange(false, std::memory_order_acq_rel)) {
        if (ui.character.usage_alarm_active) close_usage_alarm(ui);
        if (ui.character.event_cody_active) close_event_cody(ui);
        ui.state.boot_menu_open = true;
        ui.state.boot_menu_contact = CODY::BootMenuAction::None;
        CODY::dismiss_screensaver(ui.state.screensaver, esp_timer_get_time());
        invalidate();
    }
    if (can_open_options && (s_home_requested.load(std::memory_order_acquire) ||
        s_surprise_requested.load(std::memory_order_acquire))) {
        // Claim the gesture independently so a concurrent third press cannot be lost.
        const bool surprise = s_surprise_requested.exchange(false, std::memory_order_acq_rel);
        const auto error = CODY::open_device_home(ui.state.scene);
        if (error == ESP_ERR_NOT_FINISHED && surprise) {
            s_surprise_requested.store(true, std::memory_order_release);
        }
        if (error != ESP_ERR_NOT_FINISHED) {
            s_home_requested.store(false, std::memory_order_release);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "BOOT Home request could not close current route (%s)", esp_err_to_name(error));
                return ESP_OK;
            }
            s_options_requested.store(false, std::memory_order_release);
            s_boot_menu_requested.store(false, std::memory_order_release);
            ui.state.boot_menu_open = false;
            ui.state.third_party_open = false;
            ui.state.prices_open = false;
            ui.state.boot_section = BootSection::Default;
            if (ui.character.event_cody_active) close_event_cody(ui);
            ui.state.surprise_page = surprise ? 1 : 0;
            ui.state.home_swipe_active = false;
            ui.state.qr_touch_active = false;
            ui.state.audio_open = false;
            ui.state.diagnostics = false;
            ui.model.vouchers.warning_expiry = 0;
            CODY::dismiss_screensaver(ui.state.screensaver, esp_timer_get_time());
            ui.state.codex_browser.route = CODY::CodexBrowserRoute::Closed;
            ui.state.browser_destination = CODY::CodexBrowserDestination::None;
            ui.state.mascot_home = true;
            ui.state.about_from_options = false;
            ui.state.about_from_boot_menu = false;
            ui.state.options_return_valid = false;
            invalidate();
        }
    }
    if (can_open_options && !s_home_requested.load(std::memory_order_acquire) &&
        !s_surprise_requested.load(std::memory_order_acquire) &&
        s_options_requested.load(std::memory_order_acquire)) {
        const int64_t button_at_us = esp_timer_get_time();
        const bool in_options = ui.state.scene.valid && ui.state.scene.definition->id == CODY::SceneId::MakeItYours;
        const bool returning_to_options = in_options || ui.state.about_from_options ||
            ui.state.audio_open || ui.state.codex_browser.route != CODY::CodexBrowserRoute::Closed;
        const bool return_home = ui.state.mascot_home;
        const auto return_activity = ui.state.activity_page;
        const auto return_screen = ui.state.widgets.selected_screen;
        const auto return_info = ui.state.info_page;
        const esp_err_t error = returning_to_options ? CODY::return_to_options_tools(ui.state.scene) :
            CODY::open_companion_options(ui.state.scene);
        if (error != ESP_ERR_NOT_FINISHED) {
            s_options_requested.store(false, std::memory_order_release);
            if (error != ESP_OK) return error;
            ui.state.boot_menu_open = false;
            ui.state.third_party_open = false;
            ui.state.prices_open = false;
            if (ui.character.event_cody_active) close_event_cody(ui);
            ui.state.surprise_page = 0;
            if (!returning_to_options) {
                ui.state.options_return_valid = true;
                ui.state.options_return_home = return_home;
                ui.state.options_return_activity = return_activity;
                ui.state.options_return_screen = return_screen;
                ui.state.options_return_info = return_info;
            }
            ui.state.audio_open = false;
            ui.model.vouchers.warning_expiry = 0;
            CODY::dismiss_screensaver(ui.state.screensaver, button_at_us);
            ui.state.diagnostics = false;
            ui.state.codex_browser.route = CODY::CodexBrowserRoute::Closed;
            ui.state.browser_destination = CODY::CodexBrowserDestination::None;
            ui.state.mascot_home = false;
            ui.state.about_from_options = false;
            ui.state.about_from_boot_menu = false;
            if (!in_options && ui.state.scene.valid && ui.state.scene.definition->id == CODY::SceneId::MakeItYours) {
                ui.character.effects.feedback = CODY::DeviceFeedback::OptionsOpened;
            }
            invalidate();
        }
    }
    const auto request = CODY::take_widget_open_request();
    switch (request) {
    case CODY::WidgetOpenRequest::Prices:
        ui.state.prices_open = true;
        invalidate();
        break;
    case CODY::WidgetOpenRequest::ThirdParty:
        ui.state.third_party_open = true;
        invalidate();
        break;
    case CODY::WidgetOpenRequest::About: {
        const auto error = CODY::open_device_dashboard(ui.state.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.state.mascot_home = false;
        ui.state.about_from_options = true;
        ui.state.about_from_boot_menu = false;
        ui.state.activity_page = ActivityPage::Statistics;
        ui.state.statistics_entry = false;
        ui.state.widgets.selected_screen = WidgetScreen::About;
        ui.state.info_page = CODY::InfoPage::System;
        invalidate();
        break;
    }
    case CODY::WidgetOpenRequest::CodexBrowser:
        ui.state.audio_open = false;
        if (ui.settings.counter_mode_enabled) break;
        if (CODY::onboarding_codex_tools_active()) {
            CODY::open_codex_browser(ui.state.codex_browser);
            ui.state.browser_destination = CODY::CodexBrowserDestination::None;
            invalidate();
        }
        break;
    case CODY::WidgetOpenRequest::ReturnFromOptions: {
        const auto error = CODY::open_device_dashboard(ui.state.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.state.mascot_home = !ui.state.options_return_valid || ui.state.options_return_home;
        if (ui.state.options_return_valid) {
            ui.state.activity_page = ui.state.options_return_activity;
            ui.state.widgets.selected_screen = ui.state.options_return_screen;
            ui.state.info_page = ui.state.options_return_info;
            ui.state.statistics_entry = false;
        }
        ui.state.options_return_valid = false;
        ui.state.about_from_options = false;
        invalidate();
        break;
    }
    case CODY::WidgetOpenRequest::Home:
    case CODY::WidgetOpenRequest::Statistics: {
        ui.state.audio_open = false;
        const auto error = CODY::open_device_dashboard(ui.state.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.state.codex_browser.route = CODY::CodexBrowserRoute::Closed;
        ui.state.browser_destination = CODY::CodexBrowserDestination::None;
        ui.state.mascot_home = request == CODY::WidgetOpenRequest::Home;
        // Returning from Connections must retire the explicit Counter section.
        // The user's canonical counter-mode preference still selects their Home.
        ui.state.boot_section = BootSection::Default;
        if (request == CODY::WidgetOpenRequest::Statistics) {
            select_statistics();
        } else {
            invalidate();
        }
        break;
    }
    case CODY::WidgetOpenRequest::Audio:
        ui.state.audio_open = true;
        if (ui.state.audio_setting_error != ESP_OK && !ui.state.audio_setting_pending) {
            ui.state.audio_setting_error = ESP_OK;
            ui.state.audio_refresh_needed = true;
            ui.state.audio_refresh_retry_us = 0;
        }
        invalidate();
        break;
    default: break;
    }
    const auto inbox_error = DeviceSnapshotService::instance().readCodexInboxObservation(ui.model.inbox);
    if (inbox_error != ESP_OK) {
        ui.model.inbox.data_valid = false;
        ui.model.inbox.last_error = inbox_error;
    }
    CODY::set_onboarding_inbox_available(ui.model.inbox.last_success_at_us > 0);
    const auto local_error = CODY::read_notifications(ui.state.notification_offset, ui.state.local_notifications);
    if (local_error != ESP_OK && local_error != ESP_ERR_NOT_SUPPORTED)
        ESP_LOGW(kTag, "Local notifications unavailable (%s)", esp_err_to_name(local_error));
    bool changed = false;
    const auto error = CODY::update_onboarding(ui.state.scene, ui.state.completed_scene, ui.state.awake_us, ui.state.awake_us, changed,
        ui.state.retired.valid && ui.state.retired.layer == Layer::Scene);
    if (CODY::take_preference_confirmation()) play_preference_cue(ui);
    if (changed) {
        CODY::DamageRect startup_animation_bounds{};
        // A retired startup title/footer stays intact; render consumes the new content revision in the avatar rectangle.
        if (!CODY::onboarding_startup_animation_damage(startup_animation_bounds)) ui.state.scene_dirty = true;
        if (ui.state.audio_open && (!ui.state.scene.valid || ui.state.scene.definition->id != CODY::SceneId::MakeItYours)) {
            ui.state.audio_open = false;
            invalidate();
        }
    }
    return error;
}

// No parameters: apply the current setup timeout through the existing power owner.
esp_err_t apply_standby_override()
{
    auto &ui = *s_ui;
    const bool scene_visible = CODY::onboarding_startup_active() || !ui.state.mascot_home ||
        (ui.state.retired.valid && ui.state.retired.layer == Layer::Scene);
    const uint32_t seconds = !ui.state.asleep && scene_visible && ui.state.scene.valid && !CODY::onboarding_dashboard_active()
        ? ui.state.scene.definition->standby_override_seconds : 0;
    if (seconds == ui.state.standby_override) return ESP_OK;
    const auto error = LUNASHELL_power_manager_set_scene_standby_override(seconds);
    if (error == ESP_OK) ui.state.standby_override = seconds;
    return error;
}

// command: Latest startup or power-owner handoff.
// Exception: Acknowledges success only after the requested panel frame retires.
esp_err_t apply_command(Command command)
{
    auto &ui = *s_ui;
    invalidate();
    ui.state.last_active_us = 0;
    ui.state.pressed = false;
    const int64_t now_us = esp_timer_get_time();
    ui.state.screensaver_last_touch_us = now_us;
    ui.state.idle_eligible_since_us = -1;
    ui.state.idle_applied = false;
    synchronize_character_theme(ui);
    CODY::dismiss_screensaver(ui.state.screensaver, now_us);
    CODY::suspend_companion_shake(ui.state.shake, now_us);
    if (command == Command::Ready || command == Command::Wake) ui.state.navigation.interact(now_us);
    FrameView frame{};
    if (command == Command::Ready) {
        CODY::SettingsModel settings{};
        auto error = CODY::read_settings_model(settings);
        if (error != ESP_OK) return error;
        error = CODY::start_device_setup(ui.state.scene, settings.onboarding.effective_first_boot,
            settings.onboarding.onboarding_tour_step);
        if (error != ESP_OK) return error;
        select_statistics();
        ui.state.statistics_entry = false;
        ui.state.activity_page = ActivityPage::Events;
        ui.state.mascot_home = settings.onboarding.onboarding_tour_step == static_cast<uint8_t>(CODY::OnboardingTourStep::Completed);
        ui.character.pixel_demo.showcase.reset(static_cast<uint32_t>(now_us));
        ui.state.audio_refresh_needed = true;
        ui.state.audio_refresh_retry_us = 0;
    }
    if (command == Command::Sleep || command == Command::Shutdown) {
        if (ui.character.usage_alarm_active) close_usage_alarm(ui);
        if (ui.character.event_cody_active) close_event_cody(ui);
        if (ui.character.animation_active) CODY::reset_muse_animation(0);
        ui.character.animation_active = false;
        set_audio_demand(false);
        ui.character.effects.event_sound = {};
        const auto audio = CODY::set_codex_audio_suspended(true);
        if (audio != ESP_OK) ESP_LOGW(kTag, "Audio suspension status (%s)", esp_err_to_name(audio));
        esp_err_t error = ESP_OK;
        if (command == Command::Shutdown) {
            frame.layer = Layer::Message;
            error = CODY::prepare_device_message(ui.state.view, frame.presentation, "Device", "Powering off");
        }
        if (error == ESP_OK) error = render(frame, true, RenderAdmission::LifecycleHandoff);
        if (error == ESP_OK) ui.state.asleep = true;
        else (void)CODY::set_codex_audio_suspended(false);
        return error;
    }
    ui.state.asleep = false;
    const auto audio = CODY::set_codex_audio_suspended(false);
    if (audio != ESP_OK) ESP_LOGW(kTag, "Audio resume status (%s)", esp_err_to_name(audio));
    esp_err_t error = ESP_OK;
    if (command == Command::ShutdownFailed) {
        frame.layer = Layer::Message;
        error = prepare_failure_message(ui, frame, esp_timer_get_time(), "Cody needs help", "Power off failed", "Use the device menu");
    } else {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        // Ready has no sampled voice state yet; Wake may retain a pre-standby
        // session. Refresh before effects consume one-shot event identities.
        error = DeviceSnapshotService::instance().readVoiceObservation(ui.model.voice);
#endif
        if (error == ESP_OK) error = prepare_current_view(frame, esp_timer_get_time());
    }
    if (error == ESP_OK) error = render(frame, true, RenderAdmission::LifecycleHandoff);
    if (error == ESP_OK && command == Command::Wake) {
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        CODY::play_device_wake_cue(ui.character.effects, voice);
    }
    return error;
}

// command: Serialized request from the startup or power owner.
// Exception: Returns the original worker result or a bounded handoff timeout.
esp_err_t request_command(Command command)
{
    const auto fault = s_terminal_error.load(std::memory_order_acquire);
    if (fault != ESP_OK) return fault;
    const uint32_t ticket = (s_ticket.fetch_add(1, std::memory_order_relaxed) + 1U) << 8U;
    const uint32_t request = ticket | static_cast<uint32_t>(command);
    s_command.store(request, std::memory_order_release);
    const TickType_t started = xTaskGetTickCount();
    const uint32_t timeout_ms = command == Command::Ready ? kReadyTimeoutMs : kPowerTimeoutMs;
    while (s_ack.load(std::memory_order_acquire) != request) {
        const auto error = s_terminal_error.load(std::memory_order_acquire);
        if (error != ESP_OK) return error;
        if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(timeout_ms)) {
            uint32_t pending = request;
            s_command.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
            ESP_LOGE(kTag, "Display handoff timed out for command %u", static_cast<unsigned>(command));
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return s_command_result.load(std::memory_order_relaxed);
}

// sleeping: Target lifecycle of the panel owned by the power manager.
esp_err_t prepare_lifecycle(bool sleeping)
{
    if (!s_ready.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    return request_command(sleeping ? Command::Sleep : Command::Wake);
}

// No parameters: offline standby uses the same dark frame and hardware owner.
esp_err_t prepare_offline_standby()
{
    return prepare_lifecycle(true);
}

// now_us: One worker tick's monotonic sample time.
// Exception: Returns a required acquisition, route or raster failure.
esp_err_t update_and_draw(int64_t now_us)
{
    auto &ui = *s_ui;
    uint32_t whatsapp_sequence = 0;
    int64_t whatsapp_received_us = 0;
    const bool whatsapp_can_present = ui.state.retired.valid &&
        ui.state.retired.layer != Layer::Blank && ui.state.retired.layer != Layer::Branding &&
        ui.state.retired.layer != Layer::Birth && ui.state.retired.layer != Layer::Surprise &&
        ui.state.retired.layer != Layer::Screensaver && !CODY::onboarding_qr_visible() &&
        ui.state.birth_request.error == ESP_OK && ui.state.setup_request.error == ESP_OK &&
        ui.state.music_request.error == ESP_OK;
    if (CODY::read_whatsapp_message(whatsapp_sequence, whatsapp_received_us) == ESP_OK &&
        whatsapp_sequence != ui.state.whatsapp_seen && whatsapp_can_present) {
        ui.state.whatsapp_seen = whatsapp_sequence;
        const int64_t whatsapp_now_us = esp_timer_get_time();
        if (whatsapp_received_us > 0 && whatsapp_now_us >= whatsapp_received_us) {
            ui.state.whatsapp_notice_until_us = whatsapp_now_us + 5000000;
            lunashell_audio_cue_status_t status{};
            const auto audio_error = CODY::notify_whatsapp_audio(status);
            if (audio_error != ESP_OK) ESP_LOGW(kTag, "WhatsApp cue failed (%s)", esp_err_to_name(audio_error));
            invalidate();
        }
    }
    LUNASHELL::CodyOptionsObservation counter_options{};
    const auto counter_error = DeviceSnapshotService::instance().readCodyOptionsObservation(counter_options);
    synchronize_character_theme(ui);
    ui.settings.counter_mode_error = counter_error != ESP_OK ? counter_error :
        counter_options.preferences.counter_mode_error != ESP_OK ? counter_options.preferences.counter_mode_error :
        !counter_options.preferences.counter_mode_valid ? ESP_ERR_INVALID_STATE : ESP_OK;
    ui.settings.counter_mode_valid = ui.settings.counter_mode_error == ESP_OK;
    if (ui.settings.counter_mode_valid &&
        ui.settings.counter_mode_enabled != counter_options.preferences.counter_mode_enabled) {
        ui.settings.counter_mode_enabled = counter_options.preferences.counter_mode_enabled;
        ui.state.counter_page = 0;
        if (ui.settings.counter_mode_enabled && ui.character.animation_active) {
            ui.character.animation_active = false;
            CODY::reset_muse_animation(0);
        }
        invalidate();
    }
    if (counter_error == ESP_OK && counter_options.preferences.weekly_alarm_valid &&
        counter_options.preferences.weekly_alarm_error == ESP_OK &&
        (counter_options.preferences.weekly_alarm_enabled || ui.character.usage_alarm_state.weekly.initialized || ui.character.usage_alarm_active)) {
        const auto link_error = DeviceSnapshotService::instance().readCodexTechnicalObservation(ui.model.dashboard.context);
        const auto &link = ui.model.dashboard.context.link;
        bool peer_ready = link_error == ESP_OK && link.data_valid && link.retained_link_present &&
            !link.pending_pairing_present && !link.command_pending && link.paired_pc_id[0] != '\0' &&
            strnlen(link.paired_pc_id, sizeof(link.paired_pc_id)) < sizeof(link.paired_pc_id);
        if (link_error == ESP_OK && link.data_valid && !link.retained_link_present) {
            ui.character.usage_alarm_state.weekly = {};
            ui.character.usage_alarm_peer[0] = '\0';
            if (ui.character.usage_alarm_active) close_usage_alarm(ui);
        } else if (peer_ready && std::strcmp(ui.character.usage_alarm_peer, link.paired_pc_id) != 0) {
            ui.character.usage_alarm_state.weekly = {};
            std::snprintf(ui.character.usage_alarm_peer, sizeof(ui.character.usage_alarm_peer), "%s", link.paired_pc_id);
            if (ui.character.usage_alarm_active) close_usage_alarm(ui);
        }
        LUNASHELL::CodexWeeklyLimitSnapshot weekly{};
        LUNASHELL::ClockSnapshot clock{};
        const auto weekly_error = DeviceSnapshotService::instance().readCodexWeeklyLimit(weekly);
        const auto clock_error = DeviceSnapshotService::instance().readClockObservation(clock);
        if (peer_ready) {
            const auto confirmation = DeviceSnapshotService::instance().readCodexTechnicalObservation(ui.model.dashboard.context);
            const auto &confirmed = ui.model.dashboard.context.link;
            peer_ready = confirmation == ESP_OK && confirmed.data_valid && confirmed.retained_link_present &&
                !confirmed.pending_pairing_present && !confirmed.command_pending &&
                std::strncmp(ui.character.usage_alarm_peer, confirmed.paired_pc_id, sizeof(ui.character.usage_alarm_peer)) == 0;
        }
        if ((peer_ready && weekly_error == ESP_OK && clock_error == ESP_OK) || !counter_options.preferences.weekly_alarm_enabled) {
            CODY::UsageAlarmNotice notice{};
            CODY::UsageAlarmOptions options{};
            options.enabled = counter_options.preferences.weekly_alarm_enabled;
            const auto alarm_error = CODY::observe_usage_alarms(ui.character.usage_alarm_state, options, weekly, clock, notice);
            if (alarm_error != ESP_OK) ESP_LOGE(kTag, "Weekly alarm observation failed (%s)", esp_err_to_name(alarm_error));
            if (notice.sequence != 0 && ui.character.event_cody_active) close_event_cody(ui);
            if (!options.enabled && ui.character.usage_alarm_active) close_usage_alarm(ui);
            if (ui.character.usage_alarm_active && !ui.character.usage_alarm_presented &&
                notice.sequence != ui.character.usage_alarm_notice.sequence) close_usage_alarm(ui);
            const bool eligible_layer = ui.state.retired.valid && (ui.state.retired.layer == Layer::Mascot ||
                ui.state.retired.layer == Layer::Dashboard || ui.state.retired.layer == Layer::Counter);
            if (alarm_error == ESP_OK && notice.sequence != 0 && !ui.character.usage_alarm_active && eligible_layer &&
                !ui.state.asleep && !ui.state.pressed && !ui.character.animation_active && !ui.state.diagnostics && !ui.state.audio_open &&
                !CODY::device_effects_require_attention(ui.character.effects) && ui.state.battery_banner.sequence == 0) {
                ui.character.usage_alarm_notice = notice;
                ui.character.usage_alarm_active = true;
                ui.character.usage_alarm_presented = false;
                ui.character.usage_alarm_started_us = now_us;
                std::snprintf(ui.character.usage_alarm_title, sizeof(ui.character.usage_alarm_title), "WEEKLY %u%%",
                    static_cast<unsigned>(notice.threshold_percent));
                auto &preview = ui.character.animation_preview;
                preview.manual = true;
                preview.manual_playing = true;
                preview.manual_pose = CODY::PixelAvatarPose::Error;
                preview.manual_symbol = CODY::MuseSymbol::Exclamation;
                preview.manual_working = false;
                preview.manual_elapsed_us = 0;
                preview.manual_needs_compose = true;
                CODY::dismiss_screensaver(ui.state.screensaver, now_us);
                invalidate();
            }
        } else if (ui.character.usage_alarm_active && !ui.character.usage_alarm_presented) close_usage_alarm(ui);
    } else if (ui.character.usage_alarm_active && !ui.character.usage_alarm_presented) {
        close_usage_alarm(ui);
    }
    if (ui.character.usage_alarm_active && ui.character.usage_alarm_presented && now_us - ui.character.usage_alarm_started_us >= 6000000)
        close_usage_alarm(ui);
    if (ui.character.event_cody_active && now_us - ui.character.event_cody_started_us >= 3000000) close_event_cody(ui);
    const int64_t delta = ui.state.last_active_us == 0 ? 0 : now_us - ui.state.last_active_us;
    ui.state.awake_us += delta;
    ui.state.last_active_us = now_us;
    esp_err_t error = ESP_OK;
    if (ui.state.scene.valid && ui.state.retired.valid && ui.state.retired.layer == Layer::Scene &&
        ui.state.completed_scene.definition == ui.state.scene.definition) {
        error = CODY::advance_scene(ui.state.scene, delta);
    }
    if (error == ESP_OK) error = update_navigation();
    if (error == ESP_OK) error = apply_standby_override();
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (error == ESP_OK) error = DeviceSnapshotService::instance().readVoiceObservation(ui.model.voice);
#endif
    if (error != ESP_OK) return error;
    LUNASHELL::CodyIdleObservation idle{};
    const auto idle_error = DeviceSnapshotService::instance().readCodyIdleObservation(idle);
    ui.settings.idle_error = idle_error != ESP_OK ? idle_error : idle.last_error != ESP_OK ? idle.last_error :
        !idle.valid ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (idle_error == ESP_OK && idle.valid && idle.last_error == ESP_OK &&
        (!ui.settings.idle_policy.valid || ui.settings.idle_policy.show_muse != idle.show_muse ||
         ui.settings.idle_policy.delay_seconds != idle.delay_seconds)) {
        ui.settings.idle_policy = idle;
        ui.state.idle_eligible_since_us = -1;
        ui.state.idle_applied = false;
    } else if (idle_error != ESP_OK || !idle.valid || idle.last_error != ESP_OK) {
        ui.settings.idle_policy.valid = false;
    }
    bool idle_eligible = ui.settings.idle_policy.valid && !ui.state.asleep && !ui.state.pressed && !ui.state.boot_menu_open &&
        !ui.character.animation_active && !ui.character.usage_alarm_active && !ui.character.event_cody_active && !ui.state.audio_open && !ui.state.diagnostics &&
        CODY::onboarding_dashboard_active() && ui.state.retired.valid &&
        (ui.state.retired.layer == Layer::Counter || ui.state.retired.layer == Layer::Mascot || ui.state.retired.layer == Layer::Dashboard) &&
        ui.state.screensaver.phase != CODY::ScreensaverPhase::Visible &&
        !CODY::device_effects_require_attention(ui.character.effects) && ui.state.battery_banner.sequence == 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    idle_eligible = idle_eligible && !voice_visible(now_us);
#endif
    if (!idle_eligible) {
        ui.state.idle_eligible_since_us = -1;
        ui.state.idle_applied = false;
    } else {
        if (ui.state.idle_eligible_since_us < 0 || now_us < ui.state.idle_eligible_since_us)
            ui.state.idle_eligible_since_us = now_us;
        const int64_t idle_since = std::max(ui.state.idle_eligible_since_us, ui.state.screensaver_last_touch_us);
        if (!ui.state.idle_applied && now_us >= idle_since &&
            now_us - idle_since >= static_cast<int64_t>(ui.settings.idle_policy.delay_seconds) * 1000000LL) {
            ui.state.idle_applied = true;
            ui.state.boot_section = ui.settings.idle_policy.show_muse ? BootSection::Events : BootSection::Counters;
            ui.state.mascot_home = ui.settings.idle_policy.show_muse;
            if (!ui.settings.idle_policy.show_muse) ui.state.activity_page = ActivityPage::Statistics;
            invalidate();
        }
    }
    const auto navigation = CODY::update_device_navigation(ui.model, ui.state, now_us);
    // Acquisition may have published a new IMU sample during the scene or
    // snapshot update. Compare it against a timestamp taken after the read.
    const int64_t frame_now_us = esp_timer_get_time();
    if (!ui.state.pressed) {
        LUNASHELL::CodyParallaxObservation preference{};
        const auto preference_error = DeviceSnapshotService::instance().readCodyParallaxObservation(preference);
        const auto level = preference_error == ESP_OK && preference.valid && preference.last_error == ESP_OK &&
            preference.level <= 3 ? static_cast<CODY::ParallaxLevel>(preference.level) : CODY::ParallaxLevel::Off;
        const bool active = !ui.state.asleep && ui.state.retired.valid &&
            (ui.state.retired.layer == Layer::Mascot || ui.state.retired.layer == Layer::BootMenu ||
             (ui.state.retired.layer == Layer::Scene && CODY::onboarding_main_menu_active()));
        if (CODY::update_parallax(ui.character.parallax, ui.model.motion.imu, navigation.motion_error,
                ui.model.motion.rendering_rotation_degrees, active, level, frame_now_us)) {
            ui.state.boot_menu.artwork_x = ui.character.parallax.x;
            ui.state.boot_menu.artwork_y = ui.character.parallax.y;
            if (++ui.state.boot_menu.revision == 0) ++ui.state.boot_menu.revision;
            CODY::set_onboarding_menu_parallax(ui.character.parallax.x, ui.character.parallax.y);
            if (ui.state.retired.layer == Layer::Scene && CODY::onboarding_main_menu_active()) ui.state.scene_dirty = true;
        }
    }
    // Animate only the visible quick menu at under one update per second.
    // Never retire a new menu revision while an existing touch is in progress.
    if (ui.state.boot_menu_open && !ui.state.asleep && !ui.state.pressed && ui.state.retired.valid &&
        ui.state.retired.layer == Layer::BootMenu) {
        const int64_t tick = frame_now_us / 1200000;
        if (tick != ui.state.boot_menu_animation_tick) {
            ui.state.boot_menu_animation_tick = tick;
            ui.state.boot_menu.animation_phase = (tick & 1) != 0;
            if (++ui.state.boot_menu.revision == 0) ++ui.state.boot_menu.revision;
        }
    } else if (!ui.state.boot_menu_open) {
        ui.state.boot_menu_animation_tick = -1;
    }
    observe_chrome_motion(ui, frame_now_us, navigation.motion_error);
    bool may_return_home = navigation.shaken && !ui.state.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.state.audio_open && !ui.state.diagnostics;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    may_return_home = may_return_home && !voice_visible(now_us);
#endif
    if (may_return_home) {
        LUNASHELL_power_manager_note_activity();
        CODY::dismiss_screensaver(ui.state.screensaver, now_us);
        select_statistics();
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (ui.character.animation_active && frame_now_us >= ui.character.animation_deadline_us) {
        ui.character.animation_active = false;
        CODY::reset_muse_animation(0);
        invalidate();
    }
    LUNASHELL::CodexAnimationRequest animation{};
    const auto animation_result = LUNASHELL::takeCodexAnimation(animation);
    esp_err_t animation_outcome = ESP_OK;
    if (animation_result == ESP_OK) {
        if (ui.character.usage_alarm_active) close_usage_alarm(ui);
        if (ui.character.event_cody_active) close_event_cody(ui);
        // Account for the visible interval before changing the playback state.
        auto &preview = ui.character.animation_preview;
        if (ui.character.animation_active && preview.visible && ui.state.awake_us >= preview.observed_active_us) {
            if (preview.manual_playing) {
                preview.manual_elapsed_us += static_cast<uint64_t>(ui.state.awake_us - preview.observed_active_us);
            }
            preview.observed_active_us = ui.state.awake_us;
        }
        using Op = LUNASHELL::CodexAnimationOperation;
        struct Entry { CODY::PixelAvatarPose pose; CODY::MuseSymbol symbol; bool working; };
        static constexpr Entry entries[] = {
            {CODY::PixelAvatarPose::Idle, CODY::MuseSymbol::None, false},
            {CODY::PixelAvatarPose::Listening, CODY::MuseSymbol::Question, false},
            {CODY::PixelAvatarPose::Thinking, CODY::MuseSymbol::Thinking, false},
            {CODY::PixelAvatarPose::Thinking, CODY::MuseSymbol::None, true},
            {CODY::PixelAvatarPose::Reply, CODY::MuseSymbol::Idea, false},
            {CODY::PixelAvatarPose::Error, CODY::MuseSymbol::Exclamation, false},
            {CODY::PixelAvatarPose::Sleep, CODY::MuseSymbol::Sleep, false},
            {CODY::PixelAvatarPose::Waking, CODY::MuseSymbol::None, false},
            {CODY::PixelAvatarPose::Idle, CODY::MuseSymbol::Heart, false},
            {CODY::PixelAvatarPose::Idle, CODY::MuseSymbol::Sweat, false},
            {CODY::PixelAvatarPose::Error, CODY::MuseSymbol::Tension, false},
            {CODY::PixelAvatarPose::Thinking, CODY::MuseSymbol::Question, false},
            {CODY::PixelAvatarPose::Listening, CODY::MuseSymbol::Exclamation, false},
        };
        const auto index = static_cast<size_t>(animation.scene);
        if (index >= std::size(entries)) {
            animation_outcome = ESP_ERR_INVALID_ARG;
        } else if (animation.operation == Op::Normal) {
            if (ui.character.animation_active) CODY::reset_muse_animation(0);
            ui.character.animation_active = false;
            invalidate();
        } else if (animation.operation == Op::Select && !ui.character.animation_active &&
            (!ui.state.retired.valid || (ui.state.retired.layer != Layer::Mascot && ui.state.retired.layer != Layer::Dashboard))) {
            animation_outcome = ESP_ERR_INVALID_STATE;
        } else if (animation.operation == Op::Select) {
            const auto &entry = entries[index];
            CODY::reset_muse_animation(0);
            ui.character.animation_scene = animation.scene;
            ui.character.animation_preview.manual = true;
            ui.character.animation_preview.manual_pose = entry.pose;
            ui.character.animation_preview.manual_symbol = entry.symbol;
            ui.character.animation_preview.manual_working = entry.working;
            ui.character.animation_preview.manual_elapsed_us = 0;
            ui.character.animation_preview.manual_playing = false;
            ui.character.animation_preview.manual_needs_compose = true;
            ui.character.animation_active = true;
            ui.character.animation_deadline_us = frame_now_us + 300000000;
            CODY::dismiss_screensaver(ui.state.screensaver, frame_now_us);
            invalidate();
        } else if (!ui.character.animation_active || animation.scene != ui.character.animation_scene) {
            animation_outcome = ESP_ERR_INVALID_STATE;
        } else {
            if (animation.operation == Op::Restart) {
                CODY::reset_muse_animation(0);
                ui.character.animation_preview.manual_elapsed_us = 0;
            }
            ui.character.animation_preview.manual_playing = animation.operation != Op::Pause;
            ui.character.animation_deadline_us = frame_now_us + 300000000;
            if (animation.operation == Op::Restart || animation.operation == Op::Pause) invalidate();
        }
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    LUNASHELL::CodexDebugScreenRequest debug{};
    const auto debug_result = LUNASHELL::takeCodexDebugScreen(debug);
    if (debug_result == ESP_OK) {
        if (ui.character.animation_active) {
            ui.character.animation_active = false;
            CODY::reset_muse_animation(0);
        }
        ui.state.diagnostics = debug.screen == LUNASHELL::CodexDebugScreen::Character;
        invalidate();
    } else if (debug_result != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(kTag, "Diagnostics request unavailable (%s)", esp_err_to_name(debug_result));
    }
#endif
    FrameView frame{};
    error = prepare_current_view(frame, frame_now_us, navigation.motion_error);
    if (error == ESP_OK) error = render(frame);
    const auto &effects = ui.character.effects;
    if (error == ESP_OK && effects.event_visual_sequence != ui.character.event_visual_seen) {
        LUNASHELL::CodyEventVisualObservation preference{};
        const auto preference_error = DeviceSnapshotService::instance().readCodyEventVisualObservation(preference);
        const bool eligible_layer = ui.state.retired.valid && (ui.state.retired.layer == Layer::Mascot ||
            ui.state.retired.layer == Layer::Dashboard || ui.state.retired.layer == Layer::Counter);
        if (preference_error == ESP_OK && preference.valid && preference.last_error == ESP_OK && preference.enabled &&
            effects.event_online && effects.event_visual_at_us > 0 && frame_now_us >= effects.event_visual_at_us &&
            effects.event.kind == effects.event_visual_kind && effects.event.state == LUNASHELL::CodexEventState::Pending &&
            std::strcmp(effects.event_visual_request, effects.event.request_id) == 0 &&
            eligible_layer && !ui.state.pressed &&
            !ui.state.asleep && !ui.state.boot_menu_open && !ui.character.animation_active && !ui.character.usage_alarm_active &&
            !ui.character.event_cody_active && !ui.state.audio_open && !ui.state.diagnostics &&
            (!CODY::device_effects_require_attention(effects) ||
             effects.event.kind == LUNASHELL::CodexEventKind::ReplyReady) && ui.state.battery_banner.sequence == 0) {
            ui.character.event_cody_active = true;
            ui.character.event_cody_started_us = frame_now_us;
            ui.character.event_cody_kind = effects.event_visual_kind;
            ui.character.event_cody_severity = effects.event.severity;
            std::memcpy(ui.character.event_cody_request, effects.event.request_id, sizeof(ui.character.event_cody_request));
            std::memcpy(ui.character.event_cody_title, effects.event.title, sizeof(ui.character.event_cody_title));
            const char *origin = effects.event.origin_title[0] != '\0' ? effects.event.origin_title : "Chat title unavailable";
            switch (effects.event.severity) {
            case LUNASHELL::CodexEventSeverity::Information: origin = "Computer - information"; break;
            case LUNASHELL::CodexEventSeverity::Warning: origin = "Computer - warning"; break;
            case LUNASHELL::CodexEventSeverity::Critical: origin = "Computer - critical"; break;
            case LUNASHELL::CodexEventSeverity::None: break;
            }
            const auto origin_error = prepare_event_cue_origin(ui.character, origin);
            if (origin_error != ESP_OK) {
                ui.character.event_cody_active = false;
                ESP_LOGW(kTag, "Notification origin preparation failed (%s)", esp_err_to_name(origin_error));
            } else {
                auto &preview = ui.character.animation_preview;
                preview.manual = true;
                preview.manual_playing = true;
                const auto severity = effects.event.severity;
                const bool critical = severity == LUNASHELL::CodexEventSeverity::Critical ||
                    effects.event_visual_kind == LUNASHELL::CodexEventKind::ChecksFailed;
                const bool warning = severity == LUNASHELL::CodexEventSeverity::Warning;
                preview.manual_pose = critical ? CODY::PixelAvatarPose::Error :
                    warning ? CODY::PixelAvatarPose::Thinking : CODY::PixelAvatarPose::Reply;
                preview.manual_symbol = critical ? CODY::MuseSymbol::Exclamation :
                    warning ? CODY::MuseSymbol::Sweat : CODY::MuseSymbol::Heart;
                preview.manual_working = false;
                preview.manual_elapsed_us = 0;
                preview.manual_needs_compose = true;
                invalidate();
            }
            ui.character.event_visual_seen = effects.event_visual_sequence;
        }
    }
    if (error == ESP_OK && ui.character.usage_alarm_active && !ui.character.usage_alarm_presented && ui.state.retired.valid &&
        ui.state.retired.layer == Layer::UsageAlarm) {
        CODY::acknowledge_usage_alarm(ui.character.usage_alarm_state, ui.character.usage_alarm_notice);
        ui.character.usage_alarm_presented = true;
        ui.character.usage_alarm_started_us = esp_timer_get_time();
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        (void)CODY::play_pet_cue(ui.character.effects, ui.character.usage_alarm_notice.cue, voice);
    }
    if (error == ESP_OK && ui.state.retired.valid && ui.state.retired.layer == Layer::CodexBrowser &&
        ui.state.codex_browser.bookmark_sound_pending) {
        ui.state.codex_browser.bookmark_sound_pending = false;
        play_bookmark_cue(ui);
    }
    if (error == ESP_OK && ui.state.questions_navigation_sound_pending) {
        ui.state.questions_navigation_sound_pending = false;
        if (ui.state.retired.valid && ui.state.retired.layer == Layer::Events &&
            ui.state.browser_destination == CODY::CodexBrowserDestination::Events &&
            ui.state.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions) {
            play_questions_cue(ui);
        }
    }
    if (error == ESP_OK && ui.state.activity_navigation_sound_pending) {
        ui.state.activity_navigation_sound_pending = false;
        if (ui.state.retired.valid && ui.state.retired.layer == Layer::CurrentTurn &&
            ui.state.browser_destination == CODY::CodexBrowserDestination::CurrentTurn) {
            play_activity_cue(ui);
        }
    }
    const auto startup_symbol = ui.state.retired.valid && ui.state.retired.layer == Layer::Scene
        ? CODY::onboarding_startup_retired_symbol(ui.state.completed_scene) : CODY::MuseSymbol::None;
    if (error == ESP_OK && ui.state.retired.valid &&
        (ui.state.retired.layer == Layer::Mascot || ui.state.retired.layer == Layer::Animation ||
            ui.state.retired.failure_mascot ||
         startup_symbol != CODY::MuseSymbol::None)) {
        const auto &visible = ui.state.retired.layer == Layer::Mascot ? ui.character.pixel_demo : ui.character.animation_preview;
        const auto retired_symbol = ui.state.retired.layer == Layer::Scene ? startup_symbol : visible.retired_symbol;
        const bool retired_working = ui.state.retired.layer != Layer::Scene && visible.retired_working;
        const bool retired_surprise = ui.state.retired.layer != Layer::Scene && visible.retired_surprise;
        if (ui.character.announced_cue_layer != ui.state.retired.layer) {
            ui.character.announced_symbol = CODY::MuseSymbol::None;
            ui.character.announced_working = false;
            ui.character.announced_surprise = false;
            ui.character.symbol_cue_pending = false;
            ui.character.working_cue_pending = false;
            ui.character.announced_cue_layer = ui.state.retired.layer;
        }
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        if (ui.character.pet_tap_cue_pending && ui.state.retired.layer == Layer::Mascot) {
            ui.character.pet_tap_cue_pending = false;
            const auto cue_error = CODY::play_pet_cue(ui.character.effects, CODY::PetCue::Tap, voice);
            if (cue_error != ESP_OK) ESP_LOGW(kTag, "Muse tap cue admission failed (%s)", esp_err_to_name(cue_error));
        } else if (ui.state.retired.layer != Layer::Mascot) {
            ui.character.pet_tap_cue_pending = false;
        }
        if (retired_symbol != ui.character.announced_symbol) {
            ui.character.announced_symbol = retired_symbol;
            ui.character.symbol_cue_pending = retired_symbol != CODY::MuseSymbol::None;
            ui.character.symbol_cue_deadline_us = frame_now_us + kMuseCueWaitUs;
        }
        if (retired_working != ui.character.announced_working) {
            ui.character.announced_working = retired_working;
            ui.character.working_cue_pending = retired_working;
            ui.character.working_cue_deadline_us = frame_now_us + kMuseCueWaitUs;
        }
        // The shared speaker may be busy with a different event. Retry only while
        // the corresponding retired visual is still present and the cue is fresh.
        // Working has priority; a simultaneously visible symbol can follow it.
        if (ui.character.working_cue_pending) {
            if (frame_now_us >= ui.character.working_cue_deadline_us) {
                ui.character.working_cue_pending = false;
            } else {
                const auto status = CODY::play_device_working_cue(ui.character.effects, voice);
                if (status.state != LUNASHELL_CUE_DROPPED_BUSY) ui.character.working_cue_pending = false;
            }
        }
        if (ui.character.symbol_cue_pending && !ui.character.working_cue_pending) {
            if (frame_now_us >= ui.character.symbol_cue_deadline_us) {
                ui.character.symbol_cue_pending = false;
            } else {
                const auto status = CODY::play_device_symbol_cue(ui.character.effects, ui.character.announced_symbol, voice);
                if (status.state != LUNASHELL_CUE_DROPPED_BUSY) ui.character.symbol_cue_pending = false;
            }
        }
        if (retired_surprise && !ui.character.announced_surprise) {
            const auto cue_error = CODY::play_pet_cue(ui.character.effects, CODY::PetCue::Surprise, voice);
            if (cue_error != ESP_OK) ESP_LOGW(kTag, "Muse surprise cue admission failed (%s)", esp_err_to_name(cue_error));
        }
        ui.character.announced_surprise = retired_surprise;
    } else if (error == ESP_OK) {
        ui.character.announced_symbol = CODY::MuseSymbol::None;
        ui.character.announced_working = false;
        ui.character.announced_surprise = false;
        ui.character.symbol_cue_pending = false;
        ui.character.working_cue_pending = false;
        ui.character.announced_cue_layer = Layer::Blank;
        ui.character.pet_tap_cue_pending = false;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (animation_result == ESP_OK) {
        if (animation_outcome == ESP_OK) {
            const bool visible = ui.state.retired.valid && ui.state.retired.layer == Layer::Animation;
            const bool expected = animation.operation != LUNASHELL::CodexAnimationOperation::Normal;
            if (error != ESP_OK) animation_outcome = error;
            else if (visible != expected) animation_outcome = ESP_ERR_INVALID_STATE;
            if (animation_outcome != ESP_OK && expected) {
                ui.character.animation_active = false;
                CODY::reset_muse_animation(0);
            }
        }
        const auto ack = LUNASHELL::completeCodexAnimation(animation, animation_outcome);
        if (ack != ESP_OK) ESP_LOGW(kTag, "Animation acknowledgement failed (%s)", esp_err_to_name(ack));
    }
    if (debug_result == ESP_OK) {
        const bool matched = (debug.screen == LUNASHELL::CodexDebugScreen::Character) ==
            (ui.state.retired.valid && ui.state.retired.layer == Layer::Diagnostics);
        const auto outcome = error == ESP_OK && !matched ? ESP_ERR_INVALID_STATE : error;
        const auto ack = LUNASHELL::completeCodexDebugScreen(debug, outcome, 0);
        if (ack != ESP_OK) ESP_LOGW(kTag, "Diagnostics acknowledgement failed (%s)", esp_err_to_name(ack));
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (error == ESP_OK && frame.layer == Layer::Voice) {
        ui.state.presented_voice_session = ui.model.voice.input.started_at_us;
        if (ui.model.voice.input.phase == VoicePhase::Preparing && ui.model.voice.input.destination_title[0] != '\0') {
            bool accepted = false;
            error = LUNASHELL::beginVoiceInputCountdown(ui.state.presented_voice_session, accepted);
        }
    }
#endif
    return error;
}

// ui: Display-owned audio outcome and canonical voice observation.
// cue: One startup transition whose sound has not yet been admitted.
// Returns false while the first-boot voice observation or audio mailbox is pending.
bool try_startup_pet_cue(DeviceRuntime &ui, CODY::PetCue cue)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    const auto observation_error = DeviceSnapshotService::instance().readVoiceObservation(ui.model.voice);
    if (observation_error != ESP_OK) {
        if (cue == CODY::PetCue::FirstBootMelody) return false;
        ESP_LOGW(kTag, "Startup pet cue voice observation failed (%s)", esp_err_to_name(observation_error));
        return true;
    }
    voice = &ui.model.voice;
#endif
    const auto error = CODY::play_pet_cue(ui.character.effects, cue, voice);
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
    const auto &live_cue = ui.character.effects.pet_audio_status;
    if (s_studio_live_active.load(std::memory_order_acquire) &&
        (live_cue.request_sequence != 0 || live_cue.state != LUNASHELL_CUE_DROPPED_BUSY)) {
        if (s_studio_live_cue_count < std::size(s_studio_live_cues))
            s_studio_live_cues[s_studio_live_cue_count++] = live_cue;
        else s_studio_live_record_error.store(ESP_ERR_NO_MEM, std::memory_order_release);
    }
#endif
    if (cue == CODY::PetCue::FirstBootMelody && error == ESP_ERR_NOT_FINISHED) return false;
    return error != ESP_OK || ui.character.effects.pet_audio_status.state != LUNASHELL_CUE_DROPPED_BUSY;
}

// ui: Serialized boot frame state. Each resource frame receives its original visible dwell.
// Exception: Returns the real display frame error; a parked panel can be retried.
esp_err_t advance_branding(DeviceRuntime &ui)
{
    if (!LUNASHELL_display_runtime_writes_allowed()) return ESP_OK;
    const bool starting = ui.state.branding_phase == BrandingPhase::AwaitingReady;
    if (starting) {
        // The startup owner posts Ready after radio DMA parking and final log archival.
        // Until then, the first retired frame stays visible without consuming animation time.
        const auto request = s_command.load(std::memory_order_acquire);
        if (static_cast<Command>(request & 0xffU) != Command::Ready) return ESP_OK;
    } else if (ui.state.boot_ding_pending) {
        const auto now_us = esp_timer_get_time();
        if (ui.state.first_boot_melody && ui.character.effects.boot_cue_requested &&
            ui.character.effects.boot_audio_error != ESP_OK) {
            // A failed restored output policy must not start the first-run melody.
            ui.state.boot_ding_pending = false;
        } else if (ui.state.first_boot_melody && ui.state.music_request.requested) {
            // Storage owns its bounded wait; the animation continues. Never admit
            // music after this boot has abandoned its exact consumption request.
            if (now_us >= ui.state.first_boot_storage_deadline_us) {
                ui.character.effects.boot_cue_requested = true;
                ui.character.effects.boot_audio_error = ESP_ERR_TIMEOUT;
                ui.state.boot_ding_pending = false;
                ESP_LOGW(kTag, "First-boot music consumption exceeded its display wait");
            }
        } else if (ui.state.first_boot_melody && ui.state.music_request.error != ESP_OK) {
            ui.character.effects.boot_cue_requested = true;
            ui.character.effects.boot_audio_error = ui.state.music_request.error;
            ui.state.boot_ding_pending = false;
        } else if (ui.state.boot_ding_deadline_us == 0) {
            // The full audio admission window begins only after durable success.
            ui.state.boot_ding_deadline_us = now_us + kStartupCueWaitUs;
        } else if (now_us >= ui.state.boot_ding_deadline_us) {
            ESP_LOGW(kTag, "Startup music or ding was not admitted before its deadline");
            ui.state.boot_ding_pending = false;
        } else if (try_startup_pet_cue(ui, ui.state.first_boot_melody ? CODY::PetCue::FirstBootMelody : CODY::PetCue::BootDing)) {
            ui.state.boot_ding_pending = false;
            const auto &status = ui.character.effects.pet_audio_status;
            if (ui.state.first_boot_melody && status.cue_id == static_cast<uint32_t>(CODY::PetCue::FirstBootMelody) &&
                status.state == LUNASHELL_CUE_DISPATCHED && status.request_sequence != 0) {
                ui.state.first_boot_melody_sequence = status.request_sequence;
                ui.state.first_boot_melody_deadline_us = now_us + 8000000LL;
            }
        }
    }
    if (!starting && esp_timer_get_time() < ui.state.branding_due_us) {
        return ESP_OK;
    }
    const uint16_t next = starting ? 0 : static_cast<uint16_t>(ui.state.branding_frame + 1U);
    if (next == VIBEWARE_BOOT_FRAME_COUNT) {
        if (ui.state.boot_ding_pending) return ESP_OK;
        if (ui.state.first_boot_melody_sequence != 0) {
            lunashell_audio_cue_status_t status{};
            const uint32_t cue_id = static_cast<uint32_t>(CODY::PetCue::FirstBootMelody);
            const auto status_error = LUNASHELL_audio_get_cue_status(ui.state.first_boot_melody_sequence, cue_id, &status);
            const bool active = status_error == ESP_OK &&
                (status.state == LUNASHELL_CUE_DISPATCHED || status.state == LUNASHELL_CUE_PLAYING);
            if (active && esp_timer_get_time() < ui.state.first_boot_melody_deadline_us) return ESP_OK;
            if (active) {
                const auto cancel_error = LUNASHELL_audio_cancel_cue(ui.state.first_boot_melody_sequence, cue_id);
                ESP_LOGW(kTag, "First-boot melody exceeded its deadline; cancel=%s", esp_err_to_name(cancel_error));
            } else if (status_error != ESP_OK || status.state == LUNASHELL_CUE_FAILED) {
                ESP_LOGW(kTag, "First-boot melody did not complete: observation=%s playback=%s",
                    esp_err_to_name(status_error), esp_err_to_name(status.last_error));
            }
            ui.state.first_boot_melody_sequence = 0;
        }
        ui.state.branding_phase = BrandingPhase::Inactive;
        ui.state.birth_active = ui.state.birth_requested;
        ESP_LOGI(kTag, "Boot disk animation completed: %u frames retired", VIBEWARE_BOOT_FRAME_COUNT);
        return ESP_OK;
    }
    const auto previous = ui.state.branding_frame;
    ui.state.branding_frame = next;
    FrameView frame{};
    frame.layer = Layer::Branding;
    const auto error = render(frame, true);
    if (error == ESP_OK) {
        ui.state.branding_due_us = esp_timer_get_time() + VIBEWARE_boot_frames[next].duration_ms * 1000LL;
        if (starting) {
            ui.state.branding_phase = BrandingPhase::Playing;
            if (ui.state.first_boot_melody) {
                // A visible frame precedes asynchronous durable consumption. Audio
                // admission waits for this exact request's successful receipt.
                ui.state.music_request.requested = true;
            }
            ui.state.boot_ding_pending = true;
            ui.state.first_boot_storage_deadline_us = esp_timer_get_time() + 22000000LL;
            ui.state.boot_ding_deadline_us = ui.state.first_boot_melody ? 0 : esp_timer_get_time() + kStartupCueWaitUs;
            ESP_LOGI(kTag, "Boot disk visible cycle started after service readiness");
        }
    } else if (error == ESP_ERR_NOT_FINISHED) {
        ui.state.branding_frame = previous;
    }
    return error;
}

// ui: Serialized startup state with one authored birth_frame selected by its owner.
// Exception: No exceptions; preparing this selected frame neither advances a lifecycle clock nor requests audio.
void prepare_birth_view(DeviceRuntime &ui)
{
    uint8_t white = 0;
    if (ui.state.birth_frame < kBirthWhiteHoldFrames) white = 255;
    else if (ui.state.birth_frame < kBirthMuseStartFrame) {
        white = static_cast<uint8_t>((kBirthMuseStartFrame - 1U - ui.state.birth_frame) * 255U /
                                     (kBirthMuseStartFrame - kBirthWhiteHoldFrames - 1U));
    }
    ui.state.birth_background = CODY2D::fade_rgb565_to_black(CODY::Theme::text_bright, white);
    if (ui.state.birth_frame >= kBirthMuseStartFrame) {
        const bool greeting = ui.state.birth_frame >= kBirthWelcomeFrame;
        const uint32_t avatar_ms = (ui.state.birth_frame - kBirthMuseStartFrame) * kBirthFrameMs;
        const uint32_t greeting_ms = greeting ? (ui.state.birth_frame - kBirthWelcomeFrame) * kBirthFrameMs : 0;
        CODY::compose_pixel_avatar(ui.character.birth_avatar,
            greeting ? CODY::PixelAvatarPose::Reply : CODY::PixelAvatarPose::Idle,
            avatar_ms, greeting ? greeting_ms : avatar_ms,
            greeting ? greeting_ms : CODY::kPixelAvatarReactionMs);
        const auto opacity = static_cast<uint8_t>(std::min<uint32_t>(255U,
            (ui.state.birth_frame - kBirthMuseStartFrame) * 255U / (kBirthMuseFadeFrames - 1U)));
        for (auto &color : ui.character.birth_avatar.palette) color = CODY2D::fade_rgb565_to_black(color, opacity);
        for (auto &color : ui.character.birth_avatar.dim_palette) color = CODY2D::fade_rgb565_to_black(color, opacity);
    }
}

// ui: Serialized startup state; only successfully retired frames advance the sequence.
// Exception: Preserves the original rendering failure; parked frames can be retried.
esp_err_t advance_birth_intro(DeviceRuntime &ui)
{
    if (!LUNASHELL_display_runtime_writes_allowed() || esp_timer_get_time() < ui.state.birth_due_us) return ESP_OK;
    if (ui.state.birth_rise_pending) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= ui.state.birth_rise_deadline_us) {
            ESP_LOGW(kTag, "Birth rise was not admitted before its deadline");
            ui.state.birth_rise_pending = false;
        } else if (!ui.state.boot_ding_pending && try_startup_pet_cue(ui, CODY::PetCue::BirthRise)) {
            ui.state.birth_rise_pending = false;
        } else {
            // Keep the first white frame on screen while the admitted ding retires.
            ui.state.birth_due_us = now_us + kBirthFrameMs * 1000LL;
            return ESP_OK;
        }
    }
    prepare_birth_view(ui);
    FrameView frame{};
    frame.layer = Layer::Birth;
    const auto error = render(frame, true);
    if (error != ESP_OK) return error;
    if (ui.state.birth_frame == 0 && !ui.state.first_boot_melody) {
        ui.state.birth_rise_pending = true;
        ui.state.birth_rise_deadline_us = esp_timer_get_time() + kStartupCueWaitUs;
    } else if (ui.state.birth_frame == kBirthWelcomeFrame && !ui.state.first_boot_melody) {
        (void)try_startup_pet_cue(ui, CODY::PetCue::BirthGreeting);
    }
    ++ui.state.birth_frame;
    ui.state.birth_due_us = esp_timer_get_time() + kBirthFrameMs * 1000LL;
    return ESP_OK;
}

// request: Display-owned admission identity and retry timing, never confirmed settings.
// operation: Exact service command to submit or observe.
// now_us: Monotonic retry clock.
// retry: Lifecycle completions retry; one-shot music failure remains silent for this boot.
// Exception: Keeps the original asynchronous error without failing the display worker.
void advance_lifecycle_request(LifecycleRequest &request, CODY::SettingsLifecycleOperation operation,
    int64_t now_us, bool retry)
{
    if (!request.requested) return;
    if (request.sequence != 0) {
        CODY::SettingsLifecycleReceipt receipt{};
        const auto observed = CODY::read_settings_lifecycle_receipt(operation, request.sequence, receipt);
        if (observed == ESP_OK && (receipt.state == CODY::SettingsCommandState::Queued ||
            receipt.state == CODY::SettingsCommandState::Running)) return;
        if (observed == ESP_OK && receipt.state == CODY::SettingsCommandState::Superseded) {
            // A newer explicit setting owns this field. Never replay an old
            // intro-completion intent over a newly requested birth replay.
            request.sequence = 0;
            request.requested = false;
            request.error = ESP_OK;
            return;
        }
        const auto error = observed != ESP_OK ? observed : receipt.error;
        request.sequence = 0;
        request.error = error;
        if (error == ESP_OK) {
            request.requested = false;
            return;
        }
        ESP_LOGW(kTag, "Lifecycle settings operation %u failed (%s)",
            static_cast<unsigned>(operation), esp_err_to_name(error));
        request.retry_at_us = now_us + 5000000;
        if (!retry) request.requested = false;
        return;
    }
    if (now_us < request.retry_at_us) return;
    const auto error = CODY::request_settings_lifecycle(operation, request.sequence, request.expected_revision);
    if (error != ESP_OK) {
        request.error = error;
        request.retry_at_us = now_us + 5000000;
        if (!retry) request.requested = false;
        ESP_LOGW(kTag, "Lifecycle settings operation %u not admitted (%s)",
            static_cast<unsigned>(operation), esp_err_to_name(error));
    }
}

#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
// target: Validated semantic browser target.
// Exception: No exceptions; unknown targets remain Closed and are never treated as browser pages.
CODY::CodexBrowserRoute studio_browser_route(const char *target)
{
    using Route = CODY::CodexBrowserRoute;
    struct NamedRoute { const char *target; Route route; };
    static constexpr NamedRoute routes[]{
        {"cody.codex.hub", Route::Hub}, {"cody.codex.chats", Route::Chats},
        {"cody.codex.turns", Route::Turns}, {"cody.codex.calls", Route::Calls},
        {"cody.codex.active", Route::ActiveCalls}, {"cody.codex.questions", Route::PendingQuestions},
    };
    for (const auto &route : routes) if (std::strcmp(target, route.target) == 0) return route.route;
    return Route::Closed;
}

// target: Stable About child identity selected by the host controller.
// page: Receives the typed InfoPage only for a supported target.
// Exception: No exceptions; unsupported names leave page unchanged.
bool studio_info_page(const char *target, CODY::InfoPage &page)
{
    struct NamedPage { const char *target; CODY::InfoPage page; };
    static constexpr NamedPage pages[]{
        {"cody.info.system", CODY::InfoPage::System}, {"cody.info.memory", CODY::InfoPage::Memory},
        {"cody.info.firmware", CODY::InfoPage::Firmware}, {"cody.info.vibeware", CODY::InfoPage::VibeWare},
        {"cody.info.project", CODY::InfoPage::ProjectLink},
    };
    for (const auto &candidate : pages) if (std::strcmp(target, candidate.target) == 0) {
        page = candidate.page;
        return true;
    }
    return false;
}

// target: Stable dashboard destination with an explicitly prepared account owner.
// screen: Receives the native typed carousel page only for an exact supported name.
// Exception: No exceptions; unknown targets leave screen unchanged.
bool studio_dashboard_page(const char *target, WidgetScreen &screen)
{
    struct NamedPage { const char *target; WidgetScreen screen; };
    static constexpr NamedPage pages[]{
        {"cody.dashboard.device", WidgetScreen::Device}, {"cody.dashboard.reset", WidgetScreen::Reset},
        {"cody.dashboard.limits", WidgetScreen::Limits}, {"cody.dashboard.today", WidgetScreen::Today},
        {"cody.dashboard.seven-days", WidgetScreen::LastSevenDays},
        {"cody.dashboard.token-history", WidgetScreen::TokenHistory},
        {"cody.dashboard.credits", WidgetScreen::Credits}, {"cody.dashboard.account", WidgetScreen::Account},
        {"cody.dashboard.context", WidgetScreen::Context}, {"cody.dashboard.about", WidgetScreen::About},
    };
    for (const auto &page : pages) if (std::strcmp(target, page.target) == 0) {
        screen = page.screen;
        return true;
    }
    return false;
}

// The original debug presenter has no active runtime route in this profile.
// Its bounded readout belongs exclusively to the display owner between frames.
CODY::MemoryDiagnosticReadout s_studio_memory{};

// state: Immutable bounded canonical heap projection retained for this native frame.
// strip: Borrowed logical RGB565 tile owned by the common display transaction.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves the original diagnostic renderer and Back raster errors.
esp_err_t draw_studio_memory_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (state != &s_studio_memory) return ESP_ERR_INVALID_ARG;
    auto error = CODY::draw_memory_diagnostics_rows(s_studio_memory, CODY::SceneData::body_font,
        strip, first_row, first_column);
    if (error == ESP_OK) error = CODY::draw_debug_view_back_rows(strip, first_row, first_column);
    return error;
}

// ui: Exclusive display owner retaining the canonical diagnostics observation.
// frame: Receives the isolated production memory presenter and its native Back glyph.
// Exception: No numeric heap data is emitted when its acquisition is unavailable.
void prepare_studio_memory(DeviceRuntime &ui, FrameView &frame)
{
    s_studio_memory = {};
    const auto &memory = ui.model.diagnostics.memory;
    if (memory.data_valid && !memory.data_stale && memory.last_error == ESP_OK) {
        s_studio_memory.internal = {memory.internal_heap_total_bytes, memory.internal_free_bytes,
            memory.internal_largest_block_bytes, memory.internal_minimum_free_bytes,
            memory.internal_free_blocks, memory.internal_heap_total_bytes > 0, false};
        s_studio_memory.psram = {memory.psram_heap_total_bytes, memory.psram_free_bytes,
            memory.psram_largest_block_bytes, memory.psram_minimum_free_bytes,
            memory.psram_free_blocks, memory.psram_heap_total_bytes > 0, false};
    }
    frame.presentation = {};
    frame.presentation.content.state = &s_studio_memory;
    frame.presentation.content.draw_rows = draw_studio_memory_rows;
    frame.presentation.content.bounds = {0, 0, kGeometry.width, kGeometry.height};
    frame.arrows = false;
}

// ui: Exclusive display owner with a freshly reconstructed native mascot view.
// Exception: Preserves canonical theme and native composition errors; no custom pose is substituted.
esp_err_t prepare_studio_muse(DeviceRuntime &ui)
{
    ui.state.mascot_home = true;
    ui.state.boot_section = BootSection::Default;
    ui.state.about_from_options = false;
    ui.state.about_from_boot_menu = false;
    ui.state.activity_page = ActivityPage::Statistics;
    ui.character.applied_character_theme = UINT8_MAX;
    synchronize_character_theme(ui);
    if (ui.character.theme_error != ESP_OK) return ui.character.theme_error;
    auto &view = ui.character.pixel_demo;
    view.showcase.reset(0);
    CODY::SceneContent primed{};
    const auto error = CODY::prepare_pixel_demo(view, 0, primed);
    if (error == ESP_OK) ui.state.awake_us = CODY::kPixelDemoWakingUs;
    return error;
}

// ui: Display owner retaining the real Muse underlay and canonical saver preference.
// frame: Receives the production saver grid at its first visible animation frame.
// Exception: Preserves owner/preference errors and rejects a blocked or unavailable underlay.
esp_err_t prepare_studio_screensaver(DeviceRuntime &ui, FrameView &frame)
{
    if (frame.layer != Layer::Mascot || !ui.state.mascot_home ||
        ui.character.pixel_demo.elapsed_us < CODY::kPixelDemoWakingUs) return ESP_ERR_INVALID_STATE;
    // This isolated static sample runs only the original presentation owner with
    // an explicit local idle history. It never advances workers or the host clock.
    CODY::ScreensaverModel sample{};
    CODY::update_screensaver(sample, true, 0, 0);
    CODY::update_screensaver(sample, true, 0, CODY::kScreensaverDelayUs);
    if (sample.settings_error != ESP_OK) return sample.settings_error;
    if (sample.phase != CODY::ScreensaverPhase::Visible || sample.frame != 0) return ESP_ERR_INVALID_STATE;
    ui.state.screensaver = sample;
    frame = {};
    frame.layer = Layer::Screensaver;
    return CODY::prepare_screensaver(ui.state.screensaver, ui.state.view.screensaver, frame.presentation.content);
}

// frame: Owner-prepared view about to enter the display transaction.
// Exception: No exceptions; rejects a different effective layer, page or query scope.
bool studio_frame_matches(const FrameView &frame)
{
    const char *target = s_studio_identity.target;
    auto &ui = *s_ui;
    if (std::strcmp(target, "cody.boot.branding") == 0) return frame.layer == Layer::Branding;
    if (std::strcmp(target, "cody.boot.birth") == 0) return frame.layer == Layer::Birth &&
        ui.state.birth_frame == s_studio_identity.source_animation_sample_ms / kBirthFrameMs;
    if (!CODY::studio_scene_active(target)) return false;
    if (std::strcmp(target, "cody.home.muse") == 0)
        return frame.layer == Layer::Mascot && ui.state.mascot_home &&
            ui.character.pixel_demo.visible && !ui.character.pixel_demo.manual &&
            ui.character.pixel_demo.elapsed_us == CODY::kPixelDemoWakingUs;
    if (std::strcmp(target, "cody.home.screensaver") == 0)
        return frame.layer == Layer::Screensaver && ui.state.mascot_home &&
            ui.state.screensaver.phase == CODY::ScreensaverPhase::Visible && ui.state.screensaver.frame == 0 &&
            frame.presentation.content.state == &ui.state.view.screensaver && ui.state.view.screensaver.frame == 0;
    if (std::strcmp(target, "cody.codex.connected") == 0) return frame.layer == Layer::Scene;
    WidgetScreen screen{};
    if (studio_dashboard_page(target, screen))
        return frame.layer == Layer::Dashboard && ui.state.widgets.selected_screen == screen &&
            (screen != WidgetScreen::About || (!ui.state.about_from_options && !ui.state.about_from_boot_menu &&
                ui.state.info_page == CODY::InfoPage::System));
    if (std::strcmp(target, "cody.settings.audio") == 0)
        return frame.layer == Layer::Audio && ui.state.audio_open;
    if (std::strcmp(target, "cody.diagnostics.memory") == 0)
        return frame.layer == Layer::Diagnostics && frame.presentation.content.state == &s_studio_memory;
    if (std::strcmp(target, "cody.diagnostics.overview") == 0)
        return frame.layer == Layer::Diagnostics && ui.state.diagnostics;
    CODY::InfoPage info{};
    if (studio_info_page(target, info)) return ui.state.widgets.selected_screen == WidgetScreen::About &&
        ui.state.info_page == info && frame.layer == (info == CODY::InfoPage::ProjectLink ? Layer::AboutLink : Layer::Dashboard);
    const auto browser_route = studio_browser_route(target);
    if (browser_route != CODY::CodexBrowserRoute::Closed)
        return frame.layer == Layer::CodexBrowser && ui.state.codex_browser.route == browser_route &&
            ui.state.codex_browser.chat.source.value == s_studio_identity.source_id &&
            ui.state.codex_browser.chat.value == s_studio_identity.chat_id &&
            ui.state.codex_browser.turn.value == s_studio_identity.turn_id;
    return frame.layer == Layer::Scene;
}

// ui: Serialized display owner whose browser scratch retains the exact query rows.
// request: Explicit selection checked against the synthetic canonical query owner.
// Exception: Returns unavailable, mismatched parent scope or original query errors.
esp_err_t prepare_studio_browser(DeviceRuntime &ui, const CodyHostStudioCase &request)
{
    auto &browser = ui.state.codex_browser;
    CODY::Domain::StateQuery query{};
    query.kind = CODY::Domain::StateQueryKind::Turn;
    query.turn = {{request.identity.source_id}, request.identity.turn_id};
    auto error = DeviceSnapshotService::instance().readCodexDomainObservation(query, browser.observation);
    if (error != ESP_OK) return error;
    const auto *turn = std::get_if<CODY::Domain::StateTurnPage>(&browser.observation.page);
    if (!turn || turn->count != 1 || turn->rows[0].id != query.turn ||
        turn->rows[0].chat.source.value != request.identity.source_id ||
        turn->rows[0].chat.value != request.identity.chat_id) return ESP_ERR_INVALID_STATE;
    browser.chat = turn->rows[0].chat;
    browser.turn = turn->rows[0].id;
    browser.turns_parent = CODY::CodexBrowserRoute::Chats;
    browser.route = studio_browser_route(request.identity.target);
    browser.refresh_at_us = 0;
    return browser.route == CODY::CodexBrowserRoute::Closed ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
}

enum class StudioRouteGesture : uint8_t { Touch, SwipeLeft, SwipeRight };

struct StudioRouteDefinition {
    const char *id;
    const char *origin;
    const char *destination;
    uint16_t x;
    uint16_t y;
    StudioRouteGesture gesture = StudioRouteGesture::Touch;
};

// These are host gesture contracts, not Python enum ordinals. Scene admission
// and the resulting destination remain verified by the native owner.
constexpr StudioRouteDefinition kStudioRoutes[]{
    {"settings-menu-to-display", "cody.settings.menu", "cody.settings.display", 233, 216},
    {"display-to-settings-menu", "cody.settings.display", "cody.settings.menu", 40, 233},
    {"settings-menu-to-preferences", "cody.settings.menu", "cody.settings.preferences", 330, 233, StudioRouteGesture::SwipeLeft},
    {"preferences-to-settings-menu", "cody.settings.preferences", "cody.settings.menu", 40, 233},
    {"preferences-to-device", "cody.settings.preferences", "cody.settings.device", 330, 233, StudioRouteGesture::SwipeLeft},
    {"device-to-preferences", "cody.settings.device", "cody.settings.preferences", 40, 233},
    {"settings-menu-to-behavior", "cody.settings.menu", "cody.settings.behavior", 233, 278},
    {"behavior-to-settings-menu", "cody.settings.behavior", "cody.settings.menu", 40, 233},
    {"menu-to-personalization", "cody.settings.menu", "cody.settings.personalization", 233, 340},
    {"personalization-to-menu", "cody.settings.personalization", "cody.settings.menu", 40, 233},
    {"display-to-rings", "cody.settings.display", "cody.settings.display-rings", 330, 233, StudioRouteGesture::SwipeLeft},
    {"rings-to-display", "cody.settings.display-rings", "cody.settings.display", 40, 233},
    {"rings-to-time", "cody.settings.display-rings", "cody.settings.time", 330, 233, StudioRouteGesture::SwipeLeft},
    {"time-to-rings", "cody.settings.time", "cody.settings.display-rings", 40, 233},
    {"behavior-to-more", "cody.settings.behavior", "cody.settings.behavior-more", 330, 233, StudioRouteGesture::SwipeLeft},
    {"more-to-behavior", "cody.settings.behavior-more", "cody.settings.behavior", 40, 233},
    {"behavior-more-to-settings", "cody.settings.behavior-more", "cody.settings.behavior-settings", 330, 233, StudioRouteGesture::SwipeLeft},
    {"behavior-settings-to-more", "cody.settings.behavior-settings", "cody.settings.behavior-more", 40, 233},
    {"restart-confirm-to-preferences", "cody.settings.restart-confirm", "cody.settings.preferences", 40, 233},
    {"reset-confirm-to-preferences", "cody.settings.reset-confirm", "cody.settings.preferences", 40, 233},
    {"info-system-to-memory", "cody.info.system", "cody.info.memory", 330, 233, StudioRouteGesture::SwipeLeft},
    {"info-memory-to-system", "cody.info.memory", "cody.info.system", 130, 233, StudioRouteGesture::SwipeRight},
    {"info-memory-to-firmware", "cody.info.memory", "cody.info.firmware", 330, 233, StudioRouteGesture::SwipeLeft},
    {"info-firmware-to-memory", "cody.info.firmware", "cody.info.memory", 130, 233, StudioRouteGesture::SwipeRight},
    {"info-firmware-to-vibeware", "cody.info.firmware", "cody.info.vibeware", 330, 233, StudioRouteGesture::SwipeLeft},
    {"info-vibeware-to-firmware", "cody.info.vibeware", "cody.info.firmware", 130, 233, StudioRouteGesture::SwipeRight},
    {"info-vibeware-to-project", "cody.info.vibeware", "cody.info.project", 330, 233, StudioRouteGesture::SwipeLeft},
    {"info-project-to-vibeware", "cody.info.project", "cody.info.vibeware", 130, 233, StudioRouteGesture::SwipeRight},
    {"info-system-to-settings-parent", "cody.info.system", "cody.settings.preferences", 130, 233, StudioRouteGesture::SwipeRight},
    {"bluetooth-to-wifi-choice", "cody.setup.bluetooth-instructions", "cody.setup.wifi-guide", 130, 120, StudioRouteGesture::SwipeRight},
    {"retained-pc-to-connections", "cody.codex.retained-pc", "cody.setup.connections", 130, 120, StudioRouteGesture::SwipeRight},
    {"forget-to-retained-pc", "cody.codex.forget-pc", "cody.codex.retained-pc", 130, 120, StudioRouteGesture::SwipeRight},
    {"guide-buttons-to-touch", "cody.guide.buttons", "cody.guide.touch", 426, 233},
    {"guide-touch-to-buttons", "cody.guide.touch", "cody.guide.buttons", 40, 233},
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    {"guide-touch-to-sofia", "cody.guide.touch", "cody.guide.sofia", 426, 233},
    {"guide-sofia-to-touch", "cody.guide.sofia", "cody.guide.touch", 40, 233},
    {"guide-sofia-to-offline", "cody.guide.sofia", "cody.guide.offline", 426, 233},
    {"guide-offline-to-sofia", "cody.guide.offline", "cody.guide.sofia", 40, 233},
#endif
    {"guide-offline-to-project", "cody.guide.offline", "cody.guide.project", 426, 233},
    {"guide-project-to-offline", "cody.guide.project", "cody.guide.offline", 40, 233},
    {"guide-project-to-thanks", "cody.guide.project", "cody.guide.thanks", 426, 233},
    {"guide-thanks-to-project", "cody.guide.thanks", "cody.guide.project", 40, 233},
};

// route_id: Null-terminated host route identity.
// Returns its compiled-profile contract or null when unavailable.
const StudioRouteDefinition *studio_route_definition(const char *route_id)
{
    if (!route_id) return nullptr;
    for (const auto &route : kStudioRoutes) if (std::strcmp(route.id, route_id) == 0) return &route;
    return nullptr;
}

// ui: Exclusive display owner with the origin frame already retired.
// request: Validated route and scenario revisions copied into the mailbox.
// Exception: Preserves input, route, presentation and frame errors without publishing a route receipt.
esp_err_t replay_studio_route(DeviceRuntime &ui, const CodyHostStudioRouteRequest &request)
{
    const auto *route = studio_route_definition(request.route_id);
    if (!route) return ESP_ERR_NOT_SUPPORTED;
    const char *origin = route->origin;
    const char *destination = route->destination;
    CodyHostStudioFrameIdentity observed{};
    auto error = cody_host_display_read_studio_identity(&observed);
    if (error != ESP_OK) return error;
    const bool source_layer = ui.state.retired.layer == Layer::Scene ||
        ui.state.retired.layer == Layer::Dashboard || ui.state.retired.layer == Layer::AboutLink;
    if (!ui.state.retired.valid || !source_layer ||
        std::strcmp(observed.target, origin) != 0 ||
        std::strcmp(observed.scenario_revision, request.origin_scenario_revision) != 0 ||
        std::strcmp(s_studio_identity.target, origin) != 0 ||
        std::strcmp(s_studio_identity.scenario_revision, request.origin_scenario_revision) != 0 ||
        !CODY::studio_scene_active(origin)) return ESP_ERR_INVALID_STATE;
    error = cody_host_studio_touch_begin();
    if (error != ESP_OK) return error;
    if (std::strcmp(origin, "cody.guide.project") == 0 ||
        std::strcmp(origin, "cody.info.project") == 0) {
        // QR pages initially hide native chrome. A real central tap reveals
        // it; retain input ownership while the intermediate frame retires.
        error = cody_host_studio_touch_event(1, 233, 233);
        if (error == ESP_OK) error = sample_touch();
        if (error == ESP_OK) error = cody_host_studio_touch_event(0, 233, 233);
        if (error == ESP_OK) error = sample_touch();
        if (error != ESP_OK) {
            (void)cody_host_studio_touch_end(true);
            ui.state.pressed = false;
            CODY::cancel_onboarding_touch();
            invalidate();
            return error;
        }
        if (!CODY::studio_scene_active(origin)) {
            (void)cody_host_studio_touch_end(true);
            invalidate();
            return ESP_ERR_INVALID_STATE;
        }
        FrameView reveal{};
        error = prepare_current_view(reveal, esp_timer_get_time());
        if (error == ESP_OK && !studio_frame_matches(reveal)) error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) error = render(reveal, true, RenderAdmission::LifecycleHandoff);
        if (error != ESP_OK || !ui.state.retired.valid || ui.state.retired.navigation_opacity != 255 ||
            !CODY::studio_scene_active(origin)) {
            (void)cody_host_studio_touch_end(true);
            invalidate();
            return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
        }
    }
    // Coordinates are native logical hit targets or authored swipe endpoints.
    // sample_touch applies the same retired-frame admission and owner handling
    // as an ordinary simulated contact.
    const uint16_t x = route->x;
    const uint16_t y = route->y;
    error = cody_host_studio_touch_event(1, x, y);
    const bool dispatched = error == ESP_OK;
    if (dispatched) {
        const auto press_error = sample_touch();
        const bool swipe = route->gesture != StudioRouteGesture::Touch;
        const uint16_t release_x = route->gesture == StudioRouteGesture::SwipeLeft ? 130U :
            route->gesture == StudioRouteGesture::SwipeRight ? 330U : x;
        esp_err_t motion_error = ESP_OK;
        if (press_error == ESP_OK && swipe) {
            // Settings and About pagination use the authored horizontal gesture.
            // The held report establishes dx for the same native touch owner.
            motion_error = cody_host_studio_touch_event(1, release_x, y);
            if (motion_error == ESP_OK) motion_error = sample_touch();
        }
        error = press_error != ESP_OK ? press_error : motion_error;
        if (error == ESP_OK) {
            const auto release_error = cody_host_studio_touch_event(0, release_x, y);
            error = release_error == ESP_OK ? sample_touch() : release_error;
        }
        // A failed held report must be aborted below, never converted into a
        // released tap that could enqueue a settings write on the start row.
    }
    if (error == ESP_OK) error = cody_host_studio_touch_end(false);
    if (error != ESP_OK) {
        const auto cleanup = cody_host_studio_touch_end(true);
        if (cleanup != ESP_OK) ESP_LOGE(kTag, "Studio touch cleanup failed (%s)", esp_err_to_name(cleanup));
        ui.state.pressed = false;
        CODY::cancel_onboarding_touch();
        if (dispatched) invalidate();
        return error;
    }
    if (!CODY::studio_scene_active(destination)) {
        invalidate();
        return ESP_ERR_INVALID_STATE;
    }
    error = CODY::settle_studio_scene(ui.state.scene);
    if (error != ESP_OK) {
        invalidate();
        return error;
    }
    auto identity = observed;
    std::strcpy(identity.target, destination);
    std::strcpy(identity.scenario_revision, request.destination_scenario_revision);
    identity.frame_revision = 0;
    s_studio_identity = identity;
    FrameView frame{};
    error = prepare_current_view(frame, esp_timer_get_time());
    if (error != ESP_OK) {
        invalidate();
        return error;
    }
    if (!studio_frame_matches(frame)) {
        invalidate();
        return ESP_ERR_INVALID_STATE;
    }
    CodyHostStudioRouteReceipt receipt{};
    receipt.origin_frame_revision = observed.frame_revision;
    std::strcpy(receipt.origin_scenario_revision, observed.scenario_revision);
    std::strcpy(receipt.route_id, request.route_id);
    cody_host_display_stage_studio_route(&receipt);
    error = render(frame, true, RenderAdmission::LifecycleHandoff);
    cody_host_display_stage_studio_route(nullptr);
    return error;
}

// ui: Serialized display worker whose first source frame was already prepared.
// request: Scenario identity and monotonically increasing requested instant.
// Exception: Returns original settings, worker, clock or display failures.
esp_err_t replay_studio_worker(DeviceRuntime &ui, const CodyHostStudioWorkerRequest &request)
{
    constexpr int64_t kOriginUs = 1000000;
    CodyHostStudioFrameIdentity observed{};
    auto error = cody_host_display_read_studio_identity(&observed);
    if (error != ESP_OK) return error;
    if (std::strcmp(observed.scenario_revision, request.scenario_revision) != 0 ||
        std::strcmp(s_studio_identity.scenario_revision, request.scenario_revision) != 0)
        return ESP_ERR_INVALID_STATE;
    if (!s_studio_worker.active) {
        const bool birth = std::strcmp(observed.target, "cody.boot.birth") == 0;
        const bool branding = std::strcmp(observed.target, "cody.boot.branding") == 0;
        if ((!birth && !branding) || observed.at_ms != 0 || observed.source_animation_sample_ms != 0 ||
            cody_host_manual_time_us.load(std::memory_order_acquire) != kOriginUs ||
            !ui.state.retired.valid || ui.state.retired.layer != (birth ? Layer::Birth : Layer::Branding))
            return ESP_ERR_INVALID_STATE;
        LUNASHELL::CodyOptionsObservation options{};
        error = DeviceSnapshotService::instance().readCodyOptionsObservation(options);
        if (error != ESP_OK) return error;
        if (!options.preferences.data_valid || options.preferences.data_stale ||
            options.preferences.last_error != ESP_OK || options.preferences.sounds_enabled)
            return ESP_ERR_INVALID_STATE;
        if (request.requested_ms >= (birth ? kBirthFrameCount * kBirthFrameMs : VIBEWARE_BOOT_DURATION_MS))
            return ESP_ERR_INVALID_ARG;
        s_studio_worker.active = true;
        s_studio_worker.birth = birth;
        std::strcpy(s_studio_worker.scenario_revision, request.scenario_revision);
        s_studio_worker_active.store(true, std::memory_order_release);
        // Ready is an explicitly pre-admitted host event; audio policy remains
        // owned by the real sound-off service and is not bypassed here.
        if (birth) {
            ui.state.birth_active = true;
            ui.state.birth_frame = 0;
            ui.state.birth_due_us = kOriginUs;
            error = advance_birth_intro(ui);
        } else {
            uint32_t expected = 0;
            if (!s_command.compare_exchange_strong(expected, static_cast<uint32_t>(Command::Ready),
                                                    std::memory_order_acq_rel)) return ESP_ERR_NOT_FINISHED;
            ui.state.branding_phase = BrandingPhase::AwaitingReady;
            error = advance_branding(ui);
            s_command.store(0, std::memory_order_release);
        }
        if (error != ESP_OK) return error;
        CodyHostStudioFrameIdentity first_tick{};
        error = cody_host_display_read_studio_identity(&first_tick);
        if (error != ESP_OK) return error;
        if (first_tick.frame_revision == observed.frame_revision || first_tick.at_ms != 0 ||
            std::strcmp(first_tick.target, observed.target) != 0 ||
            std::strcmp(first_tick.scenario_revision, observed.scenario_revision) != 0)
            return ESP_ERR_INVALID_STATE;
        ++s_studio_worker.ticks;
    } else if (std::strcmp(s_studio_worker.scenario_revision, request.scenario_revision) != 0 ||
               request.requested_ms < s_studio_worker.requested_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    const auto duration = s_studio_worker.birth ? kBirthFrameCount * kBirthFrameMs : VIBEWARE_BOOT_DURATION_MS;
    if (request.requested_ms >= duration) return ESP_ERR_INVALID_ARG;
    const int64_t until = kOriginUs + static_cast<int64_t>(request.requested_ms) * 1000;
    while (true) {
        const int64_t due = s_studio_worker.birth ? ui.state.birth_due_us : ui.state.branding_due_us;
        if (due > until) break;
        if (due < cody_host_manual_time_us.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
        cody_host_manual_time_us.store(due, std::memory_order_release);
        s_studio_identity.at_ms = static_cast<uint32_t>((due - kOriginUs) / 1000);
        s_studio_identity.source_animation_sample_ms = s_studio_worker.birth ?
            ui.state.birth_frame * kBirthFrameMs : s_studio_identity.at_ms;
        error = s_studio_worker.birth ? advance_birth_intro(ui) : advance_branding(ui);
        if (error != ESP_OK) return error;
        ++s_studio_worker.ticks;
        const int64_t next = s_studio_worker.birth ? ui.state.birth_due_us : ui.state.branding_due_us;
        if (next <= due) return ESP_ERR_INVALID_STATE;
    }
    cody_host_manual_time_us.store(until, std::memory_order_release);
    s_studio_worker.requested_ms = request.requested_ms;
    error = cody_host_display_read_studio_identity(&observed);
    if (error != ESP_OK) return error;
    CodyHostStudioWorkerReceipt receipt{};
    receipt.frame_revision = observed.frame_revision;
    receipt.requested_ms = request.requested_ms;
    receipt.frame_at_ms = observed.at_ms;
    receipt.tick_count = s_studio_worker.ticks;
    std::strcpy(receipt.target, observed.target);
    std::strcpy(receipt.scenario_revision, observed.scenario_revision);
    return cody_host_display_ack_studio_worker(&receipt);
}

// ui: Exclusive native display worker state; never read from a Python pointer.
// Exception: Returns an unavailable mailbox allocation; individual preparation
// errors are published to the host while the worker remains ready for another case.
esp_err_t process_studio_request(DeviceRuntime &ui)
{
    if (!s_studio) return ESP_ERR_NO_MEM;
    // The fixed request lives in the process-lifetime mailbox, not on the render
    // worker stack. running prevents its caller from replacing these borrowed bytes.
    const CodyHostStudioCase *request = nullptr;
    {
        std::unique_lock<std::mutex> lock(s_studio->mutex);
        s_studio->changed.wait_for(lock, std::chrono::milliseconds(100), [] { return s_studio->pending; });
        if (!s_studio->pending) return ESP_OK;
        request = &s_studio->request;
        s_studio->pending = false;
        s_studio->running = true;
    }
    if (s_studio->live) {
        CodyHostStudioFrameIdentity observed{};
        auto error = cody_host_display_read_studio_identity(&observed);
        const bool birth = std::strcmp(observed.target, "cody.boot.birth") == 0;
        const bool branding = std::strcmp(observed.target, "cody.boot.branding") == 0;
        if (error == ESP_OK && ((!birth && !branding) || observed.at_ms != 0 ||
            observed.source_animation_sample_ms != 0 || !ui.state.retired.valid ||
            ui.state.retired.layer != (birth ? Layer::Birth : Layer::Branding))) error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK && s_studio->live_sounds)
            error = CODY::persist_companion_preference(CODY::PreferenceId::PlaybackVolume, s_studio->live_volume);
        if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::Sounds, s_studio->live_sounds);
        if (error == ESP_OK) {
            // Live frames have neither static scenario identity nor a controlled
            // capture timestamp. Never attach the previous prepared identity.
            s_studio_identity = {};
            s_studio_live_active.store(true, std::memory_order_release);
            // Publish the steady anchor first, then release the manual gate. No
            // observer can see a backwards jump when the display worker resumes.
            using clock = std::chrono::steady_clock;
            const auto now = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
            cody_host_live_origin_steady_us.store(now, std::memory_order_release);
            cody_host_manual_time_us.store(0, std::memory_order_release);
            if (birth) {
                ui.state.birth_active = true;
                ui.state.birth_frame = 0;
                ui.state.birth_due_us = esp_timer_get_time();
                error = advance_birth_intro(ui);
            } else {
                uint32_t expected = 0;
                if (!s_command.compare_exchange_strong(expected, static_cast<uint32_t>(Command::Ready), std::memory_order_acq_rel)) {
                    error = ESP_ERR_NOT_FINISHED;
                } else {
                    ui.state.branding_phase = BrandingPhase::AwaitingReady;
                    error = advance_branding(ui);
                    s_command.store(0, std::memory_order_release);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(s_studio->mutex);
            s_studio->error = error;
            s_studio->running = false;
            s_studio->complete = true;
        }
        s_studio->changed.notify_all();
        return ESP_OK;
    }
    if (s_studio->worker) {
        const auto error = replay_studio_worker(ui, s_studio->worker_request);
        if (error != ESP_OK) ESP_LOGE(kTag, "Studio worker replay failed (%s)", esp_err_to_name(error));
        {
            std::lock_guard<std::mutex> lock(s_studio->mutex);
            s_studio->error = error;
            s_studio->running = false;
            s_studio->complete = true;
        }
        s_studio->changed.notify_all();
        return ESP_OK;
    }
    if (s_studio->replay) {
        const auto error = replay_studio_route(ui, s_studio->route_request);
        if (error != ESP_OK) ESP_LOGE(kTag, "Studio route replay failed: route=%s cause=%s (0x%08x)",
            s_studio->route_request.route_id, esp_err_to_name(error), static_cast<unsigned>(error));
        {
            std::lock_guard<std::mutex> lock(s_studio->mutex);
            s_studio->error = error;
            s_studio->running = false;
            s_studio->complete = true;
        }
        s_studio->changed.notify_all();
        return ESP_OK;
    }
    cody_host_display_stage_studio_route(nullptr);
    ui.state = UiState{};
    // Each static preparation starts a new scenario at the same native instant.
    // Retaining a prior sample timestamp or warning acknowledgment would reject
    // edited battery values or suppress a warning on a repeated capture.
    // The canonical battery observation remains owned by the host snapshot service.
    ui.model.battery = {};
    ui.state.boot_section = BootSection::Events;
    ui.character.animation_active = false;
    ui.character.event_cody_active = false;
    ui.character.usage_alarm_active = false;
    // These views have immutable viewport members and own no heap resource.
    // Reconstruct them only between retired frames on their owning host thread.
    ui.character.pixel_demo.~PixelDemoView();
    new (&ui.character.pixel_demo) CODY::PixelDemoView{kMuseViewport};
    ui.character.animation_preview.~PixelDemoView();
    new (&ui.character.animation_preview) CODY::PixelDemoView{kMuseViewport};
    CODY::reset_muse_animation(0);
    // Retire prior touch and artwork ownership before preparing the new frame.
    // invalidate() hides mascot views; after preparation their borrowed pixels
    // must remain visible until render() has retired the complete transaction.
    invalidate();
    ui.state.navigation.interact(esp_timer_get_time());
    CODY::update_device_observations(ui.model.dashboard);
    LUNASHELL::CodyOptionsObservation options{};
    const auto options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(options);
    ui.settings.counter_mode_enabled = options_error == ESP_OK && options.preferences.counter_mode_valid &&
        options.preferences.counter_mode_error == ESP_OK && options.preferences.counter_mode_enabled;
    const char *stage = "read-motion";
    auto error = DeviceSnapshotService::instance().readMotionPowerObservation(ui.model.motion);
    s_studio_identity = request->identity;
    const char *target = s_studio_identity.target;
    const bool branding = std::strcmp(target, "cody.boot.branding") == 0;
    const bool birth = std::strcmp(target, "cody.boot.birth") == 0;
    const auto browser_route = studio_browser_route(target);
    const bool browser = browser_route != CODY::CodexBrowserRoute::Closed;
    const bool calls = browser_route == CODY::CodexBrowserRoute::Calls || browser_route == CODY::CodexBrowserRoute::ActiveCalls;
    const bool memory = std::strcmp(target, "cody.diagnostics.memory") == 0;
    const bool muse = std::strcmp(target, "cody.home.muse") == 0;
    const bool screensaver = std::strcmp(target, "cody.home.screensaver") == 0;
    const bool connected = std::strcmp(target, "cody.codex.connected") == 0;
    const bool event = CODY::host_studio_events::is_target(target);
    const bool maintenance = std::strcmp(target, "cody.settings.restart-pending") == 0 ||
        std::strcmp(target, "cody.settings.reset-pending") == 0 ||
        std::strcmp(target, "cody.settings.reset-failed") == 0;
    const bool questions = browser_route == CODY::CodexBrowserRoute::PendingQuestions &&
        std::strcmp(request->domain_questions_outcome, "supported") == 0;
    std::strcpy(s_studio_identity.rendering_mode, browser || memory || screensaver || connected || maintenance || event ?
        "presenter-isolated" : "runtime-full");
    FrameView frame{};
    if (error == ESP_OK && branding) {
        stage = "prepare-branding";
        error = CODY::BootBranding::prepare(CODY::SceneData::title_font, CODY::SceneData::body_font, ui.state.branding);
        uint32_t elapsed_ms = 0;
        while (ui.state.branding_frame + 1U < VIBEWARE_BOOT_FRAME_COUNT &&
            elapsed_ms + VIBEWARE_boot_frames[ui.state.branding_frame].duration_ms <= s_studio_identity.source_animation_sample_ms) {
            elapsed_ms += VIBEWARE_boot_frames[ui.state.branding_frame].duration_ms;
            ++ui.state.branding_frame;
        }
        frame.layer = Layer::Branding;
    } else if (error == ESP_OK && birth) {
        stage = "prepare-birth";
        ui.state.birth_frame = s_studio_identity.source_animation_sample_ms / kBirthFrameMs;
        ui.character.birth_avatar = {};
        error = CODY::prepare_styled_title({"Welcome!", 8}, {73, 356, 320, 40}, ui.state.birth_welcome, 3, true);
        if (error == ESP_OK) prepare_birth_view(ui);
        frame.layer = Layer::Birth;
    } else if (error == ESP_OK) {
        stage = "prepare-route";
        error = CODY::prepare_studio_scene(ui.state.scene, *request);
        if (error == ESP_OK && browser) {
            stage = "prepare-browser-scope";
            error = prepare_studio_browser(ui, *request);
        }
        if (error == ESP_OK && (muse || screensaver)) {
            stage = "prepare-muse-sample";
            error = prepare_studio_muse(ui);
        }
        WidgetScreen screen{};
        if (error == ESP_OK && studio_dashboard_page(target, screen)) {
            ui.state.activity_page = ActivityPage::Statistics;
            CODY::select_widget_screen(ui.state.widgets, screen);
            if (screen == WidgetScreen::About) {
                ui.state.about_from_options = false;
                ui.state.about_from_boot_menu = false;
                ui.state.info_page = CODY::InfoPage::System;
            }
        }
        if (error == ESP_OK && std::strcmp(target, "cody.settings.audio") == 0)
            ui.state.audio_open = true;
        CODY::InfoPage info{};
        if (error == ESP_OK && studio_info_page(target, info)) {
            ui.state.activity_page = ActivityPage::Statistics;
            ui.state.about_from_options = true;
            ui.state.info_page = info;
            CODY::select_widget_screen(ui.state.widgets, WidgetScreen::About);
        }
        if (error == ESP_OK && (memory || std::strcmp(target, "cody.diagnostics.overview") == 0)) {
            ui.state.activity_page = ActivityPage::Statistics;
            ui.state.diagnostics = true;
        }
        if (error == ESP_OK) {
            stage = "prepare-view";
            if (connected || maintenance || event) frame.layer = Layer::Scene;
            else error = prepare_current_view(frame, esp_timer_get_time());
            if (error == ESP_OK && memory) prepare_studio_memory(ui, frame);
            if (error == ESP_OK && screensaver) error = prepare_studio_screensaver(ui, frame);
        }
    }
    if (error == ESP_OK && calls) {
        stage = "verify-calls-scope";
        const auto &observed = ui.state.codex_browser.observation;
        const auto *page = std::get_if<CODY::Domain::StateCallPage>(&observed.page);
        if (!page || observed.summary.source.value != ui.state.codex_browser.chat.source.value ||
            page->count != ui.state.codex_browser.page.count || page->count > 3) error = ESP_ERR_INVALID_STATE;
        else {
            s_studio_identity.source_id = observed.summary.source.value;
            s_studio_identity.chat_id = ui.state.codex_browser.chat.value;
            s_studio_identity.turn_id = ui.state.codex_browser.turn.value;
            s_studio_identity.call_count = page->count;
            for (uint8_t index = 0; index < page->count; ++index) {
                if (page->rows[index].turn != ui.state.codex_browser.turn) { error = ESP_ERR_INVALID_STATE; break; }
                s_studio_identity.call_ids[index] = page->rows[index].id.value;
            }
        }
    }
    if (error == ESP_OK && questions) {
        stage = "verify-questions-scope";
        const auto &observed = ui.state.codex_browser.observation;
        const auto *page = std::get_if<CODY::Domain::StateQuestionPage>(&observed.page);
        if (!page || observed.summary.source.value != request->domain_source_id ||
            page->count != request->domain_question_count || page->count != ui.state.codex_browser.page.count ||
            page->count > 3) error = ESP_ERR_INVALID_STATE;
        else {
            using CODY::Domain::Availability;
            using CODY::Domain::Freshness;
            s_studio_identity.question_count = page->count;
            for (uint8_t index = 0; index < page->count; ++index) {
                const auto &row = page->rows[index];
                const auto &expected = request->domain_questions[index];
                if (row.id.source.value != request->domain_source_id || row.id.value != expected.id ||
                    row.chat.fact.availability != Availability::Known || row.chat.fact.freshness != Freshness::Current ||
                    row.chat.value.source.value != request->domain_source_id || row.chat.value.value != expected.chat_id ||
                    row.turn.fact.availability != Availability::Known || row.turn.fact.freshness != Freshness::Current ||
                    row.turn.value.source.value != request->domain_source_id || row.turn.value.value != expected.turn_id ||
                    row.prompt.fact.availability != Availability::Known || row.prompt.fact.freshness != Freshness::Current ||
                    std::strcmp(row.prompt.value.data(), expected.prompt) != 0) {
                    error = ESP_ERR_INVALID_STATE;
                    break;
                }
                s_studio_identity.question_ids[index] = row.id.value;
            }
        }
    }
    if (error == ESP_OK && std::strcmp(target, "cody.provisioning.error") == 0) {
        stage = "read-provisioning-result";
        LUNASHELL::ConnectivityObservation observation{};
        error = DeviceSnapshotService::instance().readConnectivityObservation(observation);
        if (error == ESP_OK) {
            const auto &pairing = observation.radios.pairing;
            s_studio_identity.service_error = pairing.last_error;
            std::strcpy(s_studio_identity.service_outcome, pairing.state == LUNASHELL::PairingSessionState::TimedOut ? "timeout" :
                pairing.failure_reason == LUNASHELL::ProvisioningFailureReason::Authentication ? "authentication" : "network-not-found");
        }
    }
    if (error == ESP_OK) {
        stage = "verify-destination";
        if (!studio_frame_matches(frame)) error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        stage = "render-frame";
        error = render(frame, true, RenderAdmission::LifecycleHandoff);
    }
    if (error != ESP_OK) ESP_LOGE(kTag, "Studio preparation failed: target=%s stage=%s cause=%s (0x%08x)",
        target, stage, esp_err_to_name(error), static_cast<unsigned>(error));
    {
        std::lock_guard<std::mutex> lock(s_studio->mutex);
        s_studio->error = error;
        s_studio->running = false;
        s_studio->complete = true;
    }
    s_studio->changed.notify_all();
    return ESP_OK;
}
#endif

// context: Unused; the task exclusively owns native navigation and presentation.
// Exception: Publishes terminal display failures through the snapshot owner.
void device_ui_worker(void *context)
{
    (void)context;
    auto &ui = *s_ui;
    esp_err_t error = ESP_OK;
    while (error == ESP_OK) {
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
        if (cody_host_manual_time_us.load(std::memory_order_acquire) > 0 || cody_host_audio_audition_active()) {
            error = process_studio_request(ui);
            continue;
        }
#endif
        const auto lifecycle_now_us = esp_timer_get_time();
        advance_lifecycle_request(ui.state.music_request, CODY::SettingsLifecycleOperation::ConsumeFirstBootMusic,
            lifecycle_now_us, false);
        advance_lifecycle_request(ui.state.birth_request, CODY::SettingsLifecycleOperation::CompleteBirthIntro,
            lifecycle_now_us, true);
        advance_lifecycle_request(ui.state.setup_request, CODY::SettingsLifecycleOperation::CompleteSetup,
            lifecycle_now_us, true);
        CODY::update_device_uptime(ui.model.dashboard, esp_timer_get_time());
        if (ui.state.branding_phase != BrandingPhase::Inactive) {
            error = advance_branding(ui);
            if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
            if (s_studio_live_active.load(std::memory_order_acquire) &&
                ui.state.branding_phase == BrandingPhase::Inactive)
                s_studio_live_complete.store(true, std::memory_order_release);
#endif
            if (error == ESP_OK && ui.state.branding_phase != BrandingPhase::Inactive) {
                const uint32_t wait_ms = ui.state.branding_phase == BrandingPhase::AwaitingReady ? kPollMs : 10U;
                vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
            }
            continue;
        }
        if (ui.state.birth_active) {
            if (ui.state.birth_frame == kBirthFrameCount && esp_timer_get_time() >= ui.state.birth_due_us) {
                // Playback completion is volatile. Persistence is retried independently
                // while the normal display, touch and power mailbox keep progressing.
                ui.state.birth_active = false;
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
                if (s_studio_live_active.load(std::memory_order_acquire))
                    s_studio_live_complete.store(true, std::memory_order_release);
#endif
                ui.state.birth_request.requested = true;
                ESP_LOGI(kTag, "Muse introduction completed; durable completion queued");
            } else if (ui.state.birth_frame < kBirthFrameCount) {
                error = advance_birth_intro(ui);
                if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
            }
            if (error == ESP_OK && ui.state.birth_active) vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
            continue;
        }
        const uint32_t request = s_command.exchange(0, std::memory_order_acq_rel);
        if (request != 0) {
            const auto result = apply_command(static_cast<Command>(request & 0xffU));
            s_command_result.store(result, std::memory_order_relaxed);
            s_ack.store(request, std::memory_order_release);
        }
        if (!s_ready.load(std::memory_order_acquire) || ui.state.asleep ||
            !LUNASHELL_display_runtime_writes_allowed()) {
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
            continue;
        }
        const bool animating = ui.state.retired.voice_ribbon_at_us != 0 || ui.state.view.countdown.animating() ||
            ui.state.retired.heading_scrolls ||
            (ui.state.retired.layer == Layer::Scene && CODY::scene_title_overflows(ui.state.completed_scene));
        uint32_t wait_ms = kPollMs;
        if (ui.state.screensaver.phase == CODY::ScreensaverPhase::Visible) {
            wait_ms = static_cast<uint32_t>(CODY::kScreensaverFrameUs / 1000);
        } else if (animating) {
            wait_ms = 33;
        } else if (ui.state.retired.valid && (ui.state.retired.layer == Layer::Mascot || ui.state.retired.layer == Layer::Animation ||
            ui.state.retired.failure_mascot ||
            (ui.state.retired.layer == Layer::Scene && (CODY::onboarding_pc_alert_detail_active() || CODY::onboarding_guide_visual() != 0 || CODY::onboarding_wifi_wait_mascot() || CODY::onboarding_connection_mascot() != 0)) ||
            ui.state.retired.layer == Layer::UsageAlarm || ui.state.retired.layer == Layer::EventCody)) {
            const int64_t remaining_us = std::max<int64_t>(0, ui.character.muse_frame_due_us - esp_timer_get_time());
            wait_ms = static_cast<uint32_t>((remaining_us + 999) / 1000);
        }
        const auto wait = LUNASHELL_touch_wait_for_report(wait_ms);
        if (wait != ESP_OK && wait != ESP_ERR_NOT_FOUND) {
            error = wait;
            break;
        }
        if (s_command.load(std::memory_order_acquire) != 0) continue;
        if (wait == ESP_OK || ui.state.pressed) error = sample_touch();
        if (error == ESP_OK) error = update_and_draw(esp_timer_get_time());
        if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
    }
    set_audio_demand(false);
    s_audio_demand_stop.store(true, std::memory_order_release);
    s_terminal_error.store(error, std::memory_order_release);
    DeviceSnapshotService::instance().reportDisplay(false, error);
    ESP_LOGE(kTag, "Device display worker stopped (%s)", esp_err_to_name(error));
    vTaskDeleteWithCaps(nullptr);
}
} // namespace

// No parameters: initialize the device model and retire the first native boot frame.
// Exception: Returns original mandatory settings/display/worker failures; optional audio retains its own error.
extern "C" esp_err_t CODY_companion_start(void)
{
    if (s_ui != nullptr) return ESP_ERR_INVALID_STATE;
#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
    if (cody_host_manual_time_us.load(std::memory_order_acquire) > 0 && !s_studio)
        return ESP_ERR_NO_MEM;
#endif
    s_ui = new (s_ui_storage) DeviceRuntime{};
    CODY::initialize_device_uptime(s_ui->model.dashboard);
    CODY::SettingsBootstrapResult bootstrap{};
    auto error = CODY::bootstrap_device_settings(bootstrap);
    if (error == ESP_OK) error = CODY::onboarding_service_init();
    if (error != ESP_OK) return error;
    LUNASHELL::CodyOptionsObservation startup_options{};
    const auto options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(startup_options);
    s_ui->settings.counter_mode_error = options_error != ESP_OK ? options_error :
        startup_options.preferences.counter_mode_error != ESP_OK ? startup_options.preferences.counter_mode_error :
        !startup_options.preferences.counter_mode_valid ? ESP_ERR_INVALID_STATE : ESP_OK;
    s_ui->settings.counter_mode_valid = s_ui->settings.counter_mode_error == ESP_OK;
    if (s_ui->settings.counter_mode_valid) {
        s_ui->settings.counter_mode_enabled = startup_options.preferences.counter_mode_enabled;
    }
    // The settings bootstrap restored output before any optional Cody cue can play.
    const auto startup_volume_error = bootstrap.audio_output_error;
    // Reconcile input and retry transient output contention on the action worker.
    s_ui->state.audio_refresh_needed = true;
    s_ui->state.audio_refresh_retry_us = 0;
    // The acquisition owner has initialized the codec; prepare its shared notification worker.
    // A failed optional sound worker does not remove pairing or power controls.
    (void)CODY::prepare_device_effects(s_ui->character.effects);
    if (startup_volume_error != ESP_OK) {
        s_ui->character.effects.boot_cue_requested = true;
        s_ui->character.effects.boot_audio_error = startup_volume_error;
        ESP_LOGW(kTag, "Boot cue omitted: saved output policy unavailable (%s)",
            esp_err_to_name(startup_volume_error));
    }
    CODY::SettingsModel settings{};
    error = CODY::read_settings_model(settings);
    if (error != ESP_OK) return error;
    s_ui->state.birth_requested = settings.onboarding.birth_intro_pending && esp_reset_reason() != ESP_RST_DEEPSLEEP;
    s_ui->state.birth_request.expected_revision = settings.storage.birth_intro.revision;
    // The first visible boot frame consumes this separate one-shot marker before
    // audio admission, keeping ordinary sounds on later restarts or birth replays.
    s_ui->state.first_boot_melody = settings.onboarding.first_boot_music_pending && esp_reset_reason() != ESP_RST_DEEPSLEEP;
    error = CODY::prepare_status_caption(CODY::kSettingsRetryCaption, CODY::Theme::status_error,
        s_ui->state.settings_retry_notice);
    if (error == ESP_OK) error = CODY::prepare_status_caption(CODY::kBootSoundSettingsCaption, CODY::Theme::status_error,
        s_ui->state.settings_music_notice);
    if (error == ESP_OK) error = CODY::prepare_status_caption("WhatsApp: new message", CODY::Theme::secondary_default,
        s_ui->state.whatsapp_notice);
    if (error != ESP_OK) return error;
    static constexpr char welcome[] = "Welcome!";
    const CODY2D::TextSpan welcome_text{welcome, sizeof(welcome) - 1U};
    error = CODY::prepare_styled_title(welcome_text, {73, 356, 320, 40}, s_ui->state.birth_welcome, 3, true);
    if (error != ESP_OK) return error;
    FrameView frame{};
    const bool show_branding = esp_reset_reason() != ESP_RST_DEEPSLEEP;
    if (show_branding) s_ui->state.branding_phase = BrandingPhase::AwaitingReady;
    ESP_LOGI(kTag, "Boot disk animation: reset_reason=%u requested=%u frames=%u",
        static_cast<unsigned>(esp_reset_reason()), static_cast<unsigned>(show_branding), VIBEWARE_BOOT_FRAME_COUNT);
    if (show_branding) {
        frame.layer = Layer::Branding;
        error = VIBEWARE_boot_validate();
        if (error == ESP_OK) error = CODY::BootBranding::prepare(
            CODY::SceneData::title_font, CODY::SceneData::body_font, s_ui->state.branding);
    } else {
        frame.layer = Layer::Message;
        error = CODY::prepare_device_message(s_ui->state.view, frame.presentation, "Device", "Starting services");
    }
    if (error == ESP_OK) error = render(frame, true);
    if (error == ESP_OK && show_branding) {
        ESP_LOGI(kTag, "Boot disk first frame retired; awaiting service readiness");
    }
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_display_lifecycle(prepare_lifecycle);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_boot_options(queue_boot_voice);
#else
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_boot_options(queue_options);
#endif
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_boot_home(queue_boot_home);
#if CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_offline_farewell(prepare_offline_standby);
#endif
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_shutdown_callbacks(
        CODY_companion_prepare_shutdown, CODY_companion_report_shutdown_error);
    if (error != ESP_OK) return error;
    if (xTaskCreatePinnedToCoreWithCaps(device_ui_worker, "LUNA_device_ui", kWorkerStackBytes,
        nullptr, 2, nullptr, LUNASHELL::task_affinity::kCodyRenderCoreId,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

extern "C" esp_err_t CODY_companion_boot_completed(void)
{
    const auto error = request_command(Command::Ready);
    if (error == ESP_OK) s_ready.store(true, std::memory_order_release);
    return error;
}

extern "C" esp_err_t CODY_companion_prepare_shutdown(void)
{
    return s_ready.load(std::memory_order_acquire) ? request_command(Command::Shutdown) : ESP_ERR_INVALID_STATE;
}

extern "C" esp_err_t CODY_companion_report_shutdown_error(esp_err_t error)
{
    return error != ESP_OK ? request_command(Command::ShutdownFailed) : ESP_ERR_INVALID_ARG;
}

#if defined(CODY_HOST_REAL_AUDIO) && !defined(ESP_PLATFORM)
namespace {
// text: Fixed-capacity ABI string whose terminator and printable payload are required.
// Exception: No exceptions; rejects unterminated or non-ASCII input without reading past capacity.
template<size_t Size>
bool studio_ascii(const char (&text)[Size])
{
    const auto *end = static_cast<const char *>(std::memchr(text, '\0', Size));
    if (!end) return false;
    return std::all_of(text, end, [](char value) { return value >= 32 && value <= 126; });
}

// request: Bounded caller copy, with no settings or service mutation admitted yet.
// Exception: Returns precise ABI, identity, target, scalar or owner-validation errors.
esp_err_t validate_studio_request(const CodyHostStudioCase &request)
{
    const auto &identity = request.identity;
    if (request.abi != 3 || request.size != sizeof(CodyHostStudioCase) || identity.abi != 3)
        return ESP_ERR_INVALID_SIZE;
    if (!studio_ascii(identity.target) || !studio_ascii(identity.scenario_revision) ||
        !studio_ascii(identity.rendering_mode) || !studio_ascii(identity.service_outcome) ||
        !studio_ascii(request.provisioning_outcome) || !studio_ascii(request.codex_link_outcome) ||
        !studio_ascii(request.domain_chat_title) ||
        !studio_ascii(request.domain_execution) || !studio_ascii(request.domain_questions_outcome) ||
        std::strlen(identity.scenario_revision) != 64)
        return ESP_ERR_INVALID_ARG;
    for (size_t index = 0; index < 64; ++index) {
        const char value = identity.scenario_revision[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(identity.target, "cody.boot.branding") != 0 &&
        std::strcmp(identity.target, "cody.boot.birth") != 0 &&
        !CODY::studio_scene_supported(identity.target)) return ESP_ERR_NOT_SUPPORTED;
    if (identity.at_ms != 0 || identity.frame_revision != 0 || identity.rendering_mode[0] != '\0' ||
        identity.call_count != 0 || identity.question_count != 0 || identity.service_error != 0 || identity.service_outcome[0] != '\0' ||
        std::any_of(std::begin(identity.call_ids), std::end(identity.call_ids), [](uint64_t id) { return id != 0; }) ||
        std::any_of(std::begin(identity.question_ids), std::end(identity.question_ids), [](uint64_t id) { return id != 0; }) ||
        request.brightness_percent < 10 || request.brightness_percent > 100 || request.auto_rotate > 1 ||
        (request.orientation_degrees != 0 && request.orientation_degrees != 180) ||
        request.sounds_enabled > 1 || request.playback_volume > 100 ||
        request.quiet_enabled > 1 || request.quiet_edit_start > 1 ||
        request.quiet_start_minute >= 1440 || request.quiet_end_minute >= 1440 || request.quiet_draft_minute >= 1440 ||
        (request.quiet_enabled && request.quiet_start_minute == request.quiet_end_minute)) return ESP_ERR_INVALID_ARG;
    if (studio_browser_route(identity.target) == CODY::CodexBrowserRoute::Closed &&
        (identity.source_id || identity.chat_id || identity.turn_id)) return ESP_ERR_INVALID_ARG;
    for (const auto &call : request.domain_calls) {
        if (!studio_ascii(call.tool_name) || !studio_ascii(call.label) || !studio_ascii(call.category) ||
            !studio_ascii(call.state) || !studio_ascii(call.outcome)) return ESP_ERR_INVALID_ARG;
    }
    for (const auto &question : request.domain_questions)
        if (!studio_ascii(question.prompt)) return ESP_ERR_INVALID_ARG;
    auto error = CODY::host_onboarding::validate_studio_case(request);
    if (error == ESP_OK) error = CODY::host_studio_domain::validate(request);
    if (error == ESP_OK) error = CODY::host_studio_statistics::validate(request);
    return error;
}
}

// No parameters: exact request capacity for the current host-only ABI.
// Exception: No exceptions; callers must compare before passing a ctypes allocation.
CODY_HOST_STUDIO_API size_t cody_host_studio_case_size()
{
    return sizeof(CodyHostStudioCase);
}

// request: Fixed-size named case; copied into a process-lifetime mailbox before enqueue.
// Exception: Validates before any mutation, then preserves original settings/service/render failures.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_prepare_case(const CodyHostStudioCase *request)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    if (request->abi != 3 || request->size != sizeof(CodyHostStudioCase)) return ESP_ERR_INVALID_SIZE;
    auto error = validate_studio_request(*request);
    if (error != ESP_OK) return error;
    // Firmware cue drain uses the monotonic clock. A frozen Studio clock cannot
    // safely admit sound playback; use a live-clock session for audible cues.
    if (request->sounds_enabled) return ESP_ERR_NOT_SUPPORTED;
    if (!s_ui || s_studio_worker_active.load(std::memory_order_acquire) ||
        cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000)
        return ESP_ERR_INVALID_STATE;
    if (!s_studio) return ESP_ERR_NO_MEM;
    std::unique_lock<std::mutex> lock(s_studio->mutex);
    if (s_studio->pending || s_studio->running) return ESP_ERR_NOT_FINISHED;
    // Fixed storage owns the entire request. Neither worker nor frame retains a
    // borrowed ctypes buffer. Storage waits remain on this host caller thread.
    s_studio->request = *request;
    s_studio->replay = false;
    s_studio->worker = false;
    s_studio->live = false;
    const auto &value = s_studio->request;
    error = CODY::persist_companion_preference(CODY::PreferenceId::Brightness, value.brightness_percent);
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::AutoRotate, value.auto_rotate);
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::Orientation, value.orientation_degrees);
    // Fixed Studio base dependencies are host-only and applied through their
    // actual owners: standard statistics/browser routing and a known quiet policy.
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::CounterMode, 0);
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::Screensaver,
        std::strcmp(value.identity.target, "cody.home.screensaver") == 0 ? 1U : 0U);
    if (error == ESP_OK && (std::strcmp(value.identity.target, "cody.home.muse") == 0 ||
        std::strcmp(value.identity.target, "cody.home.screensaver") == 0))
        error = CODY::persist_companion_preference(CODY::PreferenceId::CharacterTheme, 0);
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::Sounds, value.sounds_enabled);
    // Playback volume is applied in the live child, where an audio codec exists.
    // Static capture deliberately has no output backend to confirm its readback.
    LUNASHELL::ClockPreferences clock{};
    if (error == ESP_OK) error = LUNASHELL::read_clock_preferences(clock);
    if (error == ESP_OK) {
        clock.quiet_hours_enabled = value.quiet_enabled != 0;
        clock.quiet_start_minute = static_cast<uint16_t>(value.quiet_start_minute);
        clock.quiet_end_minute = static_cast<uint16_t>(value.quiet_end_minute);
        error = LUNASHELL::configure_clock_preferences(clock);
    }
    if (error == ESP_OK) error = CODY::host_onboarding::publish_studio_case(value);
    if (error == ESP_OK) error = CODY::host_studio_domain::publish(value);
    if (error == ESP_OK) error = CODY::host_studio_statistics::publish(value);
    if (error != ESP_OK) return error;
    s_studio->complete = false;
    s_studio->pending = true;
    s_studio->changed.notify_all();
    if (!s_studio->changed.wait_for(lock, std::chrono::seconds(10), [] { return s_studio->complete; }))
        return ESP_ERR_TIMEOUT;
    return s_studio->error;
}

// sounds_enabled: explicit firmware sound policy; playback_volume: 0..100.
// Exception: Live playback is isolated, one-way, and only starts from a static source frame.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_start_live(uint32_t sounds_enabled, uint32_t playback_volume)
{
    if (sounds_enabled > 1 || playback_volume > 100) return ESP_ERR_INVALID_ARG;
    if (!s_ui || !s_studio || s_studio_worker_active.load(std::memory_order_acquire) ||
        cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000) return ESP_ERR_INVALID_STATE;
    std::unique_lock<std::mutex> lock(s_studio->mutex);
    if (s_studio->pending || s_studio->running) return ESP_ERR_NOT_FINISHED;
    s_studio->live_sounds = sounds_enabled;
    s_studio->live_volume = playback_volume;
    s_studio->live = true;
    s_studio->worker = false;
    s_studio->replay = false;
    s_studio->complete = false;
    s_studio->pending = true;
    s_studio->changed.notify_all();
    if (!s_studio->changed.wait_for(lock, std::chrono::seconds(10), [] { return s_studio->complete; }))
        return ESP_ERR_TIMEOUT;
    return s_studio->error;
}

// status: Host-owned live animation and startup-cue observation.
// Exception: Rejects invalid capacities, absent live runs and lost cue records.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_live_status(CodyHostStudioLiveStatus *status, size_t capacity)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (capacity != sizeof(*status)) return ESP_ERR_INVALID_SIZE;
    if (!s_studio_live_active.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    CodyHostStudioLiveStatus value{};
    value.worker_error = s_terminal_error.load(std::memory_order_acquire);
    value.animation_complete = s_studio_live_complete.load(std::memory_order_acquire) ? 1U : 0U;
    if (value.animation_complete) {
        const auto record_error = s_studio_live_record_error.load(std::memory_order_acquire);
        if (record_error != ESP_OK) return record_error;
        value.cue_count = static_cast<uint32_t>(s_studio_live_cue_count);
        value.audio_complete = 1;
        for (size_t index = 0; index < s_studio_live_cue_count; ++index) {
            auto cue = s_studio_live_cues[index];
            if (cue.request_sequence != 0) {
                const auto error = LUNASHELL_audio_get_cue_status(cue.request_sequence, cue.cue_id, &cue);
                if (error != ESP_OK) return error;
            }
            const char *name = nullptr;
            switch (cue.state) {
            case LUNASHELL_CUE_IDLE: name = "idle"; break;
            case LUNASHELL_CUE_DISPATCHED: name = "dispatched"; value.audio_complete = 0; break;
            case LUNASHELL_CUE_PLAYING: name = "playing"; value.audio_complete = 0; break;
            case LUNASHELL_CUE_COMPLETED: name = "completed"; break;
            case LUNASHELL_CUE_DROPPED_BUSY: name = "dropped-busy"; break;
            case LUNASHELL_CUE_DROPPED_COOLDOWN: name = "dropped-cooldown"; break;
            case LUNASHELL_CUE_SUPPRESSED: name = "suppressed"; break;
            case LUNASHELL_CUE_EXPIRED: name = "expired"; break;
            case LUNASHELL_CUE_FAILED: name = "failed"; break;
            case LUNASHELL_CUE_UNAVAILABLE: name = "unavailable"; break;
            default: return ESP_ERR_INVALID_STATE;
            }
            auto &out = value.cues[index];
            out.id = cue.cue_id; out.sequence = cue.request_sequence; out.error = cue.last_error;
            std::strcpy(out.state, name);
        }
    }
    *status = value;
    return ESP_OK;
}

// request: Fixed-size, printable route and revisions copied before the owner runs.
// Exception: Rejects invalid requests/origins and preserves the original gesture/frame failure.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_replay_route(const CodyHostStudioRouteRequest *request)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    if (request->abi != 1 || request->size != sizeof(*request)) return ESP_ERR_INVALID_SIZE;
    if (!studio_ascii(request->route_id) || !studio_ascii(request->origin_scenario_revision) ||
        !studio_ascii(request->destination_scenario_revision)) return ESP_ERR_INVALID_ARG;
    if (!studio_route_definition(request->route_id)) return ESP_ERR_NOT_SUPPORTED;
    const auto valid_revision = [](const char *value) {
        if (std::strlen(value) != 64) return false;
        return std::all_of(value, value + 64, [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        });
    };
    if (!valid_revision(request->origin_scenario_revision) || !valid_revision(request->destination_scenario_revision))
        return ESP_ERR_INVALID_ARG;
    if (!s_ui || s_studio_worker_active.load(std::memory_order_acquire) ||
        cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000) return ESP_ERR_INVALID_STATE;
    if (!s_studio) return ESP_ERR_NO_MEM;
    std::unique_lock<std::mutex> lock(s_studio->mutex);
    if (s_studio->pending || s_studio->running) return ESP_ERR_NOT_FINISHED;
    s_studio->route_request = *request;
    s_studio->replay = true;
    s_studio->worker = false;
    s_studio->live = false;
    s_studio->complete = false;
    s_studio->pending = true;
    s_studio->changed.notify_all();
    if (!s_studio->changed.wait_for(lock, std::chrono::seconds(10), [] { return s_studio->complete; }))
        return ESP_ERR_TIMEOUT;
    return s_studio->error;
}

// request: Isolated worker scenario identity and forward-only requested time.
// Exception: Validates before enqueue and returns the original owner failure.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_replay_worker(const CodyHostStudioWorkerRequest *request)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    if (request->abi != 1 || request->size != sizeof(*request)) return ESP_ERR_INVALID_SIZE;
    if (!studio_ascii(request->scenario_revision) || std::strlen(request->scenario_revision) != 64 ||
        !std::all_of(request->scenario_revision, request->scenario_revision + 64, [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        })) return ESP_ERR_INVALID_ARG;
    if (!s_ui || cody_host_manual_time_us.load(std::memory_order_acquire) < 1000000)
        return ESP_ERR_INVALID_STATE;
    if (!s_studio) return ESP_ERR_NO_MEM;
    std::unique_lock<std::mutex> lock(s_studio->mutex);
    if (s_studio->pending || s_studio->running) return ESP_ERR_NOT_FINISHED;
    s_studio->worker_request = *request;
    s_studio->worker = true;
    s_studio->live = false;
    s_studio->replay = false;
    s_studio->complete = false;
    s_studio->pending = true;
    s_studio->changed.notify_all();
    if (!s_studio->changed.wait_for(lock, std::chrono::seconds(30), [] { return s_studio->complete; }))
        return ESP_ERR_TIMEOUT;
    return s_studio->error;
}

// name: Capability selected by its stable host identity.
// available: Receives the compiled feature state.
// Exception: Rejects unknown capability names without writing the output.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_has_capability(const char *name, uint8_t *available)
{
    if (!name || !available) return ESP_ERR_INVALID_ARG;
    if (std::strcmp(name, "wake-word") != 0) return ESP_ERR_NOT_SUPPORTED;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    *available = 1;
#else
    *available = 0;
#endif
    return ESP_OK;
}

// target: Native compiled source animation identity, independent of Python enum values.
// index: Authored source-frame index; invalid values publish no output.
// count: Receives the number of actual source-frame intervals.
// start_ms: Receives the selected interval's inclusive starting instant.
// duration_ms: Receives the interval length in milliseconds.
// cycle_ms: Receives the full source animation duration in milliseconds.
// Exception: Returns invalid argument, unsupported target or out-of-range index before output writes.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_animation_frame(const char *target, uint32_t index,
    uint32_t *count, uint32_t *start_ms, uint32_t *duration_ms, uint32_t *cycle_ms)
{
    if (!target || !count || !start_ms || !duration_ms || !cycle_ms) return ESP_ERR_INVALID_ARG;
    uint32_t frames = 0, start = 0, duration = 0, cycle = 0;
    if (std::strcmp(target, "cody.boot.branding") == 0) {
        frames = VIBEWARE_BOOT_FRAME_COUNT;
        if (index >= frames) return ESP_ERR_INVALID_ARG;
        for (uint32_t current = 0; current < frames; ++current) {
            if (current < index) start += VIBEWARE_boot_frames[current].duration_ms;
            cycle += VIBEWARE_boot_frames[current].duration_ms;
        }
        duration = VIBEWARE_boot_frames[index].duration_ms;
    } else if (std::strcmp(target, "cody.boot.birth") == 0) {
        frames = kBirthFrameCount;
        if (index >= frames) return ESP_ERR_INVALID_ARG;
        start = index * kBirthFrameMs;
        duration = kBirthFrameMs;
        cycle = frames * kBirthFrameMs;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *count = frames;
    *start_ms = start;
    *duration_ms = duration;
    *cycle_ms = cycle;
    return ESP_OK;
}
#endif

