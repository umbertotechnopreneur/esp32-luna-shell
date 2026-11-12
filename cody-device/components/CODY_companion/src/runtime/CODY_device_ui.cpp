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
#include "CODY_cost_badge.hpp"
#include "CODY_device_view.hpp"
#include "CODY_device_effects.hpp"
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

extern "C" {
extern const uint8_t cody_surprise_portrait_start[], cody_surprise_portrait_end[];
extern const uint8_t cody_surprise_vietnam_start[], cody_surprise_vietnam_end[];
extern const uint8_t cody_surprise_italy_start[], cody_surprise_italy_end[];
extern const uint8_t cody_onboarding_welcome_start[], cody_onboarding_welcome_end[];
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
    VoucherWarning, Events, CurrentTurn, EventReply, CodexBrowser, Screensaver, Blank, Audio, Surprise, Counter, UsageAlarm, BootMenu, EventCody, ThirdParty, Prices
};
enum class Command : uint8_t { Ready = 1, Sleep, Wake, Shutdown, ShutdownFailed };
enum class ActivityPage : uint8_t { Events, CurrentTurn, Reply, Statistics };
enum class BootSection : uint8_t { Default, Events, Counters };
enum class BrandingPhase : uint8_t { Inactive, AwaitingReady, Playing };
enum class Target : uint8_t {
    None, Previous, Next,
    Interval, FirstAction, SecondAction, VoiceGesture, VoucherScroll, VoucherDismiss,
    EventsGesture, ActivityBack, EventDismiss, CodexBrowserControl, CodexBrowserBack, AudioClose,
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
    uint8_t setup_phase = 0;
    uint8_t setup_opacity = 0;
    uint8_t qr_opacity = 0;
    CODY::DamageRect heading_bounds{};
    uint64_t heading_key = 0;
    int64_t heading_started_us = 0;
    uint16_t heading_offset = 0;
    bool heading_scrolls = false;
    CODY::ModelBackground model_background = CODY::ModelBackground::None;
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

struct DeviceUi {
    CODY::DeviceModel model{};
    CODY::SceneFrame scene{};
    CODY::SceneFrame completed_scene{};
    CODY::AudioView audio_view{};
    LUNASHELL::CodyOptionsObservation audio_options{};
    esp_err_t audio_options_error = ESP_ERR_NOT_FINISHED;
    esp_err_t audio_setting_error = ESP_OK;
    bool audio_setting_pending = false;
    bool audio_refresh_needed = false;
    bool counter_mode_enabled = false;
    BootSection boot_section = BootSection::Default;
    CODY::BootMenuView boot_menu{};
    CODY::ParallaxState parallax{};
    uint8_t applied_character_theme = UINT8_MAX;
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
    uint8_t counter_page = 0;
    uint8_t counter_mask = 0;
    char counter_model_name[129]{};
    CODY::StyledTitle counter_model_title{};
    CODY::ModelBackground counter_model_background = CODY::ModelBackground::None;
    int64_t audio_refresh_retry_us = 0;
    CODY::StatusOverlay status{};
    CODY::StatusOverlay retired_status{};
    bool status_retired = false;
    bool show_battery_percentage = true;
    LUNASHELL::CodyIdleObservation idle_policy{};
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
    uint32_t first_boot_melody_sequence = 0;
    uint16_t branding_frame = 0;
    BrandingPhase branding_phase = BrandingPhase::Inactive;
    CODY::PixelAvatarFrame birth_avatar{};
    CODY::StyledTitle birth_welcome{};
    int64_t birth_due_us = 0;
    int64_t birth_rise_deadline_us = 0;
    uint32_t birth_frame = 0;
    uint16_t birth_background = 0;
    bool birth_pending = false;
    bool birth_active = false;
    bool first_boot_melody = false;
    bool boot_ding_pending = false;
    bool birth_rise_pending = false;
    bool pet_tap_cue_pending = false;

    CODY::DeviceView view{};
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
    bool event_cody_active = false;
    bool usage_alarm_active = false;
    bool usage_alarm_presented = false;
    int64_t usage_alarm_started_us = 0;
    char usage_alarm_title[24]{};
    char usage_alarm_peer[33]{};
    int64_t muse_frame_due_us = 0;
    CODY::CodexPanelView codex_panel{};
    CODY::CodexBrowser codex_browser{};
    CODY::CodexBrowserDestination browser_destination = CODY::CodexBrowserDestination::None;
    LUNASHELL::CodexInboxSnapshot inbox{};
    LUNASHELL::CodexInboxRow reply{};
    CODY::WidgetVisibility available_statistics{};
    char touched_event[33]{};
    ActivityPage activity_page = ActivityPage::Events;
    bool reply_dismiss_pending = false;
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
    bool first_boot = false;
    uint8_t tour_step = 5;
    bool setup_progress_pending = false;
    int64_t setup_retry_at_us = 0;
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

// Data is owned by the display worker and stays off its stack and the DMA heap.
alignas(DeviceUi) EXT_RAM_BSS_ATTR uint8_t s_ui_storage[sizeof(DeviceUi)]{};
DeviceUi *s_ui = nullptr;
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
    s_ui->retired.valid = false;
    s_ui->contact = Target::None;
    CODY::hide_pixel_demo(s_ui->pixel_demo);
    CODY::hide_pixel_demo(s_ui->animation_preview);
    s_ui->view.account.chat_title.line.valid = false;
    s_ui->codex_panel.chat_title.line.valid = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    s_ui->model.voice_tapped_us = 0;
#endif
    s_ui->scene_dirty = true;
    s_ui->view.countdown.reset_animation();
    CODY::cancel_onboarding_touch();
}

// ui: Serialized display owner; changes apply only after a confirmed theme observation.
// Exception: An unavailable preference keeps the last valid visual theme.
void synchronize_character_theme(DeviceUi &ui)
{
    LUNASHELL::CodyCharacterThemeObservation theme{};
    const auto error = DeviceSnapshotService::instance().readCodyCharacterThemeObservation(theme);
    if (error != ESP_OK || !theme.valid || theme.last_error != ESP_OK || theme.theme >= 8 ||
        ui.applied_character_theme == theme.theme) return;
    ui.applied_character_theme = theme.theme;
    ui.pixel_demo.visual_style.character_theme = theme.theme;
    ui.animation_preview.visual_style.character_theme = theme.theme;
    ui.pixel_demo.manual_needs_compose = true;
    ui.animation_preview.manual_needs_compose = true;
    invalidate();
}

// ui: Display owner of an optional usage alert; preserve the underlying route.
// Exception: None; the fixed default viewport is known valid.
void close_usage_alarm(DeviceUi &ui)
{
    ui.usage_alarm_active = false;
    ui.usage_alarm_presented = false;
    CODY::hide_pixel_demo(ui.animation_preview);
    ui.animation_preview.manual = false;
    ui.animation_preview.manual_playing = false;
    ui.animation_preview.manual_needs_compose = false;
    (void)CODY::set_pixel_demo_viewport(ui.animation_preview, kMuseViewport);
    invalidate();
}

// ui: Display owner of the optional event overlay; underlying navigation is retained.
// Exception: None; the fixed default viewport is known valid.
void close_event_cody(DeviceUi &ui)
{
    ui.event_cody_active = false;
    CODY::hide_pixel_demo(ui.animation_preview);
    ui.animation_preview.manual = false;
    ui.animation_preview.manual_playing = false;
    ui.animation_preview.manual_needs_compose = false;
    (void)CODY::set_pixel_demo_viewport(ui.animation_preview, kMuseViewport);
    invalidate();
}

// No parameters: choose the first enabled statistics page, with Device always available.
void select_statistics()
{
    s_ui->activity_page = ActivityPage::Statistics;
    s_ui->model.vouchers.warning_expiry = 0;
    s_ui->model.vouchers.first_row = 0;
    s_ui->model.info_page = CODY::InfoPage::System;
    s_ui->statistics_entry = true;
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
    const auto valid = CODY2D::validate_surface(strip);
    if (valid != ESP_OK) return valid;
    constexpr size_t image_bytes = size_t{466} * 466 * 2;
    if (reinterpret_cast<uintptr_t>(end) < reinterpret_cast<uintptr_t>(start) ||
        reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(start) != image_bytes ||
        static_cast<uint32_t>(row) + strip.height > 466 ||
        static_cast<uint32_t>(left) + strip.width > 466) return ESP_ERR_INVALID_SIZE;
    for (uint16_t y = 0; y < strip.height; ++y) {
        const uint8_t *source = start + ((static_cast<size_t>(row) + y) * 466 + left) * 2;
        uint16_t *destination = strip.pixels + size_t{y} * strip.stride;
        for (uint16_t x = 0; x < strip.width; ++x) {
            const uint16_t color = static_cast<uint16_t>((uint16_t{source[2 * x]} << 8) | source[2 * x + 1]);
            destination[x] = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
    return ESP_OK;
}

// art: Explicit current guide artwork; never used for QR or connection pages.
// strip: Display-owner tile receiving immutable palette-indexed RGB565 pixels.
// row: Logical top row of the tile.
// left: Logical left column of the tile.
// Exception: Returns the original surface error or rejects mismatched asset geometry.
esp_err_t draw_onboarding_art_rows(CODY::OnboardingArtwork art, CODY2D::Surface strip,
                                   uint16_t row, uint16_t left)
{
    const uint8_t *start = art == CODY::OnboardingArtwork::Welcome ? cody_onboarding_welcome_start :
        art == CODY::OnboardingArtwork::Sofia ? cody_onboarding_sofia_start : nullptr;
    const uint8_t *end = art == CODY::OnboardingArtwork::Welcome ? cody_onboarding_welcome_end :
        art == CODY::OnboardingArtwork::Sofia ? cody_onboarding_sofia_end : nullptr;
    if (start == nullptr || end == nullptr) return ESP_ERR_INVALID_ARG;
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    constexpr size_t palette_bytes = 256U * 2U;
    constexpr size_t bytes = palette_bytes + size_t{466} * 466;
    if (reinterpret_cast<uintptr_t>(end) < reinterpret_cast<uintptr_t>(start) ||
        reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(start) != bytes ||
        static_cast<uint32_t>(row) + strip.height > 466 ||
        static_cast<uint32_t>(left) + strip.width > 466) return ESP_ERR_INVALID_SIZE;
    for (uint16_t y = 0; y < strip.height; ++y) {
        const uint8_t *source = start + palette_bytes + (static_cast<size_t>(row) + y) * 466 + left;
        uint16_t *destination = strip.pixels + size_t{y} * strip.stride;
        for (uint16_t x = 0; x < strip.width; ++x) {
            const auto index = static_cast<size_t>(source[x]) * 2U;
            const uint16_t color = static_cast<uint16_t>((uint16_t{start[index]} << 8) | start[index + 1]);
            destination[x] = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
using VoicePhase = LUNASHELL::VoiceInputPhase;

// ui: Current route and its display-owned overlays. The same route is retained through standby.
// Exception: None.
bool voice_route_available(const DeviceUi &ui)
{
    return CODY::Waveshare::kDevice.voice_recording && ui.surprise_page == 0 &&
        !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.audio_open && !ui.diagnostics &&
        ui.activity_page != ActivityPage::Reply && ui.model.screensaver.phase != CODY::ScreensaverPhase::Visible &&
        !(ui.model.widgets.selected_screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::ProjectLink) &&
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
void prepare_device_voice_ribbon(DeviceUi &ui, FrameView &frame, esp_err_t motion_error)
{
    const auto &input = ui.model.voice.input;
    const bool listening = input.phase == VoicePhase::Listening;
    if (!listening && input.phase != VoicePhase::Preparing && input.phase != VoicePhase::Sending) {
        ui.voice_ribbon_energy = 0;
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const auto &retired = ui.retired;
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
        const int energy = ui.voice_ribbon_energy;
        const int64_t response_us = target > energy ? 60000 : 220000;
        // Smoothing changes artwork gain only. Unavailable input is never published as a sample.
        const auto weight = static_cast<int>(std::clamp(elapsed_us, int64_t{0}, response_us) * 256 / response_us);
        ui.voice_ribbon_energy = static_cast<uint16_t>(energy + (target - energy) * weight / 256);
    } else {
        ui.voice_ribbon_energy = 0;
    }

    CODY::prepare_voice_ribbon(now_us, static_cast<uint8_t>(ui.voice_ribbon_energy), ui.voice_ribbon);
    frame.voice_ribbon_at_us = now_us;
}
#endif

// ui: Display-owned presentation state and canonical motion projection.
// now_us: Current monotonic time for age admission and chrome visibility.
// motion_error: Original snapshot read result.
void observe_chrome_motion(DeviceUi &ui, int64_t now_us, esp_err_t motion_error)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    const auto &imu = ui.model.motion.imu;
    if (motion_error != ESP_OK || imu.health != LUNASHELL::DeviceHealth::Ready ||
        !imu.data_valid || imu.data_stale || imu.last_error != ESP_OK ||
        imu.last_success_at_us <= 0 || now_us < imu.last_success_at_us ||
        now_us - imu.last_success_at_us > 500000) {
        ui.chrome_imu_at_us = 0;
        return;
    }
    if (imu.last_success_at_us == ui.chrome_imu_at_us) return;
    for (int index = 0; index < 3; ++index) {
        if (!std::isfinite(imu.acceleration_mps2[index]) || !std::isfinite(imu.angular_velocity_dps[index])) {
            ui.chrome_imu_at_us = 0;
            return;
        }
    }
    const bool consecutive = ui.chrome_imu_at_us > 0 && imu.last_success_at_us > ui.chrome_imu_at_us &&
        imu.last_success_at_us - ui.chrome_imu_at_us <= 500000;
    float acceleration_change_squared = 0.0f;
    float angular_speed_squared = 0.0f;
    for (int index = 0; index < 3; ++index) {
        const float angular_velocity = imu.angular_velocity_dps[index];
        angular_speed_squared += angular_velocity * angular_velocity;
        if (consecutive) {
            const float change = imu.acceleration_mps2[index] - ui.chrome_acceleration[index];
            acceleration_change_squared += change * change;
        }
        ui.chrome_acceleration[index] = imu.acceleration_mps2[index];
    }
    ui.chrome_imu_at_us = imu.last_success_at_us;
    if (acceleration_change_squared >= 0.35f * 0.35f || angular_speed_squared >= 5.0f * 5.0f) {
        ui.model.navigation.interact(now_us);
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
    ui.audio_demand_requested = false;
    return;
#endif
    const int64_t now_us = esp_timer_get_time();
    if (enabled != ui.audio_demand_requested) {
        ui.audio_demand_requested = enabled;
        if (enabled) ui.audio_demand_started_us = now_us;
        s_audio_demand_desired.store(enabled, std::memory_order_release);
    }
    if (enabled && !ui.audio_worker_started && now_us >= ui.audio_worker_retry_at_us) {
        s_audio_demand_error.store(ESP_OK, std::memory_order_release);
        if (xTaskCreatePinnedToCoreWithCaps(audio_demand_worker, "CODY_audio_demand", 4096,
            nullptr, 2, nullptr, LUNASHELL::task_affinity::kCodyRenderCoreId,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
            ui.audio_worker_started = true;
        } else {
            s_audio_demand_error.store(ESP_ERR_NO_MEM, std::memory_order_release);
            ui.audio_worker_retry_at_us = now_us + 1000000;
        }
    }
}

// ui: Retains the selected counter page and its canonical dashboard projections.
// frame: Receives only quota, active-context or model information.
// now_us: Current monotonic time for freshness and countdown.
// Exception: Returns the original projection or rendering failure.
esp_err_t prepare_counter_view(DeviceUi &ui, FrameView &frame, int64_t now_us)
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
        inputs.model[0] != '\0' && std::strlen(inputs.model) < sizeof(ui.counter_model_name);
    // Keep the three destinations stable as observations arrive or expire.
    // Each destination renders its own unavailable or stale state.
    ui.counter_mask = 0x07U;
    ui.counter_model_background = CODY::ModelBackground::None;
    if (ui.counter_page == 0) {
        CODY::project_device_statistics_calendar(ui.model.dashboard, WidgetScreen::Reset, ui.model.interval);
        return CODY::prepare_device_dashboard(ui.view, frame.presentation, WidgetScreen::Reset,
            ui.model.interval, data, now_us, CODY::VoucherList{});
    }
    if (ui.counter_page == 1) {
        return CODY::prepare_device_dashboard(ui.view, frame.presentation, WidgetScreen::Context,
            ui.model.interval, data, now_us, CODY::VoucherList{});
    }
    const char *band = active && inputs.cost_state == CODY::PanelValueState::Current
        ? CODY::cost_band_text(inputs.cost_band) : "Price unavailable";
    auto result = CODY::prepare_device_message(ui.view, frame.presentation, "Model",
        model_current ? "Current model" : "Model unavailable", band, nullptr, nullptr, "Standard reference");
    if (result != ESP_OK || !model_current) return result;
    std::snprintf(ui.counter_model_name, sizeof(ui.counter_model_name), "%s", inputs.model);
    const auto length = static_cast<uint16_t>(std::strlen(ui.counter_model_name));
    ui.counter_model_background = CODY::model_background_for({ui.counter_model_name, length}, true);
    result = CODY::prepare_styled_title({ui.counter_model_name, length},
        frame.presentation.content.heading.bounds, ui.counter_model_title, 3, true);
    if (result == ESP_OK) frame.presentation.content.heading = ui.counter_model_title;
    return result;
}

// frame: Receives one visible surface and its native content.
// now_us: Monotonic time for the reset and recording views.
// motion_error: Result of the canonical motion/audio read in this update, if any.
// Exception: Returns original native preparation failures.
esp_err_t prepare_current_view(FrameView &frame, int64_t now_us, esp_err_t motion_error = ESP_ERR_NOT_FINISHED)
{
    auto &ui = *s_ui;
    if (ui.surprise_page != 0) {
        frame.layer = Layer::Surprise;
        return ESP_OK;
    }
    CODY::update_device_observations(ui.model.dashboard);
    if (ui.setup_progress_pending && now_us >= ui.setup_retry_at_us) {
        const auto &data = ui.model.dashboard;
        const auto &wifi = data.hardware.wifi;
        const auto &link = data.codex_presence.codex_link;
        const bool ready = data.hardware_error == ESP_OK && wifi.connection_state_valid && !wifi.data_stale &&
            wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0' &&
            data.codex_presence_error == ESP_OK && link.data_valid && !link.data_stale && link.last_error == ESP_OK &&
            data.codex_presence.codex_link_state == LUNASHELL::CodexLinkState::Connected &&
            data.codex_presence.codex_app_open;
        if (ready) {
            const auto progress_error = CODY::persist_setup_progress_pending(false);
            if (progress_error == ESP_OK) {
                ui.setup_progress_pending = false;
                ESP_LOGI(kTag, "Initial Wi-Fi and Codex setup confirmed");
            } else {
                // Keep the visible pending state and retry the same milestone without guessing success.
                ui.setup_retry_at_us = now_us + 5000000;
            }
        }
    }
    CODY::update_device_vouchers(ui.model);
    CODY::update_device_battery(ui.model, now_us);
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
    bool voice_busy = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
    voice_busy = voice_visible(now_us);
#else
    (void)motion_error;
#endif
    if (!ui.audio_open || ui.diagnostics) {
        set_audio_demand(false);
    }
    // Optional sound failures remain in the effects model; visual controls keep working.
    (void)CODY::update_device_effects(ui.model.effects, ui.model.dashboard.codex_presence,
        ui.model.dashboard.codex_presence_error == ESP_OK, ui.model.dashboard.hardware.wifi,
        ui.model.dashboard.hardware_error == ESP_OK, ui.model.battery_banner, voice, now_us,
        CODY::onboarding_saved_wifi_startup_offline(now_us));
    const bool voucher_attention_allowed = !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.diagnostics && !ui.audio_open && !voice_busy &&
        !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0 &&
        !(ui.model.widgets.selected_screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::ProjectLink);
    if (voucher_attention_allowed && !ui.asleep && !ui.pressed &&
        ui.model.vouchers.warning_expiry == 0) {
        const auto expiry = CODY::pending_device_voucher_warning(ui.model);
        if (expiry != 0) {
            // A visible-only notice never wakes the panel, plays audio or changes the retained dashboard route.
            ui.model.vouchers.warning_expiry = expiry;
        }
    }
    const bool saver_eligible = (ui.counter_mode_enabled
        ? (ui.mascot_home || CODY::onboarding_dashboard_active())
        : (ui.mascot_home ? ui.pixel_demo.elapsed_us >= CODY::kPixelDemoWakingUs :
           CODY::onboarding_dashboard_active())) &&
        !ui.diagnostics && !ui.animation_active && !ui.usage_alarm_active && !ui.event_cody_active && !ui.boot_menu_open &&
        !ui.audio_open && !voice_busy &&
        ui.model.vouchers.warning_expiry == 0 &&
        !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0 &&
        !(!ui.mascot_home && ui.model.widgets.selected_screen == WidgetScreen::About &&
          ui.model.info_page == CODY::InfoPage::ProjectLink);
    CODY::update_screensaver(ui.model.screensaver, saver_eligible,
        ui.screensaver_last_touch_us, now_us);
    frame.navigation_opacity = ui.model.navigation.opacity(now_us);
    if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible) {
        frame.layer = Layer::Screensaver;
        return CODY::prepare_screensaver(ui.model.screensaver, ui.view.screensaver, frame.presentation.content);
    }
    if (ui.animation_active && (ui.counter_mode_enabled || voice_busy || CODY::device_effects_require_attention(ui.model.effects) ||
        ui.model.battery_banner.sequence != 0 || ui.diagnostics)) {
        ui.animation_active = false;
        CODY::reset_muse_animation(0);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (voice_visible(now_us)) {
        frame.layer = Layer::Voice;
        const auto error = CODY::prepare_device_voice(ui.view, frame.presentation, ui.model.voice.input);
        if (error == ESP_OK) prepare_device_voice_ribbon(ui, frame, motion_error);
        return error;
    }
    if (now_us < ui.voice_boot_error_until_us) {
        frame.layer = Layer::Message;
        return CODY::prepare_device_message(ui.view, frame.presentation,
            "Voice unavailable", "Check settings and connection");
    }
#endif
    const bool critical_event = CODY::device_effects_require_attention(ui.model.effects) &&
        ui.model.effects.event.kind != LUNASHELL::CodexEventKind::ReplyReady;
    if (ui.event_cody_active && (critical_event ||
        ui.model.battery_banner.sequence != 0 || ui.diagnostics || ui.audio_open)) close_event_cody(ui);
    if (ui.boot_menu_open) {
        frame.layer = Layer::BootMenu;
        frame.arrows = true;
        const bool in_settings = ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours;
        const bool can_change_route = CODY::onboarding_dashboard_active() ||
            (in_settings && CODY::can_open_wifi_setup_from_home());
        const bool can_open_settings = in_settings ? CODY::can_open_wifi_setup_from_home() : CODY::can_open_setup_menu();
        const bool inbox_available = ui.inbox.health == LUNASHELL::DeviceHealth::Ready &&
            ui.inbox.data_valid && !ui.inbox.data_stale && ui.inbox.last_error == ESP_OK;
        return CODY::prepare_boot_menu(ui.boot_menu, CODY::SceneData::body_font, inbox_available,
            can_change_route, can_open_settings, frame.presentation.content);
    }
    if (ui.animation_active) {
        frame.layer = Layer::Animation;
        ui.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        return CODY::prepare_pixel_demo(ui.animation_preview, ui.awake_us, frame.presentation.content);
    }
    if (ui.usage_alarm_active) {
        frame.layer = Layer::UsageAlarm;
        const auto elapsed = std::max<int64_t>(0, now_us - ui.usage_alarm_started_us);
        const int64_t half_period = 350000 - ui.usage_alarm_notice.intensity * 40000;
        const bool raised = (elapsed / half_period) % 2 != 0;
        const auto amplitude = static_cast<uint16_t>(4U + 4U * ui.usage_alarm_notice.intensity);
        const CODY::LogicalRect viewport{113, static_cast<uint16_t>(154U - (raised ? amplitude : 0U)), 240, 240};
        auto error = CODY::set_pixel_demo_viewport(ui.animation_preview, viewport);
        if (error != ESP_OK) return error;
        ui.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        error = CODY::prepare_pixel_demo(ui.animation_preview, ui.awake_us, frame.presentation.content);
        if (error != ESP_OK) return error;
        return CODY::prepare_styled_title({ui.usage_alarm_title,
            static_cast<uint16_t>(std::strlen(ui.usage_alarm_title))}, {73, 85, 320, 44},
            frame.presentation.content.heading, 3, true);
    }
    if (ui.event_cody_active) {
        frame.layer = Layer::EventCody;
        const bool raised = ((now_us - ui.event_cody_started_us) / 180000) % 2 != 0;
        auto error = CODY::set_pixel_demo_viewport(ui.animation_preview,
            {113, static_cast<uint16_t>(raised ? 140U : 154U), 240, 240});
        if (error != ESP_OK) return error;
        ui.muse_frame_due_us = now_us + kMuseFramePeriodUs;
        error = CODY::prepare_pixel_demo(ui.animation_preview, ui.awake_us, frame.presentation.content);
        if (error != ESP_OK) return error;
        const char *title = ui.event_cody_kind == LUNASHELL::CodexEventKind::ChecksFailed ? "CHECKS FAILED" :
            ui.event_cody_kind == LUNASHELL::CodexEventKind::ChecksPassed ? "CHECKS PASSED" :
            ui.event_cody_kind == LUNASHELL::CodexEventKind::ReplyReady ? "REPLY READY" :
            ui.event_cody_kind == LUNASHELL::CodexEventKind::PrPending ? "PULL REQUEST" : "NOTICE";
        return CODY::prepare_styled_title({title, static_cast<uint16_t>(std::strlen(title))},
            {73, 85, 320, 44}, frame.presentation.content.heading, 3, true);
    }
    if (ui.third_party_open) {
        frame.layer = Layer::ThirdParty;
        frame.arrows = true;
        return CODY::prepare_third_party_view(ui.third_party, CODY::SceneData::body_font, frame.presentation.content);
    }
    if (ui.prices_open) {
        frame.layer = Layer::Prices;
        frame.arrows = true;
        return CODY::prepare_prices_view(ui.prices, CODY::SceneData::body_font, now_us, frame.presentation.content);
    }
    if (ui.diagnostics) {
        frame.layer = Layer::Diagnostics;
        const auto error = DeviceSnapshotService::instance().readDiagnosticsObservation(ui.model.diagnostics);
        return CODY::prepare_device_diagnostics(ui.view, frame.presentation, ui.model.diagnostics, error == ESP_OK);
    }
    if (ui.audio_open) {
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
        const int64_t admitted_at = std::max(ui.audio_demand_started_us,
            s_audio_demand_confirmed_at_us.load(std::memory_order_acquire));
        if (demand_error == ESP_OK && motion_error == ESP_OK && sample.data_valid &&
            sample.last_error == ESP_OK && sample.last_success_at_us <= admitted_at) {
            motion_error = ESP_ERR_NOT_FINISHED;
        }
        ui.audio_options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(ui.audio_options);
        return CODY::prepare_audio_view(ui.model.motion.audio,
            demand_error == ESP_OK ? motion_error : demand_error, playback_error, ui.audio_options.preferences,
            ui.audio_options_error, ui.audio_setting_pending, ui.audio_setting_error, esp_timer_get_time(),
            CODY::SceneData::title_font, CODY::SceneData::body_font, ui.audio_view, frame.presentation.content);
    }
    if (!ui.counter_mode_enabled && ui.codex_browser.route != CODY::CodexBrowserRoute::Closed &&
        ui.browser_destination == CODY::CodexBrowserDestination::None &&
        CODY::onboarding_codex_tools_active()) {
        frame.layer = Layer::CodexBrowser;
        frame.arrows = true;
        return CODY::prepare_codex_browser(ui.codex_browser, now_us, frame.presentation.content);
    }
    if (
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        !s_boot_voice_requested.load(std::memory_order_acquire) && !ui.voice_boot_route_hold &&
#endif
        (ui.boot_section == BootSection::Counters ||
        (ui.boot_section == BootSection::Default && ui.counter_mode_enabled)) && !ui.about_from_options &&
        (CODY::onboarding_dashboard_active() || ui.mascot_home)) {
        return prepare_counter_view(ui, frame, now_us);
    }
    const bool mascot_waking = ui.pixel_demo.elapsed_us < CODY::kPixelDemoWakingUs;
    if (ui.mascot_home && (mascot_waking || !CODY::device_effects_require_attention(ui.model.effects))) {
        frame.layer = Layer::Mascot;
        ui.pixel_demo.fixed_touch_bounds = true;
        const CODY::LogicalRect viewport{
            static_cast<uint16_t>(static_cast<int>(kMuseViewport.x) + ui.parallax.x),
            static_cast<uint16_t>(static_cast<int>(kMuseViewport.y) + ui.parallax.y),
            kMuseViewport.width, kMuseViewport.height};
        const auto viewport_error = CODY::set_pixel_demo_viewport(ui.pixel_demo, viewport);
        if (viewport_error != ESP_OK) return viewport_error;
        ui.muse_frame_due_us = now_us + kMuseFramePeriodUs;
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
        CODY::observe_pixel_demo_motion(ui.pixel_demo, motion, esp_timer_get_time());
        return CODY::prepare_pixel_demo(ui.pixel_demo, ui.awake_us, frame.presentation.content);
    }
    if (CODY::onboarding_dashboard_active()) {
        auto &model = ui.model;
        CODY::update_device_statistics(model.dashboard);
        const auto &turn = model.dashboard.context;
        ui.current_turn_available = CODY::current_turn_has_data(turn, now_us);
        if (ui.activity_page == ActivityPage::CurrentTurn && !ui.current_turn_available &&
            ui.browser_destination != CODY::CodexBrowserDestination::CurrentTurn) {
            ui.activity_page = ActivityPage::Events;
            invalidate();
        }
        const bool browser_child = ui.browser_destination != CODY::CodexBrowserDestination::None;
        if (ui.activity_page == ActivityPage::Events && !browser_child && !ui.current_turn_available &&
            (!ui.inbox.data_valid || ui.inbox.data_stale)) {
            ui.activity_page = ActivityPage::Statistics;
            CODY::select_widget_screen(model.widgets, WidgetScreen::Device);
            model.info_page = CODY::InfoPage::System;
            invalidate();
        }
        if (ui.activity_page == ActivityPage::Events) {
            frame.layer = Layer::Events;
            frame.arrows = browser_child;
            return CODY::prepare_codex_events(ui.codex_panel, ui.inbox, now_us, frame.presentation.content);
        }
        if (ui.activity_page == ActivityPage::CurrentTurn) {
            frame.layer = Layer::CurrentTurn;
            frame.arrows = true;
            return CODY::prepare_current_turn(ui.codex_panel, turn, now_us, frame.presentation.content);
        }
        if (ui.activity_page == ActivityPage::Reply) {
            frame.layer = Layer::EventReply;
            frame.arrows = true;
            return CODY::prepare_codex_reply(ui.codex_panel, ui.reply.title, ui.reply_dismiss_pending,
                frame.presentation.content);
        }
        ui.available_statistics = CODY::available_device_statistics(model.dashboard, model.widgets.visibility, now_us);
        if (ui.about_from_options) {
            // Opening About from Options is independent of dashboard visibility preferences.
            ui.available_statistics.screen_mask |= CODY::WidgetStateDetail::bit(WidgetScreen::About);
        }
        if (ui.statistics_entry || !CODY::widget_visible(ui.available_statistics, model.widgets.selected_screen)) {
            auto usage = ui.available_statistics;
            usage.screen_mask &= ~CODY::WidgetStateDetail::bit(WidgetScreen::About);
            const auto first = CODY::first_visible_widget(usage, CODY::WidgetScope::Statistics);
            CODY::select_widget_screen(model.widgets, first.value_or(WidgetScreen::Device));
            model.info_page = CODY::InfoPage::System;
            ui.statistics_entry = false;
            invalidate();
        }
        if (ui.model.vouchers.warning_expiry != 0 && voucher_attention_allowed) {
            frame.layer = Layer::VoucherWarning;
            frame.arrows = true;
            return CODY::prepare_device_voucher_warning(ui.view, frame.presentation, ui.model);
        }
        if (ui.model.widgets.selected_screen == WidgetScreen::About &&
            ui.model.info_page == CODY::InfoPage::ProjectLink) {
            frame.layer = Layer::AboutLink;
            frame.arrows = false;
            const auto error = CODY::prepare_about_project_link(frame.presentation.content);
            if (error == ESP_OK) return ESP_OK;
            return CODY::prepare_device_message(ui.view, frame.presentation, "Scan for more info", "QR unavailable");
        }
        frame.layer = Layer::Dashboard;
        const auto screen = model.widgets.selected_screen;
        frame.arrows = browser_child || screen == WidgetScreen::About ||
            CODY::next_visible_widget(ui.available_statistics, screen, WidgetStep::Next) != screen;
        if (screen == WidgetScreen::About) {
            // Per-field source errors are rendered locally; uptime and navigation remain usable.
            (void)CODY::read_device_info(model);
            return CODY::prepare_device_info(ui.view, frame.presentation, model);
        }
        if (screen == WidgetScreen::TokenHistory && !CODY::device_history_has_data(model.dashboard, model.interval)) {
            model.interval = CODY::TokenHistoryInterval::TwentyFourHours;
        }
        CODY::project_device_statistics_calendar(model.dashboard, screen, model.interval);
        const auto vouchers = screen == WidgetScreen::Credits ? CODY::project_device_voucher_list(model) : CODY::VoucherList{};
        const auto error = CODY::prepare_device_dashboard(ui.view, frame.presentation,
            screen, model.interval, model.dashboard, now_us, vouchers);
        if (error == ESP_OK) return ESP_OK;
        // An optional data provider must leave navigation and power controls usable.
        if (!ui.retired.statistics_unavailable) {
            ESP_LOGW(kTag, "Dashboard unavailable (%s)", esp_err_to_name(error));
        }
        frame.statistics_unavailable = true;
        frame.presentation = {};
        return CODY::prepare_device_message(ui.view, frame.presentation, "Statistics", "Data unavailable", "BOOT opens Menu");
    }
    if (ui.scene.valid) {
        frame.layer = Layer::Scene;
        frame.connection_activity_phase = CODY::onboarding_connection_activity_phase(now_us);
        return ESP_OK;
    }
    frame.layer = Layer::Message;
    return CODY::prepare_device_message(ui.view, frame.presentation, "Device", "Starting services");
}

// frame: Prepared view borrowed until synchronous DMA retirement.
// force: Power handoffs always retire a complete panel frame.
// Exception: Preserves the lease, raster, transfer or retirement error.
esp_err_t render(const FrameView &frame, bool force = false)
{
    auto &ui = *s_ui;
    if (frame.layer != Layer::Mascot) CODY::hide_pixel_demo(ui.pixel_demo);
    if (frame.layer != Layer::Animation && frame.layer != Layer::UsageAlarm && frame.layer != Layer::EventCody)
        CODY::hide_pixel_demo(ui.animation_preview);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    // Standby retains the route so an eligible dashboard can still wake by voice.
    const bool keep_voice_route = frame.layer == Layer::Blank && voice_route_available(ui);
    const bool admit_voice = voice_route_available(ui) &&
        (frame.layer == Layer::Dashboard || frame.layer == Layer::Events || frame.layer == Layer::CurrentTurn);
    if (!keep_voice_route && !admit_voice) LUNASHELL::setVoiceInputRouteAvailable(false);
#endif
    auto &old = ui.retired;
    const auto &view = frame.presentation;
    const auto revision = LUNASHELL_display_runtime_content_revision();
    const auto screen = ui.model.widgets.selected_screen;
    const bool qr_view = frame.layer == Layer::AboutLink ||
        (frame.layer == Layer::Scene && CODY::onboarding_qr_visible());
    const auto *qr_scene = frame.layer == Layer::Scene && qr_view ? ui.scene.definition : nullptr;
    if (!qr_view || ui.qr_layer != frame.layer || ui.qr_scene_identity != qr_scene) {
        ui.qr_reveal_at_us = 0;
        ui.qr_layer = qr_view ? frame.layer : Layer::Blank;
        ui.qr_scene_identity = qr_scene;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t reveal_age = ui.qr_reveal_at_us == 0 ? -1 : now_us - ui.qr_reveal_at_us;
    const uint8_t qr_opacity = reveal_age < 0 || reveal_age >= 3500000 ? 0 :
        reveal_age <= 3000000 ? 255 : static_cast<uint8_t>((3500000 - reveal_age) * 255 / 500000);
    const bool setup_visible = ui.setup_progress_pending && frame.layer != Layer::Blank &&
        frame.layer != Layer::Branding && frame.layer != Layer::Birth && frame.layer != Layer::Screensaver &&
        frame.layer != Layer::Surprise;
    const uint8_t setup_opacity = setup_visible ? (qr_view ? qr_opacity : 255) : 0;
    const uint8_t setup_phase = static_cast<uint8_t>((now_us / CODY::kSetupProgressFrameUs) % CODY::kSetupProgressSteps);
    const bool show_connection = (frame.layer == Layer::Dashboard && screen != WidgetScreen::Device &&
        !(screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::VibeWare)) ||
        frame.layer == Layer::VoucherWarning;
    const bool show_battery = frame.layer != Layer::Blank && frame.layer != Layer::Branding && frame.layer != Layer::Birth &&
        frame.layer != Layer::Screensaver && frame.layer != Layer::Mascot && frame.layer != Layer::Animation && frame.layer != Layer::UsageAlarm && frame.layer != Layer::EventCody &&
        frame.layer != Layer::Surprise &&
        (!qr_view || qr_opacity != 0);
    const auto saved_epoch = frame.layer == Layer::Dashboard && !frame.statistics_unavailable
        ? CODY::device_saved_page_epoch(ui.model.dashboard, screen) : 0;
    const auto chrome_error = CODY::prepare_device_chrome(ui.view.chrome, ui.model.dashboard, show_connection,
        show_battery ? ui.model.battery_banner : CODY::BatteryAlertNotice{}, saved_epoch);
    if (chrome_error != ESP_OK) return chrome_error;
    const auto &chrome = ui.view.chrome;
    // QR pages reserve their complete surface for the title and scannable code.
    const bool show_status = frame.layer != Layer::Blank && frame.layer != Layer::Branding &&
        frame.layer != Layer::Birth && frame.layer != Layer::Surprise && (!qr_view || qr_opacity != 0);
    if (show_status) {
        const auto status_error = CODY::prepare_status_overlay(ui.model.dashboard.hardware, ui.status, ui.show_battery_percentage);
        if (status_error != ESP_OK) return status_error;
    }
    const bool status_changed = show_status != ui.status_retired ||
        (show_status && !CODY::status_overlay_equal(ui.status, ui.retired_status));
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
    const auto model_background = frame.layer == Layer::Counter && ui.counter_page == 2
        ? ui.counter_model_background : CODY::ModelBackground::None;
    const bool full = force || !old.valid || old.display_revision != revision ||
        old.layer != frame.layer || old.screen != screen ||
        old.model_background != model_background ||
        old.allowance != allowance || old.battery_percent != battery_percent ||
        old.statistics_unavailable != frame.statistics_unavailable || old.qr_opacity != qr_opacity ||
        old.setup_opacity != setup_opacity ||
        (frame.layer == Layer::Scene && ui.scene_dirty);
    const bool draws_navigation = frame.arrows || frame.layer == Layer::Scene || show_status;
    // Menu Back remains visible and tappable instead of disappearing with idle chrome.
    const bool persistent_menu_back = frame.layer == Layer::BootMenu || frame.layer == Layer::Prices ||
        frame.layer == Layer::ThirdParty || frame.layer == Layer::CodexBrowser ||
        (frame.layer == Layer::Scene && ui.scene.valid &&
         ui.scene.definition->id == CODY::SceneId::MakeItYours);
    const uint8_t opacity = qr_view ? qr_opacity : persistent_menu_back ? 255 :
        draws_navigation ? frame.navigation_opacity : 0;
    const bool dashboard_next = frame.layer != Layer::Dashboard || screen == WidgetScreen::About ||
        CODY::next_visible_widget(ui.available_statistics, screen, WidgetStep::Next) != screen;
    const bool next_navigation = frame.arrows && dashboard_next && frame.layer != Layer::Dashboard &&
        frame.layer != Layer::AboutLink &&
        frame.layer != Layer::VoucherWarning && frame.layer != Layer::CurrentTurn &&
        frame.layer != Layer::EventReply && frame.layer != Layer::CodexBrowser && frame.layer != Layer::Events &&
        frame.layer != Layer::Audio && frame.layer != Layer::BootMenu && frame.layer != Layer::ThirdParty && frame.layer != Layer::Prices;
    const bool navigation_changed = old.navigation_opacity != opacity || old.next_navigation != next_navigation;
    const auto same_bounds = [](const CODY::DamageRect &first, const CODY::DamageRect &second) {
        return first.left == second.left && first.top == second.top &&
            first.right == second.right && first.bottom == second.bottom;
    };
    const auto &heading = (frame.layer == Layer::Scene ? ui.scene.content : view.content).heading;
    const auto &slot = heading.bounds;
    const CODY::DamageRect heading_bounds{slot.x, slot.y, static_cast<uint16_t>(slot.x + slot.width),
        static_cast<uint16_t>(slot.y + slot.height)};
    const auto key = heading_key(heading);
    const bool same_heading = key == old.heading_key && same_bounds(heading_bounds, old.heading_bounds);
    const bool same_heading_route = old.valid && old.layer == frame.layer && old.screen == screen &&
        (frame.layer != Layer::Scene || ui.completed_scene.definition == ui.scene.definition);
    const auto heading_started_us = same_heading && same_heading_route ? old.heading_started_us : ui.awake_us;
    const auto heading_width = CODY::TitleStyleDetail::width(heading.text.length, heading.scale);
    const bool heading_scrolls = heading_width > slot.width;
    const auto heading_offset = CODY::styled_title_scroll_offset(heading_width, slot.width,
        ui.awake_us - heading_started_us);
    const bool heading_changed = !same_heading || heading_offset != old.heading_offset;
    const bool content_changed = old.content_revision != view.content.revision ||
        !same_bounds(old.content_bounds, view.content.bounds) ||
        !same_bounds(old.bottom_action_bounds, view.content.bottom_action_bounds);
    uint8_t carousel_page = 0;
    uint8_t carousel_count = 0;
    uint16_t carousel_mask = 0;
    if (frame.layer == Layer::Counter) {
        carousel_mask = ui.counter_mask;
        for (uint8_t page = 0; page < 3; ++page) {
            if ((ui.counter_mask & (1U << page)) == 0) continue;
            if (page == ui.counter_page) carousel_page = carousel_count;
            ++carousel_count;
        }
    } else if (frame.layer == Layer::Dashboard && screen != WidgetScreen::About) {
        for (const auto &entry : CODY::kWidgetScreens) {
            if (!CODY::widget_visible(ui.available_statistics, entry.id)) continue;
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
    auto &pixel_view = frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody ? ui.animation_preview : ui.pixel_demo;
    const bool pixel_demo_changed = (frame.layer == Layer::Mascot || frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody) &&
        pixel_view.damage.count != 0;
    CODY::DamageRect selected_label_bounds{};
    uint16_t selected_label_offset = UINT16_MAX;
    const bool has_selected_label = frame.layer == Layer::Scene &&
        CODY::onboarding_selected_label_damage(selected_label_bounds, selected_label_offset);
    const bool selected_label_changed = has_selected_label && old.selected_label_offset != selected_label_offset;
    const bool scene_title_moved = frame.layer == Layer::Scene &&
        CODY::scene_title_scroll_offset(ui.completed_scene) != CODY::scene_title_scroll_offset(ui.scene);
    if (!full && !content_changed && !navigation_changed && !battery_changed && !connection_changed &&
        !connection_activity_changed && !voice_ribbon_changed && !pixel_demo_changed && !status_changed &&
        !selected_label_changed && !scene_title_moved &&
        !heading_changed && !carousel_changed && !setup_phase_changed) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        if (admit_voice) LUNASHELL::setVoiceInputRouteAvailable(true);
#endif
        return ESP_OK;
    }

    bool parked = false;
    esp_err_t error = LUNASHELL_display_frame_begin(kFrameBudgetMs, &parked);
    if (error != ESP_OK) {
        old.valid = false;
        return parked ? ESP_ERR_NOT_FINISHED : error;
    }

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
            if (frame.layer == Layer::Scene) include_damage(ui.scene.content.bottom_action_bounds);
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
            const auto &title = ui.scene.title.bounds;
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
            if (error != ESP_OK) break;
            // The driver admits rows by full panel width even for a narrow update.
            const size_t max_rows = (capacity / (kGeometry.width * sizeof(uint16_t))) & ~size_t{1};
            if (pixels == nullptr || max_rows == 0 || reinterpret_cast<uintptr_t>(pixels) % alignof(uint16_t) != 0) {
                error = ESP_ERR_INVALID_SIZE;
                break;
            }
            const auto rows = static_cast<uint16_t>(std::min<size_t>(max_rows, bottom - row));
            const CODY2D::Surface strip{static_cast<uint16_t *>(pixels), width, rows, width,
                static_cast<uint32_t>(capacity / sizeof(uint16_t)), CODY2D::Rgb565ByteOrder::ByteSwapped};
            const uint16_t background = frame.layer == Layer::Scene ? ui.scene.definition->background_rgb565 :
                frame.layer == Layer::Birth ? ui.birth_background : 0;
            error = CODY2D::clear(strip, background);
            if (error == ESP_OK && frame.layer == Layer::Scene) {
                const auto art = CODY::onboarding_artwork();
                if (art != CODY::OnboardingArtwork::None)
                    error = draw_onboarding_art_rows(art, strip, row, left);
            }
            if (error == ESP_OK && model_background != CODY::ModelBackground::None) {
                // The artwork stays below all model text and inside the content damage bounds.
                error = CODY::draw_model_background_rows(model_background, {177, 270, 112, 112}, strip, row, left);
            }
            if (error == ESP_OK && frame.layer == Layer::Surprise) {
                error = draw_surprise_rows(ui.surprise_page, strip, row, left);
            } else if (error == ESP_OK && frame.layer == Layer::Branding) {
                error = CODY::BootBranding::draw_rows(ui.branding, ui.branding_scratch, ui.branding_frame, strip, row);
            } else if (error == ESP_OK && frame.layer == Layer::Birth && ui.birth_frame >= kBirthMuseStartFrame) {
                error = CODY::draw_pixel_avatar_rows(ui.birth_avatar, {105, 72, 256, 256}, strip, row, left);
                if (error == ESP_OK && ui.birth_frame >= kBirthWelcomeFrame) {
                    const auto &welcome = ui.birth_welcome;
                    const auto width = CODY::TitleStyleDetail::width(welcome.text.length, welcome.scale);
                    const auto elapsed_us = (ui.birth_frame - kBirthWelcomeFrame) * kBirthFrameMs * 1000ULL;
                    error = CODY::draw_styled_title_rows(welcome, strip, row, left, welcome.text.length, false,
                        CODY::styled_title_scroll_offset(width, welcome.bounds.width, elapsed_us));
                }
            } else if (error == ESP_OK && frame.layer == Layer::Scene) {
                error = CODY::draw_scene_rows(ui.scene, strip, row, left, heading_offset);
                if (error == ESP_OK && frame.connection_activity_phase != 0) {
                    error = CODY::draw_connection_activity_rows(frame.connection_activity_phase, strip, row, left);
                }
            } else if (error == ESP_OK) {
                if (view.content.draw_rows != nullptr) {
                    error = view.content.draw_rows(view.content.state, strip, row, left);
                }
                if (error == ESP_OK) {
                    error = CODY::draw_content_heading_rows(view.content, strip, row, left, heading_offset);
                }
            }
            if (error == ESP_OK && carousel_count > 1) {
                error = CODY::draw_page_marks(carousel_page, carousel_count, strip, row, left, 435, true);
            }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
            if (error == ESP_OK && frame.voice_ribbon_at_us != 0) {
                error = CODY::draw_voice_ribbon_rows(strip, row, left, ui.voice_ribbon);
            }
#endif
            if (error == ESP_OK && (allowance != 0 || battery_percent >= 0)) {
                error = CODY::draw_device_perimeter_rows(allowance, battery_percent, strip, row, left,
                    qr_view ? qr_opacity : 255);
            }
            if (error == ESP_OK && setup_opacity != 0) {
                error = CODY::draw_setup_progress_rows(setup_phase, setup_opacity, strip, row, left);
            } else if (error == ESP_OK && qr_view && qr_opacity != 0) {
                error = CODY::draw_setup_progress_rows(0, qr_opacity, strip, row, left, false);
            }
            if (error == ESP_OK && frame.layer == Layer::Scene) {
                error = CODY::draw_onboarding_navigation(strip, row, left, opacity, background);
            }
            const bool show_back = frame.layer == Layer::Dashboard ? ui.about_from_options && screen == WidgetScreen::About &&
                    ui.model.info_page == CODY::InfoPage::System : true;
            if (error == ESP_OK && frame.arrows && show_back && row < navigation_bounds.bottom &&
                row + rows > navigation_bounds.top) {
                error = CODY::draw_lateral_navigation(false, strip, row, left, opacity);
            }
            if (error == ESP_OK && show_status) {
                error = CODY::draw_status_overlay_rows(ui.status, opacity, background, strip, row, left);
            }
            if (error == ESP_OK && frame.layer != Layer::Birth && frame.layer != Layer::Surprise && !qr_view)
                error = CODY::draw_device_chrome(chrome, strip, row, left);
            if (error == ESP_OK) {
                error = LUNASHELL_display_frame_submit_rgb565(left, row, right, row + rows, pixels);
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

    error = LUNASHELL_display_frame_end(error);
    if (error != ESP_OK) {
        old.valid = false;
        return error;
    }
    if (!old.valid || old.display_revision != frame_revision || old.layer != frame.layer || old.screen != screen ||
        (content_changed &&
         (frame.layer == Layer::CodexBrowser ||
          ui.contact == Target::FirstAction || ui.contact == Target::SecondAction ||
          ui.contact == Target::VoucherScroll || ui.contact == Target::VoucherDismiss))) {
        ui.contact = Target::None;
    }
    if (old.navigation_opacity == 255 && opacity < 255 && !qr_view) {
        ui.contact = Target::None;
        CODY::cancel_onboarding_touch();
    }
    if (chrome.battery_sequence != 0 && battery_changed) {
        (void)CODY::acknowledge_battery_alert(ui.model.battery, ui.model.battery_banner);
    }
    old = {frame.layer, screen, frame_revision, view.content.revision, chrome.battery_sequence,
        chrome.connection_revision, allowance, battery_percent, opacity, frame.connection_activity_phase,
        frame.statistics_unavailable, next_navigation, true};
    old.voucher_bounds = frame.layer == Layer::Dashboard && screen == WidgetScreen::Credits &&
        !frame.statistics_unavailable ? ui.view.account.voucher_bounds : CODY2D::Rect{};
    old.voice_ribbon_at_us = frame.voice_ribbon_at_us;
    old.content_bounds = view.content.bounds;
    old.bottom_action_bounds = view.content.bottom_action_bounds;
    old.selected_label_offset = has_selected_label ? selected_label_offset : UINT16_MAX;
    old.page_mark_mask = carousel_mask;
    old.setup_phase = setup_phase;
    old.setup_opacity = setup_opacity;
    old.qr_opacity = qr_opacity;
    old.heading_bounds = heading_bounds;
    old.heading_key = key;
    old.heading_started_us = heading_started_us;
    old.heading_offset = heading_offset;
    old.heading_scrolls = heading_scrolls;
    old.model_background = model_background;
    ui.status_retired = show_status;
    if (show_status) ui.retired_status = ui.status;
    if (frame.layer == Layer::Mascot) CODY::retire_pixel_demo(ui.pixel_demo);
    if (frame.layer == Layer::Animation || frame.layer == Layer::UsageAlarm || frame.layer == Layer::EventCody)
        CODY::retire_pixel_demo(ui.animation_preview);
    if (frame.layer == Layer::Scene) {
        ui.completed_scene = ui.scene;
        ui.scene_dirty = false;
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
    const auto layer = ui.retired.layer;
    if (layer == Layer::ThirdParty) {
        return ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous
            ? Target::ThirdPartyBack : Target::None;
    }
    if (layer == Layer::AboutLink) return Target::Previous;
    if (layer == Layer::Audio) {
        // The view is mutable during preparation; admit only the last retired revision.
        if (ui.audio_view.revision != ui.retired.content_revision) return Target::None;
        if (ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::AudioClose;
        switch (CODY::audio_adjustment_at(ui.audio_view, point)) {
        case CODY::AudioAdjustment::VolumeDown: return Target::AudioVolumeDown;
        case CODY::AudioAdjustment::VolumeUp: return Target::AudioVolumeUp;
        case CODY::AudioAdjustment::GainDown: return Target::AudioGainDown;
        case CODY::AudioAdjustment::GainUp: return Target::AudioGainUp;
        case CODY::AudioAdjustment::None: break;
        }
        return CODY::audio_close_at(ui.audio_view, point) ? Target::AudioClose : Target::None;
    }
    if (layer == Layer::CodexBrowser) {
        if (ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
        return CODY::codex_browser_control_at(ui.codex_browser, point).action != CODY::CodexBrowserAction::None
            ? Target::CodexBrowserControl : Target::None;
    }
    if ((layer == Layer::Events || layer == Layer::Dashboard) &&
        ui.browser_destination != CODY::CodexBrowserDestination::None && ui.retired.navigation_opacity == 255 &&
        CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
    if (layer == Layer::Events) return Target::EventsGesture;
    if (layer == Layer::CurrentTurn || layer == Layer::EventReply) {
        if (ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) {
            return layer == Layer::CurrentTurn &&
                ui.browser_destination == CODY::CodexBrowserDestination::CurrentTurn
                ? Target::CodexBrowserBack : Target::ActivityBack;
        }
        if (layer == Layer::EventReply && !ui.reply_dismiss_pending &&
            point.x >= 113 && point.x < 353 && point.y >= 330 && point.y < 386) return Target::EventDismiss;
        return Target::None;
    }
    if (layer == Layer::VoucherWarning) {
        if (ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::VoucherDismiss;
        return CODY::device_action_at(ui.view, point) == 1 ? Target::VoucherDismiss : Target::None;
    }
    if (ui.retired.navigation_opacity == 255 && layer == Layer::Dashboard && ui.about_from_options &&
        ui.retired.screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::System &&
        CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::Previous;
    if (layer == Layer::Dashboard && ui.retired.statistics_unavailable) return Target::None;
    if (layer == Layer::Dashboard && ui.retired.screen == WidgetScreen::Credits) {
        const auto &bounds = ui.retired.voucher_bounds;
        if (point.x >= bounds.x && point.x < bounds.x + bounds.width &&
            point.y >= bounds.y && point.y < bounds.y + bounds.height) return Target::VoucherScroll;
    }
    if (layer == Layer::Dashboard && ui.retired.screen == WidgetScreen::TokenHistory &&
        CODY::hit_token_history_interval(ui.view.history, point)) return Target::Interval;
    if (layer == Layer::Voice || layer == Layer::Diagnostics ||
        (layer == Layer::Dashboard && ui.retired.screen == WidgetScreen::Device)) {
        switch (CODY::device_action_at(ui.view, point)) {
        case 1: return Target::FirstAction;
        case 2: return Target::SecondAction;
        default: break;
        }
    }
    return Target::None;
}

// ui: Owns the retired Events view, press identity and local navigation.
// Exception: Optional selection errors are logged without stopping the display worker.
void activate_events_gesture(DeviceUi &ui)
{
    const int dx = ui.activity_touch_last.x - ui.activity_touch_start.x;
    const int dy = ui.activity_touch_last.y - ui.activity_touch_start.y;
    if (std::abs(dx) >= 44 && std::abs(dx) > std::abs(dy)) {
        if (dx < 0) {
            if (!ui.current_turn_available) return;
            ui.activity_page = ActivityPage::CurrentTurn;
        } else {
            select_statistics();
        }
        invalidate();
        return;
    }
    const auto &page = ui.codex_panel.page;
    if (!ui.codex_panel.inbox_current) return;
    if (std::abs(dy) >= 34 && std::abs(dy) > std::abs(dx)) {
        const int maximum = std::max(0, int(page.total) - LUNASHELL::kCodexInboxRows);
        const auto offset = static_cast<uint8_t>(std::clamp(int(page.offset) + (dy < 0 ? 1 : -1), 0, maximum));
        const auto error = LUNASHELL_codex_inbox_page(offset);
        if (error != ESP_OK) ESP_LOGW(kTag, "Events page request failed (%s)", esp_err_to_name(error));
        return;
    }
    if (std::abs(dx) > 16 || std::abs(dy) > 16 || ui.touched_event[0] == '\0') return;
    const int row = CODY::codex_event_row_at(ui.activity_touch_last);
    if (row < 0 || row >= page.count || std::strcmp(page.rows[row].request_id, ui.touched_event) != 0) return;
    const auto &event = page.rows[row];
    if (event.kind == LUNASHELL::CodexEventKind::Question) {
        const auto error = CODY::open_onboarding_inbox_question(event.request_id);
        if (error != ESP_OK) ESP_LOGW(kTag, "Events question open failed (%s)", esp_err_to_name(error));
    } else if (event.kind == LUNASHELL::CodexEventKind::ReplyReady && !ui.reply_dismiss_pending) {
        ui.reply = event;
        ui.activity_page = ActivityPage::Reply;
        invalidate();
    }
}

// target: Released control admitted on the same complete frame.
// Exception: Returns a real voice transport admission error.
esp_err_t activate(Target target)
{
    auto &ui = *s_ui;
    if (target == Target::ThirdPartyBack) {
        ui.third_party_open = false;
        invalidate();
        return ESP_OK;
    }
    const auto layer = ui.retired.layer;
    if (target == Target::CodexBrowserBack) {
        if (layer == Layer::CodexBrowser) {
            CODY::back_codex_browser(ui.codex_browser);
        } else {
            const auto error = CODY::return_to_options_tools(ui.scene);
            if (error != ESP_OK) return error;
            if (!CODY::onboarding_codex_tools_active()) return ESP_OK;
            const auto origin = ui.browser_destination;
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            if ((origin == CODY::CodexBrowserDestination::Events &&
                 ui.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions) ||
                (origin == CODY::CodexBrowserDestination::CurrentTurn &&
                 ui.codex_browser.route == CODY::CodexBrowserRoute::Turns)) {
                ui.codex_browser.refresh_at_us = 0;
            } else {
                CODY::open_codex_browser(ui.codex_browser);
            }
        }
        invalidate();
    } else if (target == Target::CodexBrowserControl && layer == Layer::CodexBrowser) {
        const auto start = CODY::codex_browser_control_at(ui.codex_browser, ui.activity_touch_start);
        const auto end = CODY::codex_browser_control_at(ui.codex_browser, ui.activity_touch_last);
        if (start.action == CODY::CodexBrowserAction::None || !(start == end)) return ESP_OK;
        const bool questions_to_events = ui.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions &&
            start.action == CODY::CodexBrowserAction::Events;
        const auto destination = CODY::activate_codex_browser(ui.codex_browser, start);
        if (destination != CODY::CodexBrowserDestination::None) {
            const auto error = CODY::open_device_dashboard(ui.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) return error;
            ui.browser_destination = destination;
            if (destination == CODY::CodexBrowserDestination::Statistics) {
                select_statistics();
            } else if (destination == CODY::CodexBrowserDestination::CurrentTurn) {
                ui.activity_page = ActivityPage::CurrentTurn;
                ui.activity_navigation_sound_pending = true;
            } else {
                ui.activity_page = ActivityPage::Events;
                ui.questions_navigation_sound_pending = questions_to_events;
            }
        }
        invalidate();
    } else if (target == Target::EventsGesture && layer == Layer::Events) {
        activate_events_gesture(ui);
    } else if (target == Target::ActivityBack) {
        ui.activity_page = ActivityPage::Events;
        invalidate();
    } else if (target == Target::EventDismiss && layer == Layer::EventReply) {
        const auto error = LUNASHELL_codex_inbox_dismiss(ui.reply.request_id);
        if (error == ESP_OK) ui.reply_dismiss_pending = true;
        else ESP_LOGW(kTag, "Events reply dismissal failed (%s)", esp_err_to_name(error));
    } else if (layer == Layer::VoucherWarning) {
        if (target == Target::VoucherDismiss) {
            CODY::dismiss_device_voucher_warning(ui.model);
            invalidate();
        }
    } else if (target == Target::VoucherScroll) {
        const int dx = ui.voucher_touch_last.x - ui.voucher_touch_start.x;
        const int dy = ui.voucher_touch_last.y - ui.voucher_touch_start.y;
        if (std::abs(dy) >= 32 && std::abs(dy) > std::abs(dx)) {
            CODY::step_device_voucher(ui.model.vouchers, dy < 0 ? WidgetStep::Next : WidgetStep::Previous);
        }
    } else if (layer == Layer::Audio) {
        if (target == Target::AudioClose) {
            ui.audio_open = false;
            invalidate();
        } else if (target == Target::AudioVolumeDown || target == Target::AudioVolumeUp ||
                   target == Target::AudioGainDown || target == Target::AudioGainUp) {
            // A control may have become disabled while the finger was held.
            if (target_at(ui.activity_touch_last) != target) return ESP_OK;
            const bool volume = target == Target::AudioVolumeDown || target == Target::AudioVolumeUp;
            const bool increase = target == Target::AudioVolumeUp || target == Target::AudioGainUp;
            const uint32_t current = volume ? ui.audio_view.playback_volume : ui.audio_view.microphone_gain;
            const uint32_t step = volume ? 10U : 3U;
            const uint32_t maximum = volume ? 100U : 30U;
            const uint32_t next = increase ? std::min(maximum, current + step) :
                current < step ? 0U : current - step;
            const auto preference = volume ? CODY::PreferenceId::PlaybackVolume : CODY::PreferenceId::MicrophoneGain;
            const auto error = CODY::request_audio_preference(preference, next);
            ui.audio_setting_error = error;
            ui.audio_setting_pending = error == ESP_OK;
            if (error != ESP_OK) ESP_LOGW(kTag, "Audio setting admission failed (%s)", esp_err_to_name(error));
            invalidate();
        }
    } else if (layer == Layer::AboutLink) {
        ui.model.info_page = CODY::InfoPage::VibeWare;
        invalidate();
    } else if (target == Target::Previous || target == Target::Next) {
        if (target == Target::Previous && ui.about_from_options &&
            ui.model.widgets.selected_screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::System) {
            if (ui.about_from_boot_menu) {
                if (ui.about_return_settings) {
                    const auto error = CODY::return_to_options_page(ui.scene, ui.about_return_settings_page);
                    if (error != ESP_OK) return error;
                }
                ui.about_from_options = false;
                ui.about_from_boot_menu = false;
                ui.third_party_open = ui.about_return_third_party;
                ui.prices_open = ui.about_return_prices;
                ui.boot_section = ui.about_return_section;
                ui.mascot_home = ui.about_return_home;
                ui.activity_page = ui.about_return_activity;
                ui.model.widgets.selected_screen = ui.about_return_screen;
                ui.model.info_page = ui.about_return_info;
                ui.boot_menu_open = true;
                invalidate();
                return ESP_OK;
            }
            const auto error = CODY::return_to_options_device(ui.scene);
            if (error != ESP_OK) return error;
            ui.about_from_options = false;
            invalidate();
            return ESP_OK;
        }
        const auto step = target == Target::Previous ? WidgetStep::Previous : WidgetStep::Next;
        const bool info_changed = ui.model.widgets.selected_screen == WidgetScreen::About &&
            CODY::step_info_page(ui.model.info_page, step);
        if (!info_changed) {
            const auto page = CODY::next_visible_widget(ui.available_statistics, ui.model.widgets.selected_screen, step);
            if (page) {
                CODY::select_widget_screen(ui.model.widgets, *page);
                ui.model.info_page = CODY::InfoPage::System;
            }
        }
        invalidate();
    } else if (target == Target::Interval) {
        using Interval = CODY::TokenHistoryInterval;
        const auto previous = ui.model.interval;
        do {
            switch (ui.model.interval) {
            case Interval::OneHour: ui.model.interval = Interval::FiveHours; break;
            case Interval::FiveHours: ui.model.interval = Interval::TwentyFourHours; break;
            case Interval::TwentyFourHours: ui.model.interval = Interval::OneHour; break;
            }
        } while (ui.model.interval != previous && !CODY::device_history_has_data(ui.model.dashboard, ui.model.interval));
    } else if (layer == Layer::Diagnostics) {
        ui.diagnostics = false;
        invalidate();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    else if (layer == Layer::Voice) {
        bool accepted = false;
        const auto phase = ui.model.voice.input.phase;
        const bool send = target == Target::FirstAction && phase == VoicePhase::Listening;
        const auto error = send ? LUNASHELL::finishVoiceInput(ui.presented_voice_session, accepted) :
            LUNASHELL::cancelVoiceInput(ui.presented_voice_session, accepted);
        if (error != ESP_OK) ESP_LOGW(kTag, "Voice action failed (%s)", esp_err_to_name(error));
    }
#endif
    return ESP_OK;
}

// ui: Retained audio state owned by the display worker.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_touch_cue(DeviceUi &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    if (ui.retired.layer == Layer::Scene && CODY::onboarding_wifi_keyboard_active()) {
        CODY::play_device_key_cue(ui.model.effects, voice);
    } else {
        CODY::play_device_tap_cue(ui.model.effects, voice);
    }
}

// ui: Display-owned effects and the current canonical voice observation.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_preference_cue(DeviceUi &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_preference_cue(ui.model.effects, voice);
}

// ui: Display-owned effects and current voice state for a confirmed bookmark.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_bookmark_cue(DeviceUi &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_bookmark_cue(ui.model.effects, voice);
}

// ui: Display-owned effects for navigation to the authenticated events list.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_questions_cue(DeviceUi &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_questions_cue(ui.model.effects, voice);
}

// ui: Display-owned effects for navigation to the PC-followed activity.
// Exception: None; optional audio outcomes remain with the effects owner.
void play_activity_cue(DeviceUi &ui)
{
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_activity_cue(ui.model.effects, voice);
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
        ui.pressed = false;
        ui.home_swipe_active = false;
        ui.qr_touch_active = false;
        invalidate();
        return ESP_OK;
    }
    if (read != ESP_OK) return read;
    const bool was_pressed = ui.pressed;
    const bool pressed_edge = sample.pressed && !was_pressed;
    ui.pressed = sample.pressed;
    // The contact can reveal chrome even while a new route or orientation
    // is awaiting its first frame. Action dispatch still uses the retired frame.
    if (sample.pressed || was_pressed) {
        const int64_t touch_at_us = esp_timer_get_time();
        ui.model.navigation.interact(touch_at_us);
        ui.screensaver_last_touch_us = touch_at_us;
        ui.idle_applied = false;
    }
    const CODY2D::Point point{static_cast<int16_t>(sample.x), static_cast<int16_t>(sample.y)};
    if (ui.event_cody_active) {
        if (pressed_edge && ui.retired.valid && ui.retired.layer == Layer::EventCody &&
            ui.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            close_event_cody(ui);
            LUNASHELL_power_manager_note_activity();
        }
        return ESP_OK;
    }
    if (ui.boot_menu_open) {
        if (!ui.retired.valid || ui.retired.layer != Layer::BootMenu ||
            ui.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.retired.content_revision != ui.boot_menu.revision) {
            ui.boot_menu_contact = CODY::BootMenuAction::None;
            return ESP_OK;
        }
        if (pressed_edge) {
            ui.boot_menu_start = ui.boot_menu_last = point;
            ui.boot_menu_revision = ui.retired.display_revision;
            ui.boot_menu_contact = CODY::boot_menu_action_at(ui.boot_menu, point);
            LUNASHELL_power_manager_note_activity();
        }
        if (sample.pressed) { ui.boot_menu_last = point; return ESP_OK; }
        if (!was_pressed || ui.boot_menu_revision != ui.retired.display_revision) return ESP_OK;
        const int dx = ui.boot_menu_last.x - ui.boot_menu_start.x;
        const int dy = ui.boot_menu_last.y - ui.boot_menu_start.y;
        const bool back = ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(ui.boot_menu_start) == CODY::LateralNavigation::Previous &&
            CODY::lateral_navigation_at(ui.boot_menu_last) == CODY::LateralNavigation::Previous;
        if (back || (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3)) {
            ui.boot_menu_open = false;
            ui.boot_menu_contact = CODY::BootMenuAction::None;
            invalidate();
            return ESP_OK;
        }
        const auto action = ui.boot_menu_contact;
        ui.boot_menu_contact = CODY::BootMenuAction::None;
        if (action == CODY::BootMenuAction::None || CODY::boot_menu_action_at(ui.boot_menu, ui.boot_menu_last) != action) return ESP_OK;
        if (action == CODY::BootMenuAction::Settings) {
            ui.boot_menu_open = false;
            s_options_requested.store(true, std::memory_order_release);
            invalidate();
            return ESP_OK;
        }
        if (action == CODY::BootMenuAction::About) {
            ui.about_return_third_party = ui.third_party_open;
            ui.about_return_prices = ui.prices_open;
            ui.about_return_settings = ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours;
            if (ui.about_return_settings) ui.about_return_settings_page = CODY::onboarding_options_page_token();
            ui.about_return_home = ui.mascot_home;
            ui.about_return_section = ui.boot_section;
            ui.about_return_activity = ui.activity_page;
            ui.about_return_screen = ui.model.widgets.selected_screen;
            ui.about_return_info = ui.model.info_page;
        }
        if (!CODY::onboarding_dashboard_active()) {
            const auto error = CODY::open_device_dashboard(ui.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) return error;
        }
        ui.boot_menu_open = false;
        ui.third_party_open = false;
        ui.prices_open = false;
        ui.audio_open = false;
        ui.diagnostics = false;
        ui.surprise_page = 0;
        ui.about_from_options = false;
        ui.mascot_home = false;
        ui.codex_browser.route = CODY::CodexBrowserRoute::Closed;
        ui.browser_destination = CODY::CodexBrowserDestination::None;
        if (action == CODY::BootMenuAction::About) {
            ui.about_from_boot_menu = true;
            ui.about_from_options = true;
            ui.activity_page = ActivityPage::Statistics;
            ui.statistics_entry = false;
            ui.model.widgets.selected_screen = WidgetScreen::About;
            ui.model.info_page = CODY::InfoPage::System;
        } else if (action == CODY::BootMenuAction::Events) {
            ui.boot_section = BootSection::Events;
            ui.activity_page = ActivityPage::Events;
        } else {
            ui.boot_section = BootSection::Counters;
            ui.counter_page = 0;
            ui.activity_page = ActivityPage::Statistics;
        }
        invalidate();
        return ESP_OK;
    }
    if (ui.prices_open) {
        if (!ui.retired.valid || ui.retired.layer != Layer::Prices ||
            ui.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.retired.content_revision != ui.prices.revision) {
            ui.prices_touch_row = -1;
            return ESP_OK;
        }
        if (pressed_edge) {
            ui.prices_start = ui.prices_last = point;
            ui.prices_touch_revision = ui.retired.display_revision;
            ui.prices_touch_row = -1;
            if (!ui.prices.detail) {
                for (size_t i = 0; i < ui.prices.row_count; ++i)
                    if (CODY2D::capsule_contains(ui.prices.rows[i], point)) ui.prices_touch_row = static_cast<int8_t>(i);
            }
            LUNASHELL_power_manager_note_activity();
        }
        if (sample.pressed) { ui.prices_last = point; return ESP_OK; }
        if (!was_pressed || ui.prices_touch_revision != ui.retired.display_revision) return ESP_OK;
        const int dx = ui.prices_last.x - ui.prices_start.x;
        const int dy = ui.prices_last.y - ui.prices_start.y;
        const bool back = ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(ui.prices_start) == CODY::LateralNavigation::Previous &&
            CODY::lateral_navigation_at(ui.prices_last) == CODY::LateralNavigation::Previous;
        if (back || (dx >= 48 && dx * 2 > std::abs(dy) * 3 && ui.prices.detail)) {
            if (!CODY::prices_back(ui.prices)) ui.prices_open = false;
            invalidate();
        } else if (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3) {
            if (CODY::prices_move_page(ui.prices, dx < 0 ? 1 : -1)) invalidate();
        } else if (ui.prices_touch_row >= 0 &&
            CODY2D::capsule_contains(ui.prices.rows[static_cast<size_t>(ui.prices_touch_row)], ui.prices_last)) {
            if (CODY::prices_select_at(ui.prices, ui.prices_last)) invalidate();
        }
        ui.prices_touch_row = -1;
        return ESP_OK;
    }
    if (ui.usage_alarm_active) {
        if (pressed_edge && ui.retired.valid && ui.retired.layer == Layer::UsageAlarm &&
            ui.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            close_usage_alarm(ui);
            LUNASHELL_power_manager_note_activity();
        }
        return ESP_OK;
    }
    if (ui.surprise_page != 0) {
        if (pressed_edge && ui.retired.valid && ui.retired.layer == Layer::Surprise &&
            ui.retired.display_revision == LUNASHELL_display_runtime_content_revision()) {
            LUNASHELL_power_manager_note_activity();
            if (ui.surprise_page < 3) ++ui.surprise_page;
            else ui.surprise_page = 0;
            invalidate();
        }
        return ESP_OK;
    }
    if (pressed_edge && ui.retired.valid && ui.retired.layer != Layer::Screensaver && point.y >= 0 && point.y < 73) {
        ui.home_swipe_active = true;
        ui.home_swipe_valid = true;
        ui.home_swipe_start = ui.home_swipe_last = point;
        ui.home_swipe_revision = ui.retired.display_revision;
        ui.home_swipe_rotation = ui.model.motion.rendering_rotation_degrees;
        ui.contact = Target::None;
        CODY::cancel_onboarding_touch();
    }
    if (ui.home_swipe_active) {
        if (!ui.retired.valid || ui.retired.display_revision != ui.home_swipe_revision ||
            ui.retired.display_revision != LUNASHELL_display_runtime_content_revision() ||
            ui.home_swipe_rotation != ui.model.motion.rendering_rotation_degrees) ui.home_swipe_valid = false;
        if (sample.pressed) ui.home_swipe_last = point;
        else {
            ui.home_swipe_active = false;
            const int dx = ui.home_swipe_last.x - ui.home_swipe_start.x;
            const int dy = ui.home_swipe_last.y - ui.home_swipe_start.y;
            if (ui.home_swipe_valid && dy >= 60 && dy * 2 > std::abs(dx) * 3) {
                LUNASHELL_power_manager_note_activity();
                queue_home();
            } else if (ui.home_swipe_valid && dx * dx + dy * dy <= 12 * 12 &&
                (ui.retired.layer == Layer::AboutLink ||
                 (ui.retired.layer == Layer::Scene && CODY::onboarding_qr_visible()))) {
                ui.qr_reveal_at_us = esp_timer_get_time();
            }
        }
        return ESP_OK;
    }
    if (pressed_edge && ui.animation_active) {
        ui.animation_active = false;
        CODY::reset_muse_animation(0);
        ui.contact = Target::None;
        LUNASHELL_power_manager_note_activity();
        invalidate();
        return ESP_OK;
    }
    if (!ui.retired.valid || ui.retired.display_revision != LUNASHELL_display_runtime_content_revision()) {
        ui.contact = Target::None;
        ui.qr_touch_active = false;
        ui.pixel_demo.touch_candidate = false;
        return ESP_OK;
    }
    if (pressed_edge) {
        LUNASHELL_power_manager_note_activity();
        if (ui.retired.layer != Layer::Mascot) play_touch_cue(ui);
    }
    if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible || ui.retired.layer == Layer::Screensaver) {
        CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
        // The wake touch owns no underlying target, including its eventual release.
        ui.contact = Target::None;
        invalidate();
        return ESP_OK;
    }
    const bool qr_touch = ui.retired.layer == Layer::AboutLink ||
        (ui.retired.layer == Layer::Scene && CODY::onboarding_qr_visible());
    if (qr_touch) {
        if (pressed_edge) {
            ui.qr_touch_active = true;
            ui.qr_touch_start = ui.qr_touch_last = point;
            ui.qr_touch_revision = ui.retired.display_revision;
            ui.qr_touch_rotation = ui.model.motion.rendering_rotation_degrees;
            ui.qr_touch_scene = ui.retired.layer == Layer::Scene ? ui.scene.definition : nullptr;
        }
        if (ui.qr_touch_active && sample.pressed) ui.qr_touch_last = point;
        if (ui.qr_touch_active && (!ui.retired.valid || ui.qr_touch_revision != ui.retired.display_revision ||
            ui.qr_touch_rotation != ui.model.motion.rendering_rotation_degrees ||
            (ui.retired.layer == Layer::Scene && ui.qr_touch_scene != ui.scene.definition))) {
            ui.qr_touch_active = false;
            CODY::cancel_onboarding_touch();
        }
        if (!sample.pressed && ui.qr_touch_active) {
            ui.qr_touch_active = false;
            const int dx = ui.qr_touch_last.x - ui.qr_touch_start.x;
            const int dy = ui.qr_touch_last.y - ui.qr_touch_start.y;
            if (dx * dx + dy * dy <= 12 * 12) ui.qr_reveal_at_us = esp_timer_get_time();
            if (ui.retired.layer == Layer::AboutLink) {
                return dx >= 48 && dx * 2 > std::abs(dy) * 3 ? activate(Target::Previous) : ESP_OK;
            }
        }
        if (ui.retired.layer == Layer::AboutLink) return ESP_OK;
        // The scene owns the right-swipe and closes any pairing session before leaving.
    }
    if (ui.retired.layer == Layer::Mascot) {
        if (CODY::observe_pixel_demo_touch(ui.pixel_demo, point, sample.pressed, pressed_edge,
                sample.sample_timestamp_us, ui.retired.display_revision)) {
            const auto pet_error = CODY::record_pet_tap(sample.sample_timestamp_us, ui.model.dashboard.pet);
            if (pet_error != ESP_OK) {
                // The owner logs and publishes rejection. Do not acknowledge it
                // with a reaction or stop the unrelated display worker.
                return ESP_OK;
            }
            CODY::start_pixel_demo_reaction(ui.pixel_demo);
            ui.pet_tap_cue_pending = true;
        }
        return ESP_OK;
    }
    if (ui.retired.layer == Layer::Scene) {
        if (ui.retired.navigation_opacity != 255 && CODY::onboarding_lateral_control_at(point)) {
            CODY::cancel_onboarding_touch();
            return ESP_OK;
        }
        bool consumed = false;
        const auto *previous_definition = ui.scene.definition;
        const auto previous_content = ui.scene.content.revision;
        const auto previous_controls = ui.scene.content.control_revision;
        const auto previous_text = ui.scene.content.text_generation;
        const auto previous_visible = ui.scene.visible_characters;
        const auto previous_body = ui.scene.content.visible_characters;
        auto error = CODY::observe_onboarding_touch_activity(point, sample.pressed, pressed_edge, ui.awake_us);
        if (error == ESP_OK) {
            error = CODY::handle_onboarding_touch(ui.scene, point, sample.pressed, pressed_edge,
                ui.model.motion.rendering_rotation_degrees, consumed);
        }
        // Consumed means the route owns the touch, not that pixels changed.
        // A held settings row must not force a full redraw on every touch report.
        ui.scene_dirty = ui.scene_dirty || ui.scene.definition != previous_definition ||
            ui.scene.content.revision != previous_content || ui.scene.content.control_revision != previous_controls ||
            ui.scene.content.text_generation != previous_text || ui.scene.visible_characters != previous_visible ||
            ui.scene.content.visible_characters != previous_body;
        return error;
    }
    if (sample.pressed) {
        const auto target = target_at(point);
        if (pressed_edge) {
            ui.contact = target;
            ui.activity_touch_start = point;
            ui.touched_event[0] = '\0';
            if (target == Target::EventsGesture && ui.codex_panel.inbox_current) {
                const int row = CODY::codex_event_row_at(point);
                if (row >= 0 && row < ui.codex_panel.page.count) {
                    std::memcpy(ui.touched_event, ui.codex_panel.page.rows[row].request_id, sizeof(ui.touched_event));
                }
            }
        }
        ui.activity_touch_last = point;
        if (ui.retired.layer == Layer::AboutLink && ui.contact == Target::Previous) {
            const int dx = point.x - ui.activity_touch_start.x;
            const int dy = point.y - ui.activity_touch_start.y;
            if (dx * dx + dy * dy > 12 * 12) ui.contact = Target::None;
        }
        if (ui.contact == Target::VoucherScroll) {
            if (pressed_edge) ui.voucher_touch_start = point;
            ui.voucher_touch_last = point;
        } else if (ui.contact != target) {
            ui.contact = Target::None;
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        if (pressed_edge && target == Target::VoiceGesture) {
            ui.model.voice_touch_start = point;
            ui.model.voice_touch_started_us = esp_timer_get_time();
        } else if (ui.contact == Target::VoiceGesture &&
            (std::abs(point.x - ui.model.voice_touch_start.x) > 16 ||
             std::abs(point.y - ui.model.voice_touch_start.y) > 16)) {
            ui.contact = Target::None;
            ui.model.voice_tapped_us = 0;
        }
#endif
        return ESP_OK;
    }
    const auto contact = ui.contact;
    ui.contact = Target::None;
    if (was_pressed) {
        const int dx = ui.activity_touch_last.x - ui.activity_touch_start.x;
        const int dy = ui.activity_touch_last.y - ui.activity_touch_start.y;
        if (std::abs(dx) >= 48 && std::abs(dx) * 2 > std::abs(dy) * 3) {
            const bool previous = dx > 0;
            if (ui.retired.layer == Layer::Counter) {
                for (uint8_t visited = 0; visited < 3; ++visited) {
                    ui.counter_page = static_cast<uint8_t>((ui.counter_page + (previous ? 2U : 1U)) % 3U);
                    if ((ui.counter_mask & (1U << ui.counter_page)) != 0) break;
                }
                invalidate();
                return ESP_OK;
            }
            if (ui.retired.layer == Layer::CodexBrowser) {
                if (previous && !ui.codex_browser.page.previous) return activate(Target::CodexBrowserBack);
                const auto action = previous ? CODY::CodexBrowserAction::PreviousPage : CODY::CodexBrowserAction::NextPage;
                (void)CODY::activate_codex_browser(ui.codex_browser, {action});
                invalidate();
                return ESP_OK;
            }
            if (ui.retired.layer == Layer::Dashboard && contact != Target::VoucherScroll) {
                const auto first = CODY::first_visible_widget(ui.available_statistics, CODY::WidgetScope::Statistics)
                    .value_or(WidgetScreen::Device);
                if (previous && ui.browser_destination != CODY::CodexBrowserDestination::None &&
                    ui.retired.screen == first) return activate(Target::CodexBrowserBack);
                if (previous && ui.retired.screen == first && !ui.about_from_options &&
                    (ui.current_turn_available || (ui.inbox.data_valid && !ui.inbox.data_stale))) {
                    ui.activity_page = ActivityPage::Events;
                    invalidate();
                    return ESP_OK;
                }
                return activate(previous ? Target::Previous : Target::Next);
            }
            if (previous && ui.retired.layer == Layer::AboutLink)
                return activate(Target::Previous);
            if (previous && ui.retired.layer == Layer::Audio) return activate(Target::AudioClose);
            if (previous && ui.retired.layer == Layer::ThirdParty) return activate(Target::ThirdPartyBack);
            if (previous && (ui.retired.layer == Layer::CurrentTurn || ui.retired.layer == Layer::EventReply)) {
                return activate(ui.retired.layer == Layer::CurrentTurn &&
                    ui.browser_destination == CODY::CodexBrowserDestination::CurrentTurn
                    ? Target::CodexBrowserBack : Target::ActivityBack);
            }
        }
    }
    if (was_pressed && ui.retired.layer == Layer::Dashboard &&
        (contact == Target::None || contact == Target::VoiceGesture)) {
        const int dx = ui.activity_touch_last.x - ui.activity_touch_start.x;
        const int dy = ui.activity_touch_last.y - ui.activity_touch_start.y;
        if (dx >= 44 && dx > std::abs(dy) && (ui.current_turn_available ||
            (ui.inbox.data_valid && !ui.inbox.data_stale))) {
            ui.activity_page = ActivityPage::Events;
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
        ui.audio_setting_pending = false;
        // A reconciliation returns its first error, so a permanent failure on one
        // control must not hide a transient busy result on the other control.
        LUNASHELL::CodyOptionsObservation audio_observed{};
        const auto observation_error = DeviceSnapshotService::instance().readCodyOptionsObservation(audio_observed);
        const bool preference_busy = observation_error == ESP_OK &&
            (audio_observed.preferences.playback_volume_error == ESP_ERR_NOT_FINISHED ||
             audio_observed.preferences.microphone_gain_error == ESP_ERR_NOT_FINISHED);
        ui.audio_setting_error = audio_result == ESP_ERR_NOT_FINISHED ? ESP_OK : audio_result;
        if (audio_result == ESP_ERR_NOT_FINISHED || preference_busy) {
            ui.audio_refresh_needed = true;
            ui.audio_refresh_retry_us = esp_timer_get_time() + 2000000;
        }
        if (ui.audio_open) invalidate();
    }
    if (ui.audio_refresh_needed && !ui.audio_setting_pending && !ui.asleep &&
        !s_options_requested.load(std::memory_order_acquire) &&
        !s_home_requested.load(std::memory_order_acquire) &&
        !s_boot_menu_requested.load(std::memory_order_acquire) &&
        !s_surprise_requested.load(std::memory_order_acquire) &&
        esp_timer_get_time() >= ui.audio_refresh_retry_us) {
        const auto error = CODY::request_audio_preferences_refresh();
        if (error == ESP_OK) {
            ui.audio_refresh_needed = false;
            ui.audio_setting_pending = true;
            ui.audio_setting_error = ESP_OK;
            if (ui.audio_open) invalidate();
        } else if (error == ESP_ERR_NOT_FINISHED) {
            ui.audio_refresh_retry_us = esp_timer_get_time() + 2000000;
        } else {
            ui.audio_refresh_needed = false;
            ui.audio_setting_error = error;
            ESP_LOGW(kTag, "Audio preferences refresh admission failed (%s)", esp_err_to_name(error));
        }
    }
    if (s_boot_activity.exchange(false, std::memory_order_acq_rel)) {
        const int64_t button_at_us = esp_timer_get_time();
        ui.model.navigation.interact(button_at_us);
        ui.screensaver_last_touch_us = button_at_us;
        ui.idle_applied = false;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (ui.voice_boot_route_hold) {
        const bool active = LUNASHELL::voice_input_phase_active(ui.model.voice.input.phase);
        if (active) ui.voice_boot_phase_seen = true;
        const auto requested_at = s_boot_voice_requested_at_us.load(std::memory_order_acquire);
        if (!active && (ui.voice_boot_phase_seen || requested_at <= 0 ||
            esp_timer_get_time() - requested_at > 15000000)) {
            ui.voice_boot_route_hold = false;
            ui.voice_boot_phase_seen = false;
        }
    }
    if (s_boot_voice_requested.load(std::memory_order_acquire)) {
        const int64_t voice_requested_at = s_boot_voice_requested_at_us.load(std::memory_order_acquire);
        const int64_t voice_now = esp_timer_get_time();
        if (voice_requested_at <= 0 || voice_now < voice_requested_at || voice_now - voice_requested_at > 15000000) {
            s_boot_voice_requested.store(false, std::memory_order_release);
            ui.voice_boot_error_until_us = voice_now + 3000000;
            invalidate();
        } else if (LUNASHELL::voice_input_phase_active(ui.model.voice.input.phase)) {
            // A running recording retains its own Send and Cancel controls.
            s_boot_voice_requested.store(false, std::memory_order_release);
            ESP_LOGI(kTag, "BOOT voice request ignored while recording is active");
        } else if (!voice_route_available(ui) ||
                   (ui.retired.layer != Layer::Dashboard && ui.retired.layer != Layer::Events &&
                    ui.retired.layer != Layer::CurrentTurn)) {
            // A voice request needs a completely retired dashboard before it can
            // freeze the destination and admit the microphone.
            const auto error = CODY::onboarding_dashboard_active() ? ESP_OK : CODY::open_device_dashboard(ui.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) {
                s_boot_voice_requested.store(false, std::memory_order_release);
                ui.voice_boot_error_until_us = voice_now + 3000000;
                invalidate();
                ESP_LOGW(kTag, "BOOT voice route unavailable (%s)", esp_err_to_name(error));
                return ESP_OK;
            }
            s_options_requested.store(false, std::memory_order_release);
            s_home_requested.store(false, std::memory_order_release);
            s_boot_menu_requested.store(false, std::memory_order_release);
            s_surprise_requested.store(false, std::memory_order_release);
            ui.boot_menu_open = false;
            ui.third_party_open = false;
            ui.prices_open = false;
            ui.animation_active = false;
            if (ui.usage_alarm_active) close_usage_alarm(ui);
            if (ui.event_cody_active) close_event_cody(ui);
            ui.audio_open = false;
            ui.diagnostics = false;
            ui.surprise_page = 0;
            ui.mascot_home = false;
            ui.activity_page = ActivityPage::Statistics;
            // A retained About QR page does not admit voice; leave that page
            // before waiting for the dashboard to retire its next frame.
            ui.about_from_options = false;
            ui.about_from_boot_menu = false;
            ui.model.info_page = CODY::InfoPage::System;
            CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
            invalidate();
            return ESP_OK;
        } else if (ui.retired.layer == Layer::Dashboard || ui.retired.layer == Layer::Events ||
                   ui.retired.layer == Layer::CurrentTurn) {
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
                ui.voice_boot_error_until_us = voice_now + 3000000;
                invalidate();
                ESP_LOGW(kTag, "BOOT voice request unavailable: started=%u (%s)",
                    unsigned(started), esp_err_to_name(error));
            } else {
                ui.voice_boot_route_hold = true;
                ui.voice_boot_phase_seen = false;
            }
        }
    }
#endif
    bool can_open_options = true;
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
        if (ui.usage_alarm_active) close_usage_alarm(ui);
        if (ui.event_cody_active) close_event_cody(ui);
        ui.boot_menu_open = true;
        ui.boot_menu_contact = CODY::BootMenuAction::None;
        CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
        invalidate();
    }
    if (can_open_options && (s_home_requested.load(std::memory_order_acquire) ||
        s_surprise_requested.load(std::memory_order_acquire))) {
        // Claim the gesture independently so a concurrent third press cannot be lost.
        const bool surprise = s_surprise_requested.exchange(false, std::memory_order_acq_rel);
        const auto error = CODY::open_device_home(ui.scene);
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
            ui.boot_menu_open = false;
            ui.third_party_open = false;
            ui.prices_open = false;
            ui.boot_section = BootSection::Default;
            if (ui.event_cody_active) close_event_cody(ui);
            ui.surprise_page = surprise ? 1 : 0;
            ui.home_swipe_active = false;
            ui.qr_touch_active = false;
            ui.audio_open = false;
            ui.diagnostics = false;
            ui.model.vouchers.warning_expiry = 0;
            CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
            ui.codex_browser.route = CODY::CodexBrowserRoute::Closed;
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            ui.mascot_home = true;
            ui.about_from_options = false;
            ui.about_from_boot_menu = false;
            ui.options_return_valid = false;
            if (ui.activity_page == ActivityPage::Reply) ui.activity_page = ActivityPage::Events;
            invalidate();
        }
    }
    if (can_open_options && !s_home_requested.load(std::memory_order_acquire) &&
        !s_surprise_requested.load(std::memory_order_acquire) &&
        s_options_requested.load(std::memory_order_acquire)) {
        const int64_t button_at_us = esp_timer_get_time();
        const bool in_options = ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours;
        const bool returning_to_options = in_options || ui.about_from_options ||
            ui.audio_open || ui.codex_browser.route != CODY::CodexBrowserRoute::Closed;
        const bool return_home = ui.mascot_home;
        const auto return_activity = ui.activity_page;
        const auto return_screen = ui.model.widgets.selected_screen;
        const auto return_info = ui.model.info_page;
        const esp_err_t error = returning_to_options ? CODY::return_to_options_tools(ui.scene) :
            CODY::open_companion_options(ui.scene);
        if (error != ESP_ERR_NOT_FINISHED) {
            s_options_requested.store(false, std::memory_order_release);
            if (error != ESP_OK) return error;
            ui.boot_menu_open = false;
            ui.third_party_open = false;
            ui.prices_open = false;
            if (ui.event_cody_active) close_event_cody(ui);
            ui.surprise_page = 0;
            if (!returning_to_options) {
                ui.options_return_valid = true;
                ui.options_return_home = return_home;
                ui.options_return_activity = return_activity;
                ui.options_return_screen = return_screen;
                ui.options_return_info = return_info;
            }
            ui.audio_open = false;
            ui.model.vouchers.warning_expiry = 0;
            CODY::dismiss_screensaver(ui.model.screensaver, button_at_us);
            ui.diagnostics = false;
            ui.codex_browser.route = CODY::CodexBrowserRoute::Closed;
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            ui.mascot_home = false;
            ui.about_from_options = false;
            ui.about_from_boot_menu = false;
            if (!in_options && ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours) {
                ui.model.effects.feedback = CODY::DeviceFeedback::OptionsOpened;
            }
            invalidate();
        }
    }
    const auto request = CODY::take_widget_open_request();
    switch (request) {
    case CODY::WidgetOpenRequest::Prices:
        ui.prices_open = true;
        invalidate();
        break;
    case CODY::WidgetOpenRequest::ThirdParty:
        ui.third_party_open = true;
        invalidate();
        break;
    case CODY::WidgetOpenRequest::About: {
        const auto error = CODY::open_device_dashboard(ui.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.mascot_home = false;
        ui.about_from_options = true;
        ui.about_from_boot_menu = false;
        ui.activity_page = ActivityPage::Statistics;
        ui.statistics_entry = false;
        ui.model.widgets.selected_screen = WidgetScreen::About;
        ui.model.info_page = CODY::InfoPage::System;
        invalidate();
        break;
    }
    case CODY::WidgetOpenRequest::CodexBrowser:
        ui.audio_open = false;
        if (ui.counter_mode_enabled) break;
        if (CODY::onboarding_codex_tools_active()) {
            CODY::open_codex_browser(ui.codex_browser);
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            invalidate();
        }
        break;
    case CODY::WidgetOpenRequest::ReturnFromOptions: {
        const auto error = CODY::open_device_dashboard(ui.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.mascot_home = !ui.options_return_valid || ui.options_return_home;
        if (ui.options_return_valid) {
            ui.activity_page = ui.options_return_activity;
            ui.model.widgets.selected_screen = ui.options_return_screen;
            ui.model.info_page = ui.options_return_info;
            ui.statistics_entry = false;
        }
        ui.options_return_valid = false;
        ui.about_from_options = false;
        invalidate();
        break;
    }
    case CODY::WidgetOpenRequest::Home:
    case CODY::WidgetOpenRequest::Statistics: {
        ui.audio_open = false;
        const auto error = CODY::open_device_dashboard(ui.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        ui.codex_browser.route = CODY::CodexBrowserRoute::Closed;
        ui.browser_destination = CODY::CodexBrowserDestination::None;
        ui.mascot_home = request == CODY::WidgetOpenRequest::Home;
        if (request == CODY::WidgetOpenRequest::Statistics) {
            select_statistics();
        } else {
            if (ui.activity_page == ActivityPage::Reply) ui.activity_page = ActivityPage::Events;
            invalidate();
        }
        break;
    }
    case CODY::WidgetOpenRequest::Audio:
        ui.audio_open = true;
        if (ui.audio_setting_error != ESP_OK && !ui.audio_setting_pending) {
            ui.audio_setting_error = ESP_OK;
            ui.audio_refresh_needed = true;
            ui.audio_refresh_retry_us = 0;
        }
        invalidate();
        break;
    default: break;
    }
    const auto inbox_error = DeviceSnapshotService::instance().readCodexInboxObservation(ui.inbox);
    if (inbox_error != ESP_OK) {
        ui.inbox.data_valid = false;
        ui.inbox.last_error = inbox_error;
    }
    CODY::set_onboarding_inbox_available(ui.inbox.last_success_at_us > 0);
    if (ui.reply_dismiss_pending && ui.inbox.data_valid &&
        std::strcmp(ui.inbox.acknowledged_reply, ui.reply.request_id) == 0) {
        ui.reply_dismiss_pending = false;
        if (ui.activity_page == ActivityPage::Reply) ui.activity_page = ActivityPage::Events;
        invalidate();
    }
    bool changed = false;
    const auto error = CODY::update_onboarding(ui.scene, ui.completed_scene, ui.awake_us, ui.awake_us, changed,
        ui.retired.valid && ui.retired.layer == Layer::Scene);
    if (CODY::take_preference_confirmation()) play_preference_cue(ui);
    if (changed) {
        ui.scene_dirty = true;
        if (ui.audio_open && (!ui.scene.valid || ui.scene.definition->id != CODY::SceneId::MakeItYours)) {
            ui.audio_open = false;
            invalidate();
        }
    }
    return error;
}

// No parameters: apply the current setup timeout through the existing power owner.
esp_err_t apply_standby_override()
{
    auto &ui = *s_ui;
    const bool scene_visible = !ui.mascot_home || (ui.retired.valid && ui.retired.layer == Layer::Scene);
    const uint32_t seconds = !ui.asleep && scene_visible && ui.scene.valid && !CODY::onboarding_dashboard_active()
        ? ui.scene.definition->standby_override_seconds : 0;
    if (seconds == ui.standby_override) return ESP_OK;
    const auto error = LUNASHELL_power_manager_set_scene_standby_override(seconds);
    if (error == ESP_OK) ui.standby_override = seconds;
    return error;
}

// command: Latest startup or power-owner handoff.
// Exception: Acknowledges success only after the requested panel frame retires.
esp_err_t apply_command(Command command)
{
    auto &ui = *s_ui;
    invalidate();
    ui.last_active_us = 0;
    ui.pressed = false;
    const int64_t now_us = esp_timer_get_time();
    ui.screensaver_last_touch_us = now_us;
    ui.idle_eligible_since_us = -1;
    ui.idle_applied = false;
    synchronize_character_theme(ui);
    CODY::dismiss_screensaver(ui.model.screensaver, now_us);
    CODY::suspend_companion_shake(ui.model.shake, now_us);
    if (command == Command::Ready || command == Command::Wake) ui.model.navigation.interact(now_us);
    FrameView frame{};
    if (command == Command::Ready) {
        const auto error = CODY::start_device_setup(ui.scene, ui.first_boot, ui.tour_step);
        if (error != ESP_OK) return error;
        select_statistics();
        ui.statistics_entry = false;
        ui.activity_page = ActivityPage::Events;
        ui.mascot_home = ui.tour_step == 5U;
        ui.pixel_demo.showcase.reset(static_cast<uint32_t>(now_us));
        ui.audio_refresh_needed = true;
        ui.audio_refresh_retry_us = 0;
    }
    if (command == Command::Sleep || command == Command::Shutdown) {
        if (ui.usage_alarm_active) close_usage_alarm(ui);
        if (ui.event_cody_active) close_event_cody(ui);
        if (ui.animation_active) CODY::reset_muse_animation(0);
        ui.animation_active = false;
        set_audio_demand(false);
        ui.model.effects.event_sound = {};
        const auto audio = CODY::set_codex_audio_suspended(true);
        if (audio != ESP_OK) ESP_LOGW(kTag, "Audio suspension status (%s)", esp_err_to_name(audio));
        esp_err_t error = ESP_OK;
        if (command == Command::Shutdown) {
            frame.layer = Layer::Message;
            error = CODY::prepare_device_message(ui.view, frame.presentation, "Device", "Powering off");
        }
        if (error == ESP_OK) error = render(frame, true);
        if (error == ESP_OK) ui.asleep = true;
        else (void)CODY::set_codex_audio_suspended(false);
        return error;
    }
    ui.asleep = false;
    const auto audio = CODY::set_codex_audio_suspended(false);
    if (audio != ESP_OK) ESP_LOGW(kTag, "Audio resume status (%s)", esp_err_to_name(audio));
    esp_err_t error = ESP_OK;
    if (command == Command::ShutdownFailed) {
        frame.layer = Layer::Message;
        error = CODY::prepare_device_message(ui.view, frame.presentation, "Device", "Power off failed");
    } else {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        // Ready has no sampled voice state yet; Wake may retain a pre-standby
        // session. Refresh before effects consume one-shot event identities.
        error = DeviceSnapshotService::instance().readVoiceObservation(ui.model.voice);
#endif
        if (error == ESP_OK) error = prepare_current_view(frame, esp_timer_get_time());
    }
    if (error == ESP_OK) error = render(frame, true);
    if (error == ESP_OK && command == Command::Wake) {
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        CODY::play_device_wake_cue(ui.model.effects, voice);
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
    LUNASHELL::CodyOptionsObservation counter_options{};
    const auto counter_error = DeviceSnapshotService::instance().readCodyOptionsObservation(counter_options);
    synchronize_character_theme(ui);
    if (counter_error == ESP_OK && counter_options.preferences.counter_mode_valid &&
        ui.counter_mode_enabled != counter_options.preferences.counter_mode_enabled) {
        ui.counter_mode_enabled = counter_options.preferences.counter_mode_enabled;
        ui.counter_page = 0;
        if (ui.counter_mode_enabled && ui.animation_active) {
            ui.animation_active = false;
            CODY::reset_muse_animation(0);
        }
        invalidate();
    }
    if (counter_error == ESP_OK && counter_options.preferences.weekly_alarm_valid &&
        counter_options.preferences.weekly_alarm_error == ESP_OK &&
        (counter_options.preferences.weekly_alarm_enabled || ui.usage_alarm_state.weekly.initialized || ui.usage_alarm_active)) {
        const auto link_error = DeviceSnapshotService::instance().readCodexTechnicalObservation(ui.model.dashboard.context);
        const auto &link = ui.model.dashboard.context.link;
        bool peer_ready = link_error == ESP_OK && link.data_valid && link.retained_link_present &&
            !link.pending_pairing_present && !link.command_pending && link.paired_pc_id[0] != '\0' &&
            strnlen(link.paired_pc_id, sizeof(link.paired_pc_id)) < sizeof(link.paired_pc_id);
        if (link_error == ESP_OK && link.data_valid && !link.retained_link_present) {
            ui.usage_alarm_state.weekly = {};
            ui.usage_alarm_peer[0] = '\0';
            if (ui.usage_alarm_active) close_usage_alarm(ui);
        } else if (peer_ready && std::strcmp(ui.usage_alarm_peer, link.paired_pc_id) != 0) {
            ui.usage_alarm_state.weekly = {};
            std::snprintf(ui.usage_alarm_peer, sizeof(ui.usage_alarm_peer), "%s", link.paired_pc_id);
            if (ui.usage_alarm_active) close_usage_alarm(ui);
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
                std::strncmp(ui.usage_alarm_peer, confirmed.paired_pc_id, sizeof(ui.usage_alarm_peer)) == 0;
        }
        if ((peer_ready && weekly_error == ESP_OK && clock_error == ESP_OK) || !counter_options.preferences.weekly_alarm_enabled) {
            CODY::UsageAlarmNotice notice{};
            CODY::UsageAlarmOptions options{};
            options.enabled = counter_options.preferences.weekly_alarm_enabled;
            const auto alarm_error = CODY::observe_usage_alarms(ui.usage_alarm_state, options, weekly, clock, notice);
            if (alarm_error != ESP_OK) ESP_LOGE(kTag, "Weekly alarm observation failed (%s)", esp_err_to_name(alarm_error));
            if (notice.sequence != 0 && ui.event_cody_active) close_event_cody(ui);
            if (!options.enabled && ui.usage_alarm_active) close_usage_alarm(ui);
            if (ui.usage_alarm_active && !ui.usage_alarm_presented &&
                notice.sequence != ui.usage_alarm_notice.sequence) close_usage_alarm(ui);
            const bool eligible_layer = ui.retired.valid && (ui.retired.layer == Layer::Mascot ||
                ui.retired.layer == Layer::Dashboard || ui.retired.layer == Layer::Counter);
            if (alarm_error == ESP_OK && notice.sequence != 0 && !ui.usage_alarm_active && eligible_layer &&
                !ui.asleep && !ui.pressed && !ui.animation_active && !ui.diagnostics && !ui.audio_open &&
                !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0) {
                ui.usage_alarm_notice = notice;
                ui.usage_alarm_active = true;
                ui.usage_alarm_presented = false;
                ui.usage_alarm_started_us = now_us;
                std::snprintf(ui.usage_alarm_title, sizeof(ui.usage_alarm_title), "WEEKLY %u%%",
                    static_cast<unsigned>(notice.threshold_percent));
                auto &preview = ui.animation_preview;
                preview.manual = true;
                preview.manual_playing = true;
                preview.manual_pose = CODY::PixelAvatarPose::Error;
                preview.manual_symbol = CODY::MuseSymbol::Exclamation;
                preview.manual_working = false;
                preview.manual_elapsed_us = 0;
                preview.manual_needs_compose = true;
                CODY::dismiss_screensaver(ui.model.screensaver, now_us);
                invalidate();
            }
        } else if (ui.usage_alarm_active && !ui.usage_alarm_presented) close_usage_alarm(ui);
    } else if (ui.usage_alarm_active && !ui.usage_alarm_presented) {
        close_usage_alarm(ui);
    }
    if (ui.usage_alarm_active && ui.usage_alarm_presented && now_us - ui.usage_alarm_started_us >= 6000000)
        close_usage_alarm(ui);
    if (ui.event_cody_active && now_us - ui.event_cody_started_us >= 3000000) close_event_cody(ui);
    const int64_t delta = ui.last_active_us == 0 ? 0 : now_us - ui.last_active_us;
    ui.awake_us += delta;
    ui.last_active_us = now_us;
    esp_err_t error = ESP_OK;
    if (ui.scene.valid && ui.retired.valid && ui.retired.layer == Layer::Scene &&
        ui.completed_scene.definition == ui.scene.definition) {
        error = CODY::advance_scene(ui.scene, delta);
    }
    if (error == ESP_OK) error = update_navigation();
    if (error == ESP_OK) error = apply_standby_override();
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (error == ESP_OK) error = DeviceSnapshotService::instance().readVoiceObservation(ui.model.voice);
#endif
    if (error != ESP_OK) return error;
    LUNASHELL::CodyIdleObservation idle{};
    const auto idle_error = DeviceSnapshotService::instance().readCodyIdleObservation(idle);
    if (idle_error == ESP_OK && idle.valid && idle.last_error == ESP_OK &&
        (!ui.idle_policy.valid || ui.idle_policy.show_muse != idle.show_muse ||
         ui.idle_policy.delay_seconds != idle.delay_seconds)) {
        ui.idle_policy = idle;
        ui.idle_eligible_since_us = -1;
        ui.idle_applied = false;
    } else if (idle_error != ESP_OK || !idle.valid || idle.last_error != ESP_OK) {
        ui.idle_policy.valid = false;
    }
    bool idle_eligible = ui.idle_policy.valid && !ui.asleep && !ui.pressed && !ui.boot_menu_open &&
        !ui.animation_active && !ui.usage_alarm_active && !ui.event_cody_active && !ui.audio_open && !ui.diagnostics &&
        CODY::onboarding_dashboard_active() && ui.retired.valid &&
        (ui.retired.layer == Layer::Counter || ui.retired.layer == Layer::Mascot || ui.retired.layer == Layer::Dashboard) &&
        ui.model.screensaver.phase != CODY::ScreensaverPhase::Visible &&
        !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    idle_eligible = idle_eligible && !voice_visible(now_us);
#endif
    if (!idle_eligible) {
        ui.idle_eligible_since_us = -1;
        ui.idle_applied = false;
    } else {
        if (ui.idle_eligible_since_us < 0 || now_us < ui.idle_eligible_since_us)
            ui.idle_eligible_since_us = now_us;
        const int64_t idle_since = std::max(ui.idle_eligible_since_us, ui.screensaver_last_touch_us);
        if (!ui.idle_applied && now_us >= idle_since &&
            now_us - idle_since >= static_cast<int64_t>(ui.idle_policy.delay_seconds) * 1000000LL) {
            ui.idle_applied = true;
            ui.boot_section = ui.idle_policy.show_muse ? BootSection::Events : BootSection::Counters;
            ui.mascot_home = ui.idle_policy.show_muse;
            if (!ui.idle_policy.show_muse) ui.activity_page = ActivityPage::Statistics;
            invalidate();
        }
    }
    const auto navigation = CODY::update_device_navigation(ui.model, now_us);
    // Acquisition may have published a new IMU sample during the scene or
    // snapshot update. Compare it against a timestamp taken after the read.
    const int64_t frame_now_us = esp_timer_get_time();
    if (!ui.pressed) {
        LUNASHELL::CodyParallaxObservation preference{};
        const auto preference_error = DeviceSnapshotService::instance().readCodyParallaxObservation(preference);
        const auto level = preference_error == ESP_OK && preference.valid && preference.last_error == ESP_OK &&
            preference.level <= 3 ? static_cast<CODY::ParallaxLevel>(preference.level) : CODY::ParallaxLevel::Off;
        const bool active = !ui.asleep && ui.retired.valid &&
            (ui.retired.layer == Layer::Mascot || ui.retired.layer == Layer::BootMenu ||
             (ui.retired.layer == Layer::Scene && CODY::onboarding_main_menu_active()));
        if (CODY::update_parallax(ui.parallax, ui.model.motion.imu, navigation.motion_error,
                ui.model.motion.rendering_rotation_degrees, active, level, frame_now_us)) {
            ui.boot_menu.artwork_x = ui.parallax.x;
            ui.boot_menu.artwork_y = ui.parallax.y;
            if (++ui.boot_menu.revision == 0) ++ui.boot_menu.revision;
            CODY::set_onboarding_menu_parallax(ui.parallax.x, ui.parallax.y);
            if (ui.retired.layer == Layer::Scene && CODY::onboarding_main_menu_active()) ui.scene_dirty = true;
        }
    }
    observe_chrome_motion(ui, frame_now_us, navigation.motion_error);
    bool may_return_home = navigation.shaken && !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.audio_open && !ui.diagnostics;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    may_return_home = may_return_home && !voice_visible(now_us);
#endif
    if (may_return_home) {
        LUNASHELL_power_manager_note_activity();
        CODY::dismiss_screensaver(ui.model.screensaver, now_us);
        select_statistics();
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (ui.animation_active && frame_now_us >= ui.animation_deadline_us) {
        ui.animation_active = false;
        CODY::reset_muse_animation(0);
        invalidate();
    }
    LUNASHELL::CodexAnimationRequest animation{};
    const auto animation_result = LUNASHELL::takeCodexAnimation(animation);
    esp_err_t animation_outcome = ESP_OK;
    if (animation_result == ESP_OK) {
        if (ui.usage_alarm_active) close_usage_alarm(ui);
        if (ui.event_cody_active) close_event_cody(ui);
        // Account for the visible interval before changing the playback state.
        auto &preview = ui.animation_preview;
        if (ui.animation_active && preview.visible && ui.awake_us >= preview.observed_active_us) {
            if (preview.manual_playing) {
                preview.manual_elapsed_us += static_cast<uint64_t>(ui.awake_us - preview.observed_active_us);
            }
            preview.observed_active_us = ui.awake_us;
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
            if (ui.animation_active) CODY::reset_muse_animation(0);
            ui.animation_active = false;
            invalidate();
        } else if (animation.operation == Op::Select && !ui.animation_active &&
            (!ui.retired.valid || (ui.retired.layer != Layer::Mascot && ui.retired.layer != Layer::Dashboard))) {
            animation_outcome = ESP_ERR_INVALID_STATE;
        } else if (animation.operation == Op::Select) {
            const auto &entry = entries[index];
            CODY::reset_muse_animation(0);
            ui.animation_scene = animation.scene;
            ui.animation_preview.manual = true;
            ui.animation_preview.manual_pose = entry.pose;
            ui.animation_preview.manual_symbol = entry.symbol;
            ui.animation_preview.manual_working = entry.working;
            ui.animation_preview.manual_elapsed_us = 0;
            ui.animation_preview.manual_playing = false;
            ui.animation_preview.manual_needs_compose = true;
            ui.animation_active = true;
            ui.animation_deadline_us = frame_now_us + 300000000;
            CODY::dismiss_screensaver(ui.model.screensaver, frame_now_us);
            invalidate();
        } else if (!ui.animation_active || animation.scene != ui.animation_scene) {
            animation_outcome = ESP_ERR_INVALID_STATE;
        } else {
            if (animation.operation == Op::Restart) {
                CODY::reset_muse_animation(0);
                ui.animation_preview.manual_elapsed_us = 0;
            }
            ui.animation_preview.manual_playing = animation.operation != Op::Pause;
            ui.animation_deadline_us = frame_now_us + 300000000;
            if (animation.operation == Op::Restart || animation.operation == Op::Pause) invalidate();
        }
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    LUNASHELL::CodexDebugScreenRequest debug{};
    const auto debug_result = LUNASHELL::takeCodexDebugScreen(debug);
    if (debug_result == ESP_OK) {
        if (ui.animation_active) {
            ui.animation_active = false;
            CODY::reset_muse_animation(0);
        }
        ui.diagnostics = debug.screen == LUNASHELL::CodexDebugScreen::Character;
        invalidate();
    } else if (debug_result != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(kTag, "Diagnostics request unavailable (%s)", esp_err_to_name(debug_result));
    }
#endif
    FrameView frame{};
    error = prepare_current_view(frame, frame_now_us, navigation.motion_error);
    if (error == ESP_OK) error = render(frame);
    const auto &effects = ui.model.effects;
    if (error == ESP_OK && effects.event_visual_sequence != ui.event_visual_seen) {
        ui.event_visual_seen = effects.event_visual_sequence;
        LUNASHELL::CodyEventVisualObservation preference{};
        const auto preference_error = DeviceSnapshotService::instance().readCodyEventVisualObservation(preference);
        const bool eligible_layer = ui.retired.valid && (ui.retired.layer == Layer::Mascot ||
            ui.retired.layer == Layer::Dashboard || ui.retired.layer == Layer::Counter);
        if (preference_error == ESP_OK && preference.valid && preference.last_error == ESP_OK && preference.enabled &&
            effects.event_online && effects.event_visual_at_us > 0 && frame_now_us >= effects.event_visual_at_us &&
            frame_now_us - effects.event_visual_at_us <= 1000000 && eligible_layer && !ui.pressed &&
            !ui.asleep && !ui.boot_menu_open && !ui.animation_active && !ui.usage_alarm_active &&
            !ui.event_cody_active && !ui.audio_open && !ui.diagnostics &&
            (!CODY::device_effects_require_attention(effects) ||
             effects.event.kind == LUNASHELL::CodexEventKind::ReplyReady) && ui.model.battery_banner.sequence == 0) {
            ui.event_cody_active = true;
            ui.event_cody_started_us = frame_now_us;
            ui.event_cody_kind = effects.event_visual_kind;
            auto &preview = ui.animation_preview;
            preview.manual = true;
            preview.manual_playing = true;
            preview.manual_pose = effects.event_visual_kind == LUNASHELL::CodexEventKind::ChecksFailed
                ? CODY::PixelAvatarPose::Error : CODY::PixelAvatarPose::Reply;
            preview.manual_symbol = effects.event_visual_kind == LUNASHELL::CodexEventKind::ChecksFailed
                ? CODY::MuseSymbol::Exclamation : CODY::MuseSymbol::Heart;
            preview.manual_working = false;
            preview.manual_elapsed_us = 0;
            preview.manual_needs_compose = true;
            invalidate();
        }
    }
    if (error == ESP_OK && ui.usage_alarm_active && !ui.usage_alarm_presented && ui.retired.valid &&
        ui.retired.layer == Layer::UsageAlarm) {
        CODY::acknowledge_usage_alarm(ui.usage_alarm_state, ui.usage_alarm_notice);
        ui.usage_alarm_presented = true;
        ui.usage_alarm_started_us = esp_timer_get_time();
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        (void)CODY::play_pet_cue(ui.model.effects, ui.usage_alarm_notice.cue, voice);
    }
    if (error == ESP_OK && ui.retired.valid && ui.retired.layer == Layer::CodexBrowser &&
        ui.codex_browser.bookmark_sound_pending) {
        ui.codex_browser.bookmark_sound_pending = false;
        play_bookmark_cue(ui);
    }
    if (error == ESP_OK && ui.questions_navigation_sound_pending) {
        ui.questions_navigation_sound_pending = false;
        if (ui.retired.valid && ui.retired.layer == Layer::Events &&
            ui.browser_destination == CODY::CodexBrowserDestination::Events &&
            ui.codex_browser.route == CODY::CodexBrowserRoute::PendingQuestions) {
            play_questions_cue(ui);
        }
    }
    if (error == ESP_OK && ui.activity_navigation_sound_pending) {
        ui.activity_navigation_sound_pending = false;
        if (ui.retired.valid && ui.retired.layer == Layer::CurrentTurn &&
            ui.browser_destination == CODY::CodexBrowserDestination::CurrentTurn) {
            play_activity_cue(ui);
        }
    }
    if (error == ESP_OK && ui.retired.valid &&
        (ui.retired.layer == Layer::Mascot || ui.retired.layer == Layer::Animation)) {
        const auto &visible = ui.retired.layer == Layer::Mascot ? ui.pixel_demo : ui.animation_preview;
        if (ui.announced_cue_layer != ui.retired.layer) {
            ui.announced_symbol = CODY::MuseSymbol::None;
            ui.announced_working = false;
            ui.announced_surprise = false;
            ui.symbol_cue_pending = false;
            ui.working_cue_pending = false;
            ui.announced_cue_layer = ui.retired.layer;
        }
        const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        voice = &ui.model.voice;
#endif
        if (ui.pet_tap_cue_pending && ui.retired.layer == Layer::Mascot) {
            ui.pet_tap_cue_pending = false;
            const auto cue_error = CODY::play_pet_cue(ui.model.effects, CODY::PetCue::Tap, voice);
            if (cue_error != ESP_OK) ESP_LOGW(kTag, "Muse tap cue admission failed (%s)", esp_err_to_name(cue_error));
        } else if (ui.retired.layer != Layer::Mascot) {
            ui.pet_tap_cue_pending = false;
        }
        if (visible.retired_symbol != ui.announced_symbol) {
            ui.announced_symbol = visible.retired_symbol;
            ui.symbol_cue_pending = visible.retired_symbol != CODY::MuseSymbol::None;
            ui.symbol_cue_deadline_us = frame_now_us + kMuseCueWaitUs;
        }
        if (visible.retired_working != ui.announced_working) {
            ui.announced_working = visible.retired_working;
            ui.working_cue_pending = visible.retired_working;
            ui.working_cue_deadline_us = frame_now_us + kMuseCueWaitUs;
        }
        // The shared speaker may be busy with a different event. Retry only while
        // the corresponding retired visual is still present and the cue is fresh.
        // Working has priority; a simultaneously visible symbol can follow it.
        if (ui.working_cue_pending) {
            if (frame_now_us >= ui.working_cue_deadline_us) {
                ui.working_cue_pending = false;
            } else {
                const auto status = CODY::play_device_working_cue(ui.model.effects, voice);
                if (status.state != LUNASHELL_CUE_DROPPED_BUSY) ui.working_cue_pending = false;
            }
        }
        if (ui.symbol_cue_pending && !ui.working_cue_pending) {
            if (frame_now_us >= ui.symbol_cue_deadline_us) {
                ui.symbol_cue_pending = false;
            } else {
                const auto status = CODY::play_device_symbol_cue(ui.model.effects, ui.announced_symbol, voice);
                if (status.state != LUNASHELL_CUE_DROPPED_BUSY) ui.symbol_cue_pending = false;
            }
        }
        if (visible.retired_surprise && !ui.announced_surprise) {
            const auto cue_error = CODY::play_pet_cue(ui.model.effects, CODY::PetCue::Surprise, voice);
            if (cue_error != ESP_OK) ESP_LOGW(kTag, "Muse surprise cue admission failed (%s)", esp_err_to_name(cue_error));
        }
        ui.announced_surprise = visible.retired_surprise;
    } else if (error == ESP_OK) {
        ui.announced_symbol = CODY::MuseSymbol::None;
        ui.announced_working = false;
        ui.announced_surprise = false;
        ui.symbol_cue_pending = false;
        ui.working_cue_pending = false;
        ui.announced_cue_layer = Layer::Blank;
        ui.pet_tap_cue_pending = false;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (animation_result == ESP_OK) {
        if (animation_outcome == ESP_OK) {
            const bool visible = ui.retired.valid && ui.retired.layer == Layer::Animation;
            const bool expected = animation.operation != LUNASHELL::CodexAnimationOperation::Normal;
            if (error != ESP_OK) animation_outcome = error;
            else if (visible != expected) animation_outcome = ESP_ERR_INVALID_STATE;
            if (animation_outcome != ESP_OK && expected) {
                ui.animation_active = false;
                CODY::reset_muse_animation(0);
            }
        }
        const auto ack = LUNASHELL::completeCodexAnimation(animation, animation_outcome);
        if (ack != ESP_OK) ESP_LOGW(kTag, "Animation acknowledgement failed (%s)", esp_err_to_name(ack));
    }
    if (debug_result == ESP_OK) {
        const bool matched = (debug.screen == LUNASHELL::CodexDebugScreen::Character) ==
            (ui.retired.valid && ui.retired.layer == Layer::Diagnostics);
        const auto outcome = error == ESP_OK && !matched ? ESP_ERR_INVALID_STATE : error;
        const auto ack = LUNASHELL::completeCodexDebugScreen(debug, outcome, 0);
        if (ack != ESP_OK) ESP_LOGW(kTag, "Diagnostics acknowledgement failed (%s)", esp_err_to_name(ack));
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (error == ESP_OK && frame.layer == Layer::Voice) {
        ui.presented_voice_session = ui.model.voice.input.started_at_us;
        if (ui.model.voice.input.phase == VoicePhase::Preparing && ui.model.voice.input.destination_title[0] != '\0') {
            bool accepted = false;
            error = LUNASHELL::beginVoiceInputCountdown(ui.presented_voice_session, accepted);
        }
    }
#endif
    return error;
}

// ui: Display-owned audio outcome and canonical voice observation.
// cue: One startup transition whose sound has not yet been admitted.
// Returns false while the first-boot voice observation or audio mailbox is pending.
bool try_startup_pet_cue(DeviceUi &ui, CODY::PetCue cue)
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
    const auto error = CODY::play_pet_cue(ui.model.effects, cue, voice);
    if (cue == CODY::PetCue::FirstBootMelody && error == ESP_ERR_NOT_FINISHED) return false;
    return error != ESP_OK || ui.model.effects.pet_audio_status.state != LUNASHELL_CUE_DROPPED_BUSY;
}

// ui: Serialized boot frame state. Each resource frame receives its original visible dwell.
// Exception: Returns the real display frame error; a parked panel can be retried.
esp_err_t advance_branding(DeviceUi &ui)
{
    if (!LUNASHELL_display_runtime_writes_allowed()) return ESP_OK;
    const bool starting = ui.branding_phase == BrandingPhase::AwaitingReady;
    if (starting) {
        // The startup owner posts Ready after radio DMA parking and final log archival.
        // Until then, the first retired frame stays visible without consuming animation time.
        const auto request = s_command.load(std::memory_order_acquire);
        if (static_cast<Command>(request & 0xffU) != Command::Ready) return ESP_OK;
    } else if (ui.boot_ding_pending) {
        const auto now_us = esp_timer_get_time();
        if (ui.first_boot_melody && ui.model.effects.boot_cue_requested &&
            ui.model.effects.boot_audio_error != ESP_OK) {
            // A failed restored output policy must not start the first-run melody.
            ui.boot_ding_pending = false;
        } else if (now_us >= ui.boot_ding_deadline_us) {
            ESP_LOGW(kTag, "Startup music or ding was not admitted before its deadline");
            ui.boot_ding_pending = false;
        } else if (try_startup_pet_cue(ui, ui.first_boot_melody ? CODY::PetCue::FirstBootMelody : CODY::PetCue::BootDing)) {
            ui.boot_ding_pending = false;
            const auto &status = ui.model.effects.pet_audio_status;
            if (ui.first_boot_melody && status.cue_id == static_cast<uint32_t>(CODY::PetCue::FirstBootMelody) &&
                status.state == LUNASHELL_CUE_DISPATCHED && status.request_sequence != 0) {
                ui.first_boot_melody_sequence = status.request_sequence;
                ui.first_boot_melody_deadline_us = now_us + 8000000LL;
            }
        }
    }
    if (!starting && esp_timer_get_time() < ui.branding_due_us) {
        return ESP_OK;
    }
    const uint16_t next = starting ? 0 : static_cast<uint16_t>(ui.branding_frame + 1U);
    if (next == VIBEWARE_BOOT_FRAME_COUNT) {
        if (ui.boot_ding_pending) return ESP_OK;
        if (ui.first_boot_melody_sequence != 0) {
            lunashell_audio_cue_status_t status{};
            const uint32_t cue_id = static_cast<uint32_t>(CODY::PetCue::FirstBootMelody);
            const auto status_error = LUNASHELL_audio_get_cue_status(ui.first_boot_melody_sequence, cue_id, &status);
            const bool active = status_error == ESP_OK &&
                (status.state == LUNASHELL_CUE_DISPATCHED || status.state == LUNASHELL_CUE_PLAYING);
            if (active && esp_timer_get_time() < ui.first_boot_melody_deadline_us) return ESP_OK;
            if (active) {
                const auto cancel_error = LUNASHELL_audio_cancel_cue(ui.first_boot_melody_sequence, cue_id);
                ESP_LOGW(kTag, "First-boot melody exceeded its deadline; cancel=%s", esp_err_to_name(cancel_error));
            } else if (status_error != ESP_OK || status.state == LUNASHELL_CUE_FAILED) {
                ESP_LOGW(kTag, "First-boot melody did not complete: observation=%s playback=%s",
                    esp_err_to_name(status_error), esp_err_to_name(status.last_error));
            }
            ui.first_boot_melody_sequence = 0;
        }
        ui.branding_phase = BrandingPhase::Inactive;
        ui.birth_active = ui.birth_pending;
        ESP_LOGI(kTag, "Boot disk animation completed: %u frames retired", VIBEWARE_BOOT_FRAME_COUNT);
        return ESP_OK;
    }
    const auto previous = ui.branding_frame;
    ui.branding_frame = next;
    FrameView frame{};
    frame.layer = Layer::Branding;
    const auto error = render(frame, true);
    if (error == ESP_OK) {
        ui.branding_due_us = esp_timer_get_time() + VIBEWARE_boot_frames[next].duration_ms * 1000LL;
        if (starting) {
            ui.branding_phase = BrandingPhase::Playing;
            if (ui.first_boot_melody) {
                // A successful visible frame precedes consuming the one-shot key;
                // audio is never attempted when that durable write fails.
                const auto music_error = CODY::persist_first_boot_music_pending(false);
                if (music_error != ESP_OK) {
                    ui.model.effects.boot_cue_requested = true;
                    ui.model.effects.boot_audio_error = music_error;
                    ESP_LOGW(kTag, "First-boot melody omitted: one-shot persistence failed (%s)",
                        esp_err_to_name(music_error));
                }
            }
            ui.boot_ding_pending = true;
            ui.boot_ding_deadline_us = esp_timer_get_time() + kStartupCueWaitUs;
            ESP_LOGI(kTag, "Boot disk visible cycle started after service readiness");
        }
    } else if (error == ESP_ERR_NOT_FINISHED) {
        ui.branding_frame = previous;
    }
    return error;
}

// ui: Serialized startup state; only successfully retired frames advance the sequence.
// Exception: Preserves the original rendering failure; parked frames can be retried.
esp_err_t advance_birth_intro(DeviceUi &ui)
{
    if (!LUNASHELL_display_runtime_writes_allowed() || esp_timer_get_time() < ui.birth_due_us) return ESP_OK;
    if (ui.birth_rise_pending) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= ui.birth_rise_deadline_us) {
            ESP_LOGW(kTag, "Birth rise was not admitted before its deadline");
            ui.birth_rise_pending = false;
        } else if (!ui.boot_ding_pending && try_startup_pet_cue(ui, CODY::PetCue::BirthRise)) {
            ui.birth_rise_pending = false;
        } else {
            // Keep the first white frame on screen while the admitted ding retires.
            ui.birth_due_us = now_us + kBirthFrameMs * 1000LL;
            return ESP_OK;
        }
    }
    uint8_t white = 0;
    if (ui.birth_frame < kBirthWhiteHoldFrames) white = 255;
    else if (ui.birth_frame < kBirthMuseStartFrame) {
        white = static_cast<uint8_t>((kBirthMuseStartFrame - 1U - ui.birth_frame) * 255U /
                                     (kBirthMuseStartFrame - kBirthWhiteHoldFrames - 1U));
    }
    ui.birth_background = CODY2D::fade_rgb565_to_black(CODY::Theme::text_bright, white);
    if (ui.birth_frame >= kBirthMuseStartFrame) {
        const bool greeting = ui.birth_frame >= kBirthWelcomeFrame;
        const uint32_t avatar_ms = (ui.birth_frame - kBirthMuseStartFrame) * kBirthFrameMs;
        const uint32_t greeting_ms = greeting ? (ui.birth_frame - kBirthWelcomeFrame) * kBirthFrameMs : 0;
        CODY::compose_pixel_avatar(ui.birth_avatar,
            greeting ? CODY::PixelAvatarPose::Reply : CODY::PixelAvatarPose::Idle,
            avatar_ms, greeting ? greeting_ms : avatar_ms,
            greeting ? greeting_ms : CODY::kPixelAvatarReactionMs);
        const auto opacity = static_cast<uint8_t>(std::min<uint32_t>(255U,
            (ui.birth_frame - kBirthMuseStartFrame) * 255U / (kBirthMuseFadeFrames - 1U)));
        for (auto &color : ui.birth_avatar.palette) color = CODY2D::fade_rgb565_to_black(color, opacity);
        for (auto &color : ui.birth_avatar.dim_palette) color = CODY2D::fade_rgb565_to_black(color, opacity);
    }
    FrameView frame{};
    frame.layer = Layer::Birth;
    const auto error = render(frame, true);
    if (error != ESP_OK) return error;
    if (ui.birth_frame == 0 && !ui.first_boot_melody) {
        ui.birth_rise_pending = true;
        ui.birth_rise_deadline_us = esp_timer_get_time() + kStartupCueWaitUs;
    } else if (ui.birth_frame == kBirthWelcomeFrame && !ui.first_boot_melody) {
        (void)try_startup_pet_cue(ui, CODY::PetCue::BirthGreeting);
    }
    ++ui.birth_frame;
    ui.birth_due_us = esp_timer_get_time() + kBirthFrameMs * 1000LL;
    return ESP_OK;
}

// context: Unused; the task exclusively owns native navigation and presentation.
// Exception: Publishes terminal display failures through the snapshot owner.
void device_ui_worker(void *context)
{
    (void)context;
    auto &ui = *s_ui;
    esp_err_t error = ESP_OK;
    while (error == ESP_OK) {
        CODY::update_device_uptime(ui.model.dashboard, esp_timer_get_time());
        if (ui.branding_phase != BrandingPhase::Inactive) {
            error = advance_branding(ui);
            if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
            if (error == ESP_OK && ui.branding_phase != BrandingPhase::Inactive) {
                const uint32_t wait_ms = ui.branding_phase == BrandingPhase::AwaitingReady ? kPollMs : 10U;
                vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
            }
            continue;
        }
        if (ui.birth_active) {
            if (ui.birth_frame == kBirthFrameCount && esp_timer_get_time() >= ui.birth_due_us) {
                // All frames and the final visible dwell precede the durable completion record.
                error = CODY::persist_birth_intro_pending(false);
                if (error == ESP_OK) {
                    ui.birth_pending = false;
                    ui.birth_active = false;
                    ESP_LOGI(kTag, "Muse birth introduction completed and saved");
                }
            } else if (ui.birth_frame < kBirthFrameCount) {
                error = advance_birth_intro(ui);
                if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
            }
            if (error == ESP_OK && ui.birth_active) vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
            continue;
        }
        const uint32_t request = s_command.exchange(0, std::memory_order_acq_rel);
        if (request != 0) {
            const auto result = apply_command(static_cast<Command>(request & 0xffU));
            s_command_result.store(result, std::memory_order_relaxed);
            s_ack.store(request, std::memory_order_release);
        }
        if (!s_ready.load(std::memory_order_acquire) || ui.asleep ||
            !LUNASHELL_display_runtime_writes_allowed()) {
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
            continue;
        }
        const bool animating = ui.retired.voice_ribbon_at_us != 0 || ui.view.countdown.animating() ||
            ui.retired.heading_scrolls ||
            (ui.retired.layer == Layer::Scene && CODY::scene_title_overflows(ui.completed_scene));
        uint32_t wait_ms = kPollMs;
        if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible) {
            wait_ms = static_cast<uint32_t>(CODY::kScreensaverFrameUs / 1000);
        } else if (animating) {
            wait_ms = 33;
        } else if (ui.retired.valid && (ui.retired.layer == Layer::Mascot || ui.retired.layer == Layer::Animation ||
            ui.retired.layer == Layer::UsageAlarm || ui.retired.layer == Layer::EventCody)) {
            const int64_t remaining_us = std::max<int64_t>(0, ui.muse_frame_due_us - esp_timer_get_time());
            wait_ms = static_cast<uint32_t>((remaining_us + 999) / 1000);
        }
        const auto wait = LUNASHELL_touch_wait_for_report(wait_ms);
        if (wait != ESP_OK && wait != ESP_ERR_NOT_FOUND) {
            error = wait;
            break;
        }
        if (s_command.load(std::memory_order_acquire) != 0) continue;
        if (wait == ESP_OK || ui.pressed) error = sample_touch();
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
    s_ui = new (s_ui_storage) DeviceUi{};
    LUNASHELL_audio_begin_cody_output_policy();
    CODY::initialize_device_uptime(s_ui->model.dashboard);
    CODY::OnboardingSettings settings{};
    auto error = CODY::load_onboarding_settings(settings);
    if (error == ESP_OK) error = CODY::load_companion_preferences();
    if (error == ESP_OK) error = CODY::onboarding_service_init();
    if (error != ESP_OK) return error;
    LUNASHELL::CodyOptionsObservation startup_options{};
    const auto options_error = DeviceSnapshotService::instance().readCodyOptionsObservation(startup_options);
    if (options_error == ESP_OK && startup_options.preferences.counter_mode_valid) {
        s_ui->counter_mode_enabled = startup_options.preferences.counter_mode_enabled;
    }
    // Restore the saved output policy before any optional Cody cue can play.
    const auto startup_volume_error = CODY::reconcile_audio_output_preference();
    // Reconcile input and retry transient output contention on the action worker.
    s_ui->audio_refresh_needed = true;
    s_ui->audio_refresh_retry_us = 0;
    // The acquisition owner has initialized the codec; prepare its shared notification worker.
    // A failed optional sound worker does not remove pairing or power controls.
    (void)CODY::prepare_device_effects(s_ui->model.effects);
    if (startup_volume_error != ESP_OK) {
        s_ui->model.effects.boot_cue_requested = true;
        s_ui->model.effects.boot_audio_error = startup_volume_error;
        ESP_LOGW(kTag, "Boot cue omitted: saved output policy unavailable (%s)",
            esp_err_to_name(startup_volume_error));
    }
    s_ui->first_boot = settings.effective_first_boot;
    s_ui->tour_step = settings.onboarding_tour_step;
    s_ui->birth_pending = settings.birth_intro_pending && esp_reset_reason() != ESP_RST_DEEPSLEEP;
    // The first visible boot frame consumes this separate one-shot marker before
    // audio admission, keeping ordinary sounds on later restarts or birth replays.
    s_ui->first_boot_melody = settings.first_boot_music_pending && esp_reset_reason() != ESP_RST_DEEPSLEEP;
    s_ui->setup_progress_pending = settings.setup_progress_pending;
    static constexpr char welcome[] = "Welcome!";
    const CODY2D::TextSpan welcome_text{welcome, sizeof(welcome) - 1U};
    error = CODY::prepare_styled_title(welcome_text, {73, 356, 320, 40}, s_ui->birth_welcome, 3, true);
    if (error != ESP_OK) return error;
    FrameView frame{};
    const bool show_branding = esp_reset_reason() != ESP_RST_DEEPSLEEP;
    if (show_branding) s_ui->branding_phase = BrandingPhase::AwaitingReady;
    ESP_LOGI(kTag, "Boot disk animation: reset_reason=%u requested=%u frames=%u",
        static_cast<unsigned>(esp_reset_reason()), static_cast<unsigned>(show_branding), VIBEWARE_BOOT_FRAME_COUNT);
    if (show_branding) {
        frame.layer = Layer::Branding;
        error = VIBEWARE_boot_validate();
        if (error == ESP_OK) error = CODY::BootBranding::prepare(
            CODY::SceneData::title_font, CODY::SceneData::body_font, s_ui->branding);
    } else {
        frame.layer = Layer::Message;
        error = CODY::prepare_device_message(s_ui->view, frame.presentation, "Device", "Starting services");
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

