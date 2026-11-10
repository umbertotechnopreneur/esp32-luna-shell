#include "CODY_companion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <new>

#include "CODY_boot_branding.hpp"
#include "CODY_codex_audio.hpp"
#include "CODY_device_view.hpp"
#include "CODY_device_effects.hpp"
#include "CODY_codex_panels.hpp"
#include "CODY_codex_browser.hpp"
#include "CODY_local_timer_view.hpp"
#include "CODY_pixel_demo.hpp"
#include "CODY_screensaver.hpp"
#include "CODY_onboarding.hpp"
#include "CODY_settings.hpp"
#include "CODY_voice_waveform.hpp"
#include "CODY_waveshare_device.hpp"
#include "CODY_widget_visibility_view.hpp"
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

namespace {
using CODY::WidgetScreen;
using CODY::WidgetStep;
using LUNASHELL::DeviceSnapshotService;
constexpr char kTag[] = "LUNA_device_ui";
constexpr auto kGeometry = CODY::Waveshare::kDevice.geometry;
constexpr auto kMuseViewport = kGeometry.centered_grid(CODY::kPixelAvatarGridSize, 3);
static_assert(kMuseViewport.width != 0, "The Cody display must admit a full Muse grid");
constexpr uint32_t kFrameBudgetMs = 500;
constexpr uint32_t kPollMs = 100;
constexpr uint32_t kPowerTimeoutMs = 2500;
// Ready includes one complete branding cycle after the radio owners release the display.
constexpr uint32_t kReadyTimeoutMs = kPowerTimeoutMs + VIBEWARE_BOOT_DURATION_MS +
    VIBEWARE_BOOT_FRAME_COUNT * (kFrameBudgetMs + kPollMs);
// The onboarding and CCP projections still use the existing display task stack budget.
constexpr uint32_t kWorkerStackBytes = 12288;

enum class Layer : uint8_t {
    Branding, Mascot, Message, Scene, Dashboard, AboutLink, Visibility, Timer, Voice, Diagnostics,
    VoucherWarning, Events, CurrentTurn, EventReply, CodexBrowser, Screensaver, Blank
};
enum class Command : uint8_t { Ready = 1, Sleep, Wake, Shutdown, ShutdownFailed };
enum class ActivityPage : uint8_t { Events, CurrentTurn, Reply, Statistics };
enum class BrandingPhase : uint8_t { Inactive, AwaitingReady, Playing };
using Save = CODY::VisibilitySave;
enum class Target : uint8_t {
    None, Previous, Next, VisibilityFirst, VisibilitySecond, VisibilityThird,
    Interval, FirstAction, SecondAction, VoiceGesture, VoucherScroll, VoucherDismiss,
    TimerDecrease, TimerIncrease, TimerSound, TimerStart, TimerCancel, TimerDismiss,
    EventsGesture, ActivityBack, EventDismiss, CodexBrowserControl, CodexBrowserBack
};
constexpr std::array<Target, CODY::kVisibilityRowsPerPage> kVisibilityTargets{
    Target::VisibilityFirst, Target::VisibilitySecond, Target::VisibilityThird,
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
};

struct DeviceUi {
    CODY::DeviceModel model{};
    CODY::SceneFrame scene{};
    CODY::SceneFrame completed_scene{};
    CODY::WidgetVisibilityView editor{};
    CODY::LocalTimerView timer_view{};
    CODY::BootBranding::Presentation branding{};
    CODY::BootBranding::Scratch branding_scratch{};
    int64_t branding_due_us = 0;
    uint16_t branding_frame = 0;
    BrandingPhase branding_phase = BrandingPhase::Inactive;

    CODY::DeviceView view{};
    CODY::PixelDemoView pixel_demo{kMuseViewport};
    CODY::CodexPanelView codex_panel{};
    CODY::CodexBrowser codex_browser{};
    CODY::CodexBrowserDestination browser_destination = CODY::CodexBrowserDestination::None;
    LUNASHELL::CodexInboxSnapshot inbox{};
    LUNASHELL::CodexInboxRow reply{};
    CODY::WidgetVisibility available_statistics{};
    char touched_event[33]{};
    ActivityPage activity_page = ActivityPage::Events;
    bool reply_dismiss_pending = false;
    bool statistics_entry = false;
    bool current_turn_available = false;
    RetiredFrame retired{};
    CODY2D::Point activity_touch_start{};
    CODY2D::Point activity_touch_last{};
    CODY2D::Point voucher_touch_start{};
    CODY2D::Point voucher_touch_last{};
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
    bool timer_open = false;
    bool first_boot = false;
    bool mascot_home = false;
};

// Data is owned by the display worker and stays off its stack and the DMA heap.
alignas(DeviceUi) EXT_RAM_BSS_ATTR uint8_t s_ui_storage[sizeof(DeviceUi)]{};
DeviceUi *s_ui = nullptr;
std::atomic<bool> s_ready{false};
std::atomic<bool> s_options_requested{false};
std::atomic<uint32_t> s_ticket{0};
std::atomic<uint32_t> s_command{0};
std::atomic<uint32_t> s_ack{0};
std::atomic<esp_err_t> s_command_result{ESP_OK};
std::atomic<esp_err_t> s_terminal_error{ESP_OK};

// No parameters: retire old touch ownership before changing the visible route.
void invalidate()
{
    s_ui->retired.valid = false;
    s_ui->contact = Target::None;
    CODY::hide_pixel_demo(s_ui->pixel_demo);
    s_ui->view.account.chat_title.line.valid = false;
    s_ui->codex_panel.chat_title.line.valid = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    s_ui->model.voice_tapped_us = 0;
#endif
    s_ui->scene_dirty = true;
    s_ui->view.countdown.reset_animation();
    CODY::cancel_onboarding_touch();
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

// No parameters: physical BOOT posts an intent for the display owner.
void queue_options()
{
    s_options_requested.store(true, std::memory_order_release);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
using VoicePhase = LUNASHELL::VoiceInputPhase;

// ui: Current route and its display-owned overlays. The same route is retained through standby.
bool voice_route_available(const DeviceUi &ui)
{
    return CODY::Waveshare::kDevice.voice_recording &&
        !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.model.editing && !ui.timer_open && !ui.diagnostics &&
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

// frame: Receives one visible surface and its native content.
// now_us: Monotonic time for the reset and recording views.
// motion_error: Result of the canonical motion/audio read in this update, if any.
// Exception: Returns original native preparation failures.
esp_err_t prepare_current_view(FrameView &frame, int64_t now_us, esp_err_t motion_error = ESP_ERR_NOT_SUPPORTED)
{
    auto &ui = *s_ui;
    CODY::update_device_observations(ui.model.dashboard);
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
    // Read an admitted alarm's result before another effect can reuse the audio mailbox.
    auto &timer = ui.model.timer;
    const bool tracking_alarm = timer.alarm_request_sequence != 0;
    if (tracking_alarm) CODY::update_local_timer_alarm(timer, now_us, voice_busy);
    // Optional sound failures remain in the effects model; visual controls keep working.
    (void)CODY::update_device_effects(ui.model.effects, ui.model.dashboard.connection,
        ui.model.dashboard.connection_valid, ui.model.battery_banner, voice, now_us,
        CODY::onboarding_saved_wifi_startup_offline(now_us));
    if (!tracking_alarm) CODY::update_local_timer_alarm(timer, now_us, voice_busy);
    const bool voucher_attention_allowed = !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.model.editing && !ui.diagnostics && !ui.timer_open && !voice_busy &&
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
    const bool saver_eligible = !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.model.editing && !ui.diagnostics &&
        !ui.timer_open && !voice_busy &&
        ui.model.vouchers.warning_expiry == 0 &&
        !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0 &&
        !(ui.model.widgets.selected_screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::ProjectLink);
    CODY::update_screensaver(ui.model.screensaver, saver_eligible,
        ui.model.navigation.last_touch_us, now_us);
    if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible) {
        frame.layer = Layer::Screensaver;
        return CODY::prepare_screensaver(ui.model.screensaver, ui.view.screensaver, frame.presentation.content);
    }
    frame.navigation_opacity = ui.model.navigation.opacity(now_us);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (voice_visible(now_us)) {
        frame.layer = Layer::Voice;
        const auto error = CODY::prepare_device_voice(ui.view, frame.presentation, ui.model.voice.input);
        if (error == ESP_OK) prepare_device_voice_ribbon(ui, frame, motion_error);
        return error;
    }
#endif
    if (ui.diagnostics) {
        frame.layer = Layer::Diagnostics;
        const auto error = DeviceSnapshotService::instance().readDiagnosticsObservation(ui.model.diagnostics);
        return CODY::prepare_device_diagnostics(ui.view, frame.presentation, ui.model.diagnostics, error == ESP_OK);
    }
    if (ui.timer_open) {
        frame.layer = Layer::Timer;
        frame.arrows = true;
        return CODY::prepare_local_timer_view(timer, now_us, CODY::SceneData::title_font,
            CODY::SceneData::body_font, ui.timer_view, frame.presentation.content);
    }
    if (ui.model.editing) {
        frame.layer = Layer::Visibility;
        frame.arrows = true;
        return CODY::prepare_widget_visibility_view(ui.model.editor_page, ui.model.widgets.visibility,
            ui.model.save == Save::Pending, ui.model.save == Save::Failed, ui.model.settings_error != ESP_OK,
            CODY::SceneData::title_font, CODY::SceneData::body_font, ui.editor, frame.presentation.content);
    }
    if (ui.codex_browser.route != CODY::CodexBrowserRoute::Closed &&
        ui.browser_destination == CODY::CodexBrowserDestination::None &&
        CODY::onboarding_codex_tools_active()) {
        frame.layer = Layer::CodexBrowser;
        frame.arrows = true;
        frame.navigation_opacity = 255;
        return CODY::prepare_codex_browser(ui.codex_browser, now_us, frame.presentation.content);
    }
    const bool mascot_waking = ui.pixel_demo.elapsed_us < CODY::kPixelDemoWakingUs;
    if (ui.mascot_home && (mascot_waking || !CODY::device_effects_require_attention(ui.model.effects))) {
        frame.layer = Layer::Mascot;
        return CODY::prepare_pixel_demo(ui.pixel_demo, ui.awake_us, frame.presentation.content);
    }
    if (CODY::onboarding_dashboard_active()) {
        auto &model = ui.model;
        CODY::update_device_statistics(model.dashboard);
        const auto &turn = model.dashboard.context;
        ui.current_turn_available = CODY::current_turn_has_data(turn, now_us);
        if (ui.activity_page == ActivityPage::CurrentTurn && !ui.current_turn_available) {
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
            if (browser_child) frame.navigation_opacity = 255;
            return CODY::prepare_codex_events(ui.codex_panel, ui.inbox, now_us, frame.presentation.content);
        }
        if (ui.activity_page == ActivityPage::CurrentTurn) {
            frame.layer = Layer::CurrentTurn;
            frame.arrows = true;
            frame.navigation_opacity = 255;
            return CODY::prepare_current_turn(ui.codex_panel, turn, now_us, frame.presentation.content);
        }
        if (ui.activity_page == ActivityPage::Reply) {
            frame.layer = Layer::EventReply;
            frame.arrows = true;
            frame.navigation_opacity = 255;
            return CODY::prepare_codex_reply(ui.codex_panel, ui.reply.title, ui.reply_dismiss_pending,
                frame.presentation.content);
        }
        ui.available_statistics = CODY::available_device_statistics(model.dashboard, model.widgets.visibility, now_us);
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
            frame.navigation_opacity = 255;
            return CODY::prepare_device_voucher_warning(ui.view, frame.presentation, ui.model);
        }
        if (ui.model.widgets.selected_screen == WidgetScreen::About &&
            ui.model.info_page == CODY::InfoPage::ProjectLink) {
            frame.layer = Layer::AboutLink;
            frame.arrows = true;
            const auto error = CODY::prepare_about_project_link(frame.presentation.content);
            if (error == ESP_OK) return ESP_OK;
            return CODY::prepare_device_message(ui.view, frame.presentation, "Scan for more info", "QR unavailable");
        }
        frame.layer = Layer::Dashboard;
        const auto screen = model.widgets.selected_screen;
        if (browser_child) frame.navigation_opacity = 255;
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
        return CODY::prepare_device_message(ui.view, frame.presentation, "Statistics", "Data unavailable", "BOOT opens Options");
    }
    if (ui.scene.valid) {
        frame.layer = Layer::Scene;
        frame.connection_activity_phase = CODY::onboarding_connection_activity_phase(now_us);
        if (CODY::onboarding_quick_guide_active()) frame.navigation_opacity = 255;
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
    const bool show_connection = (frame.layer == Layer::Dashboard && screen != WidgetScreen::Device) ||
        frame.layer == Layer::AboutLink || frame.layer == Layer::VoucherWarning;
    const bool show_battery = frame.layer != Layer::Blank && frame.layer != Layer::Branding &&
        frame.layer != Layer::Screensaver && frame.layer != Layer::Mascot;
    const auto saved_epoch = frame.layer == Layer::Dashboard && !frame.statistics_unavailable
        ? CODY::device_saved_page_epoch(ui.model.dashboard, screen) : 0;
    const auto chrome_error = CODY::prepare_device_chrome(ui.view.chrome, ui.model.dashboard, show_connection,
        show_battery ? ui.model.battery_banner : CODY::BatteryAlertNotice{}, saved_epoch);
    if (chrome_error != ESP_OK) return chrome_error;
    const auto &chrome = ui.view.chrome;
    const uint16_t allowance = show_battery ? CODY::device_allowance_ring(ui.model.dashboard) : 0;
    const auto &battery = ui.model.dashboard.connection.battery;
    const bool battery_current = show_battery && ui.model.dashboard.connection_valid &&
        battery.data_valid && !battery.data_stale && battery.last_error == ESP_OK &&
        battery.level_percent_valid && battery.level_percent >= 0 && battery.level_percent <= 100;
    const int8_t battery_percent = battery_current ? static_cast<int8_t>(battery.level_percent) : -1;
    const bool full = force || !old.valid || old.display_revision != revision ||
        old.layer != frame.layer || old.screen != screen ||
        old.allowance != allowance || old.battery_percent != battery_percent ||
        old.statistics_unavailable != frame.statistics_unavailable ||
        (frame.layer == Layer::Scene && ui.scene_dirty);
    const bool draws_navigation = frame.arrows || frame.layer == Layer::Scene;
    const uint8_t opacity = draws_navigation ? frame.navigation_opacity : 0;
    const bool dashboard_next = frame.layer != Layer::Dashboard || screen == WidgetScreen::About ||
        CODY::next_visible_widget(ui.available_statistics, screen, WidgetStep::Next) != screen;
    const bool next_navigation = frame.arrows && dashboard_next && frame.layer != Layer::AboutLink && frame.layer != Layer::Timer &&
        frame.layer != Layer::VoucherWarning && frame.layer != Layer::CurrentTurn &&
        frame.layer != Layer::EventReply && frame.layer != Layer::CodexBrowser && frame.layer != Layer::Events &&
        (frame.layer != Layer::Visibility || ui.editor.page + 1U < CODY::kVisibilityPageCount);
    const bool navigation_changed = old.navigation_opacity != opacity || old.next_navigation != next_navigation;
    const bool content_changed = old.content_revision != view.content.revision;
    const bool battery_changed = old.battery_sequence != chrome.battery_sequence;
    const bool connection_changed = old.connection_revision != chrome.connection_revision;
    const bool connection_activity_changed = old.connection_activity_phase != frame.connection_activity_phase;
    const bool voice_ribbon_changed = old.voice_ribbon_at_us != frame.voice_ribbon_at_us;
    const bool pixel_demo_changed = frame.layer == Layer::Mascot && !ui.pixel_demo.damage.empty();
    if (!full && !content_changed && !navigation_changed && !battery_changed && !connection_changed &&
        !connection_activity_changed && !voice_ribbon_changed && !pixel_demo_changed) {
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
        if (pixel_demo_changed) include_damage(ui.pixel_demo.damage);
        if (content_changed) {
            include_damage(view.content.bounds);
            include_damage(view.content.bottom_action_bounds);
        }
        if (navigation_changed) include_damage(navigation_bounds);
        if (battery_changed) include_damage({105, 397, 361, 427});
        if (connection_changed) include_damage({105, 37, 361, 64});
        if (connection_activity_changed) include_damage(CODY::kConnectionActivityBounds);
        if (voice_ribbon_changed) {
            include_damage({CODY::kVoiceRibbonLeft, CODY::kVoiceRibbonDamageTop,
                CODY::kVoiceRibbonLeft + CODY::kVoiceRibbonWidth, CODY::kVoiceRibbonDamageBottom});
        }
    }
    const uint16_t left = bounds.left & ~1U;
    const uint16_t right = (bounds.right + 1U) & ~1U;
    const uint16_t top = bounds.top & ~1U;
    const uint16_t bottom = (bounds.bottom + 1U) & ~1U;
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
        const uint16_t background = frame.layer == Layer::Scene ? ui.scene.definition->background_rgb565 : 0;
        error = CODY2D::clear(strip, background);
        if (error == ESP_OK && frame.layer == Layer::Branding) {
            error = CODY::BootBranding::draw_rows(ui.branding, ui.branding_scratch, ui.branding_frame, strip, row);
        } else if (error == ESP_OK && frame.layer == Layer::Scene) {
            error = CODY::draw_scene_rows(ui.scene, strip, row, left);
            if (error == ESP_OK && frame.connection_activity_phase != 0) {
                error = CODY::draw_connection_activity_rows(frame.connection_activity_phase, strip, row, left);
            }
        } else if (error == ESP_OK && view.content.draw_rows != nullptr) {
            error = view.content.draw_rows(view.content.state, strip, row, left);
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        if (error == ESP_OK && frame.voice_ribbon_at_us != 0) {
            error = CODY::draw_voice_ribbon_rows(strip, row, left, ui.voice_ribbon);
        }
#endif
        if (error == ESP_OK && (allowance != 0 || battery_percent >= 0)) {
            error = CODY::draw_device_perimeter_rows(allowance, battery_percent, strip, row, left);
        }
        if (error == ESP_OK && frame.layer == Layer::Scene) {
            error = CODY::draw_onboarding_navigation(strip, row, left, opacity, background);
        }
        if (error == ESP_OK && frame.arrows && row < navigation_bounds.bottom &&
            row + rows > navigation_bounds.top) {
            error = CODY::draw_lateral_navigation(next_navigation, strip, row, left, opacity);
        }
        if (error == ESP_OK) error = CODY::draw_device_chrome(chrome, strip, row, left);
        if (error == ESP_OK) {
            error = LUNASHELL_display_frame_submit_rgb565(left, row, right, row + rows, pixels);
        }
        row += rows;
    }

    error = LUNASHELL_display_frame_end(error);
    if (error != ESP_OK) {
        old.valid = false;
        return error;
    }
    if (!old.valid || old.display_revision != frame_revision || old.layer != frame.layer || old.screen != screen ||
        (old.content_revision != view.content.revision &&
         (frame.layer == Layer::Visibility || frame.layer == Layer::CodexBrowser ||
          ui.contact == Target::FirstAction || ui.contact == Target::SecondAction ||
          ui.contact == Target::VoucherScroll || ui.contact == Target::VoucherDismiss))) {
        ui.contact = Target::None;
    }
    if (old.navigation_opacity == 255 && opacity < 255) {
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
    if (frame.layer == Layer::Mascot) CODY::retire_pixel_demo(ui.pixel_demo);
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
Target target_at(CODY2D::Point point)
{
    const auto &ui = *s_ui;
    const auto layer = ui.retired.layer;
    if (layer == Layer::CodexBrowser) {
        if (CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
        return CODY::codex_browser_control_at(ui.codex_browser, point).action != CODY::CodexBrowserAction::None
            ? Target::CodexBrowserControl : Target::None;
    }
    if ((layer == Layer::Events || layer == Layer::Dashboard) &&
        ui.browser_destination != CODY::CodexBrowserDestination::None &&
        CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::CodexBrowserBack;
    if (layer == Layer::Events) return Target::EventsGesture;
    if (layer == Layer::CurrentTurn || layer == Layer::EventReply) {
        if (CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::ActivityBack;
        if (layer == Layer::EventReply && !ui.reply_dismiss_pending &&
            point.x >= 113 && point.x < 353 && point.y >= 330 && point.y < 386) return Target::EventDismiss;
        return Target::None;
    }
    if (layer == Layer::VoucherWarning) {
        if (CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::VoucherDismiss;
        return CODY::device_action_at(ui.view, point) == 1 ? Target::VoucherDismiss : Target::None;
    }
    if (layer == Layer::Timer) {
        if (ui.retired.navigation_opacity == 255 &&
            CODY::lateral_navigation_at(point) == CODY::LateralNavigation::Previous) return Target::Previous;
        switch (CODY::local_timer_control_at(ui.timer_view, point)) {
        case CODY::LocalTimerControl::Decrease: return Target::TimerDecrease;
        case CODY::LocalTimerControl::Increase: return Target::TimerIncrease;
        case CODY::LocalTimerControl::CycleSound: return Target::TimerSound;
        case CODY::LocalTimerControl::Start: return Target::TimerStart;
        case CODY::LocalTimerControl::Cancel: return Target::TimerCancel;
        case CODY::LocalTimerControl::Dismiss: return Target::TimerDismiss;
        default: return Target::None;
        }
    }
    if (ui.retired.navigation_opacity == 255 &&
        (layer == Layer::Visibility || layer == Layer::Dashboard || layer == Layer::AboutLink)) {
        switch (CODY::lateral_navigation_at(point)) {
        case CODY::LateralNavigation::Previous: return Target::Previous;
        case CODY::LateralNavigation::Next: return ui.retired.next_navigation ? Target::Next : Target::None;
        default: break;
        }
    }
    if (layer == Layer::Visibility) {
        const auto row = CODY::widget_visibility_row_at(ui.editor, point);
        return row ? kVisibilityTargets[*row] : Target::None;
    }
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
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (layer == Layer::Dashboard && ui.model.dashboard.voice_ready &&
        point.x >= 73 && point.x < 393 && point.y >= 73 && point.y < 393) {
        return Target::VoiceGesture;
    }
#endif
    return Target::None;
}

// target: Released setting control from the retired configuration view.
void edit_visibility(Target target)
{
    auto &ui = *s_ui;
    if (ui.model.save == Save::Pending) return;
    const auto row = std::find(kVisibilityTargets.begin(), kVisibilityTargets.end(), target);
    if (row != kVisibilityTargets.end()) {
        const auto screen = ui.editor.rows[row - kVisibilityTargets.begin()].screen;
        ui.model.draft = ui.model.widgets.visibility;
        if (!CODY::set_widget_visibility(ui.model.draft, screen, !CODY::widget_visible(ui.model.draft, screen))) return;
        const auto error = CODY::request_widget_visibility_save(ui.model.draft);
        ui.model.save = error == ESP_OK ? Save::Pending : Save::Failed;
    } else if (target == Target::Previous && ui.model.editor_page == 0) {
        ui.model.editing = false;
    } else if (target == Target::Previous) {
        --ui.model.editor_page;
        ui.model.save = Save::Idle;
    } else if (target == Target::Next && ui.model.editor_page + 1U < CODY::kVisibilityPageCount) {
        ++ui.model.editor_page;
        ui.model.save = Save::Idle;
    }
    // The visibility presenter revisions its central content; layer changes already
    // trigger a full repaint. A local edit must not invalidate the whole panel.
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
    const auto layer = ui.retired.layer;
    if (target == Target::CodexBrowserBack) {
        if (layer == Layer::CodexBrowser) {
            CODY::back_codex_browser(ui.codex_browser);
        } else {
            const auto error = CODY::open_companion_options(ui.scene);
            if (error != ESP_OK) return error;
            if (!CODY::onboarding_codex_tools_active()) return ESP_OK;
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            CODY::open_codex_browser(ui.codex_browser);
        }
        invalidate();
    } else if (target == Target::CodexBrowserControl && layer == Layer::CodexBrowser) {
        const auto start = CODY::codex_browser_control_at(ui.codex_browser, ui.activity_touch_start);
        const auto end = CODY::codex_browser_control_at(ui.codex_browser, ui.activity_touch_last);
        if (start.action == CODY::CodexBrowserAction::None || !(start == end)) return ESP_OK;
        const auto destination = CODY::activate_codex_browser(ui.codex_browser, start);
        if (destination != CODY::CodexBrowserDestination::None) {
            const auto error = CODY::open_device_dashboard(ui.scene);
            if (error == ESP_ERR_NOT_FINISHED) return ESP_OK;
            if (error != ESP_OK) return error;
            ui.browser_destination = destination;
            if (destination == CODY::CodexBrowserDestination::Statistics) {
                select_statistics();
            } else {
                ui.activity_page = ActivityPage::Events;
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
    } else if (layer == Layer::Visibility) {
        edit_visibility(target);
    } else if (layer == Layer::Timer) {
        switch (target) {
        case Target::Previous:
            ui.timer_open = false;
            invalidate();
            break;
        case Target::TimerDecrease: CODY::adjust_local_timer(ui.model.timer, -1); break;
        case Target::TimerIncrease: CODY::adjust_local_timer(ui.model.timer, 1); break;
        case Target::TimerSound: CODY::cycle_local_timer_sound(ui.model.timer); break;
        case Target::TimerStart: {
            const auto error = CODY::start_local_timer(ui.model.timer, esp_timer_get_time());
            // Expiry or a route change can retire a previously admitted action.
            if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
            if (error == ESP_OK) ui.model.effects.feedback = CODY::DeviceFeedback::TimerStarted;
            break;
        }
        case Target::TimerCancel: CODY::cancel_local_timer(ui.model.timer); break;
        case Target::TimerDismiss: CODY::dismiss_local_timer(ui.model.timer); break;
        default: break;
        }
    } else if (layer == Layer::AboutLink) {
        ui.model.info_page = CODY::InfoPage::Firmware;
        invalidate();
    } else if (target == Target::Previous || target == Target::Next) {
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
    } else if (layer == Layer::Dashboard &&
        (target == Target::VoiceGesture || target == Target::FirstAction)) {
        const int64_t now_us = esp_timer_get_time();
        auto &model = ui.model;
        if (target == Target::VoiceGesture) {
            if (now_us - model.voice_touch_started_us > 220000) {
                model.voice_tapped_us = 0;
                return ESP_OK;
            }
            if (model.voice_tapped_us == 0 || now_us - model.voice_tapped_us > 350000) {
                model.voice_tapped_us = now_us;
                return ESP_OK;
            }
        }
        model.voice_tapped_us = 0;
        bool started = false;
        const auto trigger = target == Target::VoiceGesture ?
            LUNASHELL::VoiceInputTrigger::DoubleTap : LUNASHELL::VoiceInputTrigger::DeviceButton;
        const auto error = LUNASHELL::requestVoiceInput(trigger, &started);
        if (error != ESP_OK) ESP_LOGW(kTag, "Voice request unavailable (%s)", esp_err_to_name(error));
    }
#endif
    return ESP_OK;
}

// ui: Retained audio and timer state owned by the display worker.
void play_touch_cue(DeviceUi &ui)
{
    const auto alarm = ui.model.timer.alarm;
    if (alarm == CODY::LocalTimerAlarm::Pending || alarm == CODY::LocalTimerAlarm::Playing) return;
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    CODY::play_device_tap_cue(ui.model.effects, voice);
}

// No parameters: read touch and dispatch it only to the retired visible route.
// Exception: Returns a mandatory input acquisition or route error.
esp_err_t sample_touch()
{
    auto &ui = *s_ui;
    LUNASHELL_touch_point_t sample{};
    const esp_err_t read = LUNASHELL_touch_read_point(&sample);
    if (read == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (read == ESP_ERR_INVALID_RESPONSE || read == ESP_ERR_TIMEOUT) {
        ui.pressed = false;
        invalidate();
        return ESP_OK;
    }
    if (read != ESP_OK) return read;
    const bool was_pressed = ui.pressed;
    const bool pressed_edge = sample.pressed && !was_pressed;
    ui.pressed = sample.pressed;
    if (!ui.retired.valid || ui.retired.display_revision != LUNASHELL_display_runtime_content_revision()) {
        ui.contact = Target::None;
        ui.pixel_demo.touch_candidate = false;
        return ESP_OK;
    }
    const CODY2D::Point point{static_cast<int16_t>(sample.x), static_cast<int16_t>(sample.y)};
    if (sample.pressed || pressed_edge) ui.model.navigation.interact(esp_timer_get_time());
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
    if (ui.retired.layer == Layer::Mascot) {
        if (CODY::observe_pixel_demo_touch(ui.pixel_demo, point, sample.pressed, pressed_edge,
                sample.sample_timestamp_us, ui.retired.display_revision)) {
            play_touch_cue(ui);
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
    esp_err_t saved = ESP_OK;
    if (CODY::take_widget_visibility_save_result(saved)) {
        ui.model.save = saved == ESP_OK && CODY::apply_widget_visibility(ui.model.widgets, ui.model.draft)
            ? Save::Idle : Save::Failed;
        // The cached editor detects Pending -> Idle/Failed and redraws its own bounds.
    }
    bool can_open_options = ui.model.save != Save::Pending;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (can_open_options && s_options_requested.load(std::memory_order_acquire)) {
        // An ongoing recording retains its Cancel/Send controls before BOOT opens Options.
        can_open_options = !LUNASHELL::setVoiceInputRouteAvailable(false);
    }
#endif
    if (can_open_options && s_options_requested.exchange(false, std::memory_order_acq_rel)) {
        const bool already_open = ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours &&
            !ui.model.editing && !ui.timer_open && !ui.diagnostics;
        ui.model.editing = false;
        ui.timer_open = false;
        ui.model.vouchers.warning_expiry = 0;
        CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
        ui.diagnostics = false;
        ui.codex_browser.route = CODY::CodexBrowserRoute::Closed;
        ui.browser_destination = CODY::CodexBrowserDestination::None;
        // A busy setup route can decline Options. Reveal that route so pairing remains reachable.
        ui.mascot_home = false;
        const esp_err_t error = CODY::open_companion_options(ui.scene);
        if (error != ESP_OK) return error;
        if (!already_open && ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours) {
            ui.model.effects.feedback = CODY::DeviceFeedback::OptionsOpened;
        }
        invalidate();
    }
    const auto request = CODY::take_widget_open_request();
    switch (request) {
    case CODY::WidgetOpenRequest::CodexBrowser:
        if (CODY::onboarding_codex_tools_active()) {
            CODY::open_codex_browser(ui.codex_browser);
            ui.browser_destination = CODY::CodexBrowserDestination::None;
            invalidate();
        }
        break;
    case CODY::WidgetOpenRequest::Home:
    case CODY::WidgetOpenRequest::Statistics: {
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
    case CODY::WidgetOpenRequest::Visibility:
        ui.model.editor_page = 0;
        ui.model.save = Save::Idle;
        ui.model.editing = true;
        invalidate();
        break;
    case CODY::WidgetOpenRequest::Timer:
        ui.timer_open = true;
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
    const auto error = CODY::update_onboarding(ui.scene, ui.completed_scene, ui.awake_us, ui.awake_us, changed);
    if (changed) {
        ui.scene_dirty = true;
        if (ui.model.editing && ui.scene.valid && ui.scene.definition->id != CODY::SceneId::MakeItYours) {
            ui.model.editing = false;
            invalidate();
        }
        if (ui.timer_open && !CODY::onboarding_dashboard_active() &&
            (!ui.scene.valid || ui.scene.definition->id != CODY::SceneId::MakeItYours)) {
            // Remote attention or setup can replace the view without stopping the timer.
            ui.timer_open = false;
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
    CODY::dismiss_screensaver(ui.model.screensaver, now_us);
    CODY::suspend_companion_shake(ui.model.shake, now_us);
    if (command == Command::Ready || command == Command::Wake) ui.model.navigation.interact(now_us);
    FrameView frame{};
    if (command == Command::Ready) {
        const auto error = CODY::start_device_setup(ui.scene, ui.first_boot);
        if (error != ESP_OK) return error;
        select_statistics();
        ui.statistics_entry = false;
        ui.activity_page = ActivityPage::Events;
        ui.mascot_home = true;
        ui.pixel_demo.showcase.reset(static_cast<uint32_t>(now_us));
    }
    if (command == Command::Sleep || command == Command::Shutdown) {
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
    return error == ESP_OK ? render(frame, true) : error;
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
    const auto navigation = CODY::update_device_navigation(ui.model, now_us);
    bool may_return_home = navigation.shaken && !ui.mascot_home && CODY::onboarding_dashboard_active() &&
        !ui.model.editing && !ui.timer_open && !ui.diagnostics;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    may_return_home = may_return_home && !voice_visible(now_us);
#endif
    if (may_return_home) {
        LUNASHELL_power_manager_note_activity();
        CODY::dismiss_screensaver(ui.model.screensaver, now_us);
        select_statistics();
    }

#if CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    LUNASHELL::CodexDebugScreenRequest debug{};
    const auto debug_result = LUNASHELL::takeCodexDebugScreen(debug);
    if (debug_result == ESP_OK) {
        ui.diagnostics = debug.screen == LUNASHELL::CodexDebugScreen::Character;
        invalidate();
    } else if (debug_result != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(kTag, "Diagnostics request unavailable (%s)", esp_err_to_name(debug_result));
    }
#endif
    FrameView frame{};
    error = prepare_current_view(frame, now_us, navigation.motion_error);
    if (error == ESP_OK) error = render(frame);
#if CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
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
    } else if (esp_timer_get_time() < ui.branding_due_us) {
        return ESP_OK;
    }
    const uint16_t next = starting ? 0 : static_cast<uint16_t>(ui.branding_frame + 1U);
    if (next == VIBEWARE_BOOT_FRAME_COUNT) {
        ui.branding_phase = BrandingPhase::Inactive;
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
            ESP_LOGI(kTag, "Boot disk visible cycle started after service readiness");
            (void)CODY::play_device_boot_cue(ui.model.effects);
        }
    } else if (error == ESP_ERR_NOT_FINISHED) {
        ui.branding_frame = previous;
    }
    return error;
}

// ui: Worker-owned timer and retained navigation state.
// now_us: Monotonic time before processing any potentially overdue touch release.
void advance_local_timer(DeviceUi &ui, int64_t now_us)
{
    if (CODY::update_local_timer(ui.model.timer, now_us) && CODY::onboarding_dashboard_active() &&
        !ui.model.editing && !ui.diagnostics) {
        ui.timer_open = true;
        ui.model.navigation.interact(now_us);
        invalidate();
    }
}

// context: Unused; the task exclusively owns native navigation and presentation.
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
        const uint32_t request = s_command.exchange(0, std::memory_order_acq_rel);
        if (request != 0) {
            const auto result = apply_command(static_cast<Command>(request & 0xffU));
            s_command_result.store(result, std::memory_order_relaxed);
            s_ack.store(request, std::memory_order_release);
        }
        advance_local_timer(ui, esp_timer_get_time());
        if (!s_ready.load(std::memory_order_acquire) || ui.asleep ||
            !LUNASHELL_display_runtime_writes_allowed()) {
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
            continue;
        }
        const bool animating = ui.retired.voice_ribbon_at_us != 0 || ui.view.countdown.animating();
        uint32_t wait_ms = kPollMs;
        if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible) {
            wait_ms = static_cast<uint32_t>(CODY::kScreensaverFrameUs / 1000);
        } else if (animating) {
            wait_ms = 33;
        } else if (ui.retired.valid && ui.retired.layer == Layer::Mascot) {
            wait_ms = CODY::kPixelAvatarFrameMs;
        }
        const auto wait = LUNASHELL_touch_wait_for_report(wait_ms);
        if (wait != ESP_OK && wait != ESP_ERR_NOT_FOUND) {
            error = wait;
            break;
        }
        if (s_command.load(std::memory_order_acquire) != 0) continue;
        advance_local_timer(ui, esp_timer_get_time());
        if (wait == ESP_OK || ui.pressed) error = sample_touch();
        if (error == ESP_OK) error = update_and_draw(esp_timer_get_time());
        if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
    }
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
    CODY::initialize_device_uptime(s_ui->model.dashboard);
    CODY::OnboardingSettings settings{};
    auto error = CODY::load_onboarding_settings(settings);
    if (error == ESP_OK) error = CODY::load_companion_preferences();
    if (error == ESP_OK) error = CODY::onboarding_service_init();
    if (error != ESP_OK) return error;
    // The acquisition owner has initialized the codec; prepare its shared notification worker.
    // A failed optional sound worker does not remove pairing or power controls.
    (void)CODY::prepare_device_effects(s_ui->model.effects);
    s_ui->first_boot = settings.effective_first_boot;
    CODY::WidgetVisibility visibility{};
    s_ui->model.settings_error = CODY::load_widget_visibility(visibility);
    if (s_ui->model.settings_error == ESP_OK) CODY::apply_widget_visibility(s_ui->model.widgets, visibility);
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
    if (error == ESP_OK) error = LUNASHELL_power_manager_register_boot_options(queue_options);
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

