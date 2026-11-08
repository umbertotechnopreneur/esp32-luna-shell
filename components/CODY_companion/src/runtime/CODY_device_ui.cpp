#include "CODY_companion.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

#include "CODY_boot_branding.hpp"
#include "CODY_codex_audio.hpp"
#include "CODY_device_view.hpp"
#include "CODY_device_effects.hpp"
#include "CODY_screensaver.hpp"
#include "CODY_onboarding.hpp"
#include "CODY_settings.hpp"
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
constexpr uint16_t kCanvas = 466;
constexpr uint32_t kFrameBudgetMs = 500;
constexpr uint32_t kPollMs = 100;
constexpr uint32_t kPowerTimeoutMs = 2500;
// Ready also covers any remaining startup art while the radio owners release the display.
constexpr uint32_t kReadyTimeoutMs = kPowerTimeoutMs + VIBEWARE_BOOT_DURATION_MS +
    VIBEWARE_BOOT_FRAME_COUNT * (kFrameBudgetMs + kPollMs);
// The onboarding and CCP projections still use the existing display task stack budget.
constexpr uint32_t kWorkerStackBytes = 12288;

enum class Layer : uint8_t { Branding, Message, Scene, Dashboard, AboutLink, Visibility, Voice, Diagnostics, Screensaver, Blank };
enum class Command : uint8_t { Ready = 1, Sleep, Wake, Shutdown, ShutdownFailed };
using Save = CODY::VisibilitySave;
enum class Target : uint8_t { None, Previous, Next, Toggle, Interval, FirstAction, SecondAction, VoiceGesture };

struct FrameView {
    CODY::DeviceFrameContent presentation{};
    Layer layer = Layer::Blank;
    uint8_t navigation_opacity = 0;
    bool arrows = false;
    bool statistics_unavailable = false;
};

struct RetiredFrame {
    Layer layer = Layer::Blank;
    WidgetScreen screen = WidgetScreen::Device;
    uint32_t display_revision = 0;
    uint32_t content_revision = 0;
    uint32_t battery_sequence = 0;
    const char *connection_text = nullptr;
    uint16_t allowance = 0;
    uint8_t navigation_opacity = 0;
    bool statistics_unavailable = false;
    bool valid = false;
};

struct DeviceUi {
    CODY::DeviceModel model{};
    CODY::SceneFrame scene{};
    CODY::SceneFrame completed_scene{};
    CODY::WidgetVisibilityView editor{};
    CODY::BootBranding::Presentation branding{};
    CODY::BootBranding::Scratch branding_scratch{};
    int64_t branding_due_us = 0;
    uint16_t branding_frame = 0;
    bool branding_active = false;

    CODY::DeviceView view{};
    RetiredFrame retired{};
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    int64_t presented_voice_session = 0;
#endif
    int64_t last_active_us = 0;
    int64_t awake_us = 0;
    uint32_t standby_override = 0;
    Target contact = Target::None;
    bool pressed = false;
    bool scene_dirty = true;
    bool asleep = false;
    bool diagnostics = false;
    bool first_boot = false;
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
    s_ui->model.info_page = CODY::InfoPage::System;
    const auto page = CODY::first_visible_widget(s_ui->model.widgets.visibility, CODY::WidgetScope::Statistics);
    CODY::select_widget_screen(s_ui->model.widgets, page.value_or(WidgetScreen::Device));
    invalidate();
}

// No parameters: physical BOOT posts an intent for the display owner.
void queue_options()
{
    s_options_requested.store(true, std::memory_order_release);
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
using VoicePhase = LUNASHELL::VoiceInputPhase;

// now_us: Monotonic time for the bounded terminal audio-delivery caption.
bool voice_visible(int64_t now_us)
{
    const auto &input = s_ui->model.voice.input;
    switch (input.phase) {
    case VoicePhase::Idle: return false;
    case VoicePhase::Delivered:
    case VoicePhase::Cancelled:
    case VoicePhase::Unconfirmed:
    case VoicePhase::Failed: return now_us - input.changed_at_us < 3000000;
    default: return true;
    }
}
#endif

// frame: Receives one visible surface and its native content.
// now_us: Monotonic time for the reset and recording views.
// Exception: Returns original native preparation failures.
esp_err_t prepare_current_view(FrameView &frame, int64_t now_us)
{
    auto &ui = *s_ui;
    CODY::update_device_observations(ui.model.dashboard);
    CODY::update_device_battery(ui.model, now_us);
    const LUNASHELL::VoiceSnapshot *voice = nullptr;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    voice = &ui.model.voice;
#endif
    // Optional sound failures remain in the effects model; visual controls keep working.
    (void)CODY::update_device_effects(ui.model.effects, ui.model.dashboard.connection,
        ui.model.dashboard.connection_valid, ui.model.battery_banner, voice, now_us);
    bool saver_eligible = CODY::onboarding_dashboard_active() && !ui.model.editing && !ui.diagnostics &&
        !CODY::device_effects_require_attention(ui.model.effects) && ui.model.battery_banner.sequence == 0 &&
        !(ui.model.widgets.selected_screen == WidgetScreen::About && ui.model.info_page == CODY::InfoPage::ProjectLink);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    saver_eligible = saver_eligible && !voice_visible(now_us);
#endif
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
        return CODY::prepare_device_voice(ui.view, frame.presentation, ui.model.voice.input);
    }
#endif
    if (ui.diagnostics) {
        frame.layer = Layer::Diagnostics;
        const auto error = DeviceSnapshotService::instance().readDiagnosticsObservation(ui.model.diagnostics);
        return CODY::prepare_device_diagnostics(ui.view, frame.presentation, ui.model.diagnostics, error == ESP_OK);
    }
    if (ui.model.editing) {
        frame.layer = Layer::Visibility;
        frame.arrows = true;
        return CODY::prepare_widget_visibility_view(ui.model.editor_screen, ui.model.widgets.visibility,
            ui.model.save == Save::Pending, ui.model.save == Save::Failed, ui.model.settings_error != ESP_OK,
            CODY::SceneData::title_font, CODY::SceneData::body_font, ui.editor, frame.presentation.content);
    }
    if (CODY::onboarding_dashboard_active()) {
        if (ui.model.widgets.selected_screen == WidgetScreen::About &&
            ui.model.info_page == CODY::InfoPage::ProjectLink) {
            frame.layer = Layer::AboutLink;
            frame.arrows = true;
            const auto error = CODY::prepare_about_project_link(frame.presentation.content);
            if (error == ESP_OK) return ESP_OK;
            return CODY::prepare_device_message(ui.view, frame.presentation, "Scan for more info", "QR unavailable");
        }
        frame.layer = Layer::Dashboard;
        frame.arrows = true;
        auto &model = ui.model;
        const auto screen = model.widgets.selected_screen;
        if (screen == WidgetScreen::About) {
            // Per-field source errors are rendered locally; uptime and navigation remain usable.
            (void)CODY::read_device_info(model);
            return CODY::prepare_device_info(ui.view, frame.presentation, model);
        }
        const auto read_error = CODY::read_device_dashboard(model.dashboard, screen, model.interval);
        const auto error = read_error == ESP_OK ? CODY::prepare_device_dashboard(ui.view, frame.presentation,
            screen, model.interval, model.dashboard, now_us) : read_error;
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
    auto &old = ui.retired;
    const auto &view = frame.presentation;
    const auto revision = LUNASHELL_display_runtime_content_revision();
    const auto screen = ui.model.widgets.selected_screen;
    const bool show_connection = (frame.layer == Layer::Dashboard && screen != WidgetScreen::Device) ||
        frame.layer == Layer::AboutLink;
    const bool show_battery = frame.layer != Layer::Blank && frame.layer != Layer::Branding &&
        frame.layer != Layer::Screensaver;
    const auto chrome_error = CODY::prepare_device_chrome(ui.view.chrome, ui.model.dashboard, show_connection,
        show_battery ? ui.model.battery_banner : CODY::BatteryAlertNotice{});
    if (chrome_error != ESP_OK) return chrome_error;
    const auto &chrome = ui.view.chrome;
    const uint16_t allowance = frame.layer == Layer::Blank || frame.layer == Layer::Branding ||
        frame.layer == Layer::Screensaver
        ? 0 : CODY::device_allowance_ring(ui.model.dashboard);
    const bool full = force || !old.valid || old.display_revision != revision ||
        old.layer != frame.layer || old.screen != screen ||
        old.allowance != allowance || old.statistics_unavailable != frame.statistics_unavailable ||
        (frame.layer == Layer::Scene && ui.scene_dirty);
    const bool draws_navigation = frame.arrows || frame.layer == Layer::Scene;
    const uint8_t opacity = draws_navigation ? frame.navigation_opacity : 0;
    const bool navigation_changed = old.navigation_opacity != opacity;
    const bool content_changed = old.content_revision != view.content.revision;
    const bool battery_changed = old.battery_sequence != chrome.battery_sequence;
    const bool connection_changed = old.connection_text != chrome.connection_text;
    if (!full && !content_changed && !navigation_changed && !battery_changed && !connection_changed) return ESP_OK;

    bool parked = false;
    esp_err_t error = LUNASHELL_display_frame_begin(kFrameBudgetMs, &parked);
    if (error != ESP_OK) {
        old.valid = false;
        return parked ? ESP_ERR_NOT_FINISHED : error;
    }

    const uint32_t frame_revision = LUNASHELL_display_runtime_content_revision();
    const bool redraw_all = full || frame_revision != revision;
    constexpr CODY::DamageRect navigation_bounds{0, 202, kCanvas, 264};
    auto bounds = redraw_all ? CODY::DamageRect{0, 0, kCanvas, kCanvas} : CODY::DamageRect{};
    const auto include_damage = [&bounds](CODY::DamageRect changed) {
        if (changed.empty()) return;
        if (bounds.empty()) bounds = changed;
        else bounds = {std::min(bounds.left, changed.left), std::min(bounds.top, changed.top),
            std::max(bounds.right, changed.right), std::max(bounds.bottom, changed.bottom)};
    };
    if (!redraw_all) {
        if (content_changed) {
            include_damage(view.content.bounds);
            include_damage(view.content.bottom_action_bounds);
        }
        if (navigation_changed) include_damage(navigation_bounds);
        if (battery_changed) include_damage({105, 397, 361, 427});
        if (connection_changed) include_damage({105, 37, 361, 64});
    }
    const uint16_t left = bounds.left & ~1U;
    const uint16_t right = (bounds.right + 1U) & ~1U;
    const uint16_t top = bounds.top & ~1U;
    const uint16_t bottom = (bounds.bottom + 1U) & ~1U;
    const uint16_t width = right - left;
    if (left >= right || right > kCanvas || top >= bottom || bottom > kCanvas) error = ESP_ERR_INVALID_SIZE;
    for (uint16_t row = top; error == ESP_OK && row < bottom;) {
        void *pixels = nullptr;
        size_t capacity = 0;
        error = LUNASHELL_display_frame_buffer(&pixels, &capacity);
        if (error != ESP_OK) break;
        // The driver admits rows by full panel width even for a narrow update.
        const size_t max_rows = (capacity / (kCanvas * sizeof(uint16_t))) & ~size_t{1};
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
            if (error == ESP_OK) error = CODY::draw_onboarding_navigation(strip, row, left, opacity, background);
        } else if (error == ESP_OK && view.content.draw_rows != nullptr) {
            error = view.content.draw_rows(view.content.state, strip, row, left);
        }
        if (error == ESP_OK && frame.arrows && row < navigation_bounds.bottom &&
            row + rows > navigation_bounds.top) {
            error = CODY::draw_lateral_navigation(frame.layer != Layer::AboutLink, strip, row, left, opacity);
        }
        if (error == ESP_OK) error = CODY::draw_device_chrome(chrome, strip, row, left);
        if (error == ESP_OK && allowance != 0) {
            error = CODY::draw_reset_countdown_ring_rows(allowance, strip, row, left);
        }
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
         (ui.contact == Target::FirstAction || ui.contact == Target::SecondAction))) {
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
        chrome.connection_text, allowance, opacity, frame.statistics_unavailable, true};
    if (frame.layer == Layer::Scene) {
        ui.completed_scene = ui.scene;
        ui.scene_dirty = false;
    }
    return ESP_OK;
}

// point: Logical position on the last fully retired page.
Target target_at(CODY2D::Point point)
{
    const auto &ui = *s_ui;
    const auto layer = ui.retired.layer;
    if (ui.retired.navigation_opacity == 255 &&
        (layer == Layer::Visibility || layer == Layer::Dashboard || layer == Layer::AboutLink)) {
        switch (CODY::lateral_navigation_at(point)) {
        case CODY::LateralNavigation::Previous: return Target::Previous;
        case CODY::LateralNavigation::Next: return layer == Layer::AboutLink ? Target::None : Target::Next;
        default: break;
        }
    }
    if (layer == Layer::Visibility) {
        return CODY::widget_visibility_control_at(ui.editor, point) == CODY::WidgetVisibilityControl::Toggle
            ? Target::Toggle : Target::None;
    }
    if (layer == Layer::Dashboard && ui.retired.statistics_unavailable) return Target::None;
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
    if (target == Target::Toggle) {
        ui.model.draft = ui.model.widgets.visibility;
        if (!CODY::set_widget_visibility(ui.model.draft, ui.model.editor_screen,
                !CODY::widget_visible(ui.model.draft, ui.model.editor_screen))) return;
        const auto error = CODY::request_widget_visibility_save(ui.model.draft);
        ui.model.save = error == ESP_OK ? Save::Pending : Save::Failed;
    } else if (target == Target::Previous && ui.model.editor_screen == WidgetScreen::Reset) {
        ui.model.editing = false;
    } else {
        const auto step = target == Target::Previous ? WidgetStep::Previous : WidgetStep::Next;
        ui.model.editor_screen = CODY::next_visible_widget({}, ui.model.editor_screen, step,
            CODY::WidgetScope::Statistics).value_or(ui.model.editor_screen);
        ui.model.save = Save::Idle;
    }
    // The visibility presenter revisions its central content; layer changes already
    // trigger a full repaint. A local edit must not invalidate the whole panel.
}

// target: Released control admitted on the same complete frame.
// Exception: Returns a real voice transport admission error.
esp_err_t activate(Target target)
{
    auto &ui = *s_ui;
    const auto layer = ui.retired.layer;
    if (layer == Layer::Visibility) {
        edit_visibility(target);
    } else if (layer == Layer::AboutLink) {
        ui.model.info_page = CODY::InfoPage::Firmware;
        invalidate();
    } else if (target == Target::Previous || target == Target::Next) {
        const auto step = target == Target::Previous ? WidgetStep::Previous : WidgetStep::Next;
        const bool info_changed = ui.model.widgets.selected_screen == WidgetScreen::About &&
            CODY::step_info_page(ui.model.info_page, step);
        if (!info_changed) {
            const auto page = CODY::next_visible_widget(ui.model.widgets.visibility, ui.model.widgets.selected_screen, step);
            if (page) {
                CODY::select_widget_screen(ui.model.widgets, *page);
                ui.model.info_page = CODY::InfoPage::System;
            }
        }
        invalidate();
    } else if (target == Target::Interval) {
        using Interval = CODY::TokenHistoryInterval;
        switch (ui.model.interval) {
        case Interval::OneHour: ui.model.interval = Interval::FiveHours; break;
        case Interval::FiveHours: ui.model.interval = Interval::TwentyFourHours; break;
        case Interval::TwentyFourHours: ui.model.interval = Interval::OneHour; break;
        }
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
    const bool pressed_edge = sample.pressed && !ui.pressed;
    ui.pressed = sample.pressed;
    if (!ui.retired.valid || ui.retired.display_revision != LUNASHELL_display_runtime_content_revision()) {
        ui.contact = Target::None;
        return ESP_OK;
    }
    const CODY2D::Point point{static_cast<int16_t>(sample.x), static_cast<int16_t>(sample.y)};
    if (sample.pressed || pressed_edge) ui.model.navigation.interact(esp_timer_get_time());
    if (pressed_edge) LUNASHELL_power_manager_note_activity();
    if (ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible || ui.retired.layer == Layer::Screensaver) {
        CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
        // The wake touch owns no underlying target, including its eventual release.
        ui.contact = Target::None;
        invalidate();
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
        if (pressed_edge) ui.contact = target;
        else if (ui.contact != target) ui.contact = Target::None;
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
    if (ui.model.save != Save::Pending && s_options_requested.exchange(false, std::memory_order_acq_rel)) {
        ui.model.editing = false;
        ui.model.info_page = CODY::InfoPage::System;
        CODY::dismiss_screensaver(ui.model.screensaver, esp_timer_get_time());
        ui.diagnostics = false;
        const esp_err_t error = CODY::open_companion_options(ui.scene);
        if (error != ESP_OK) return error;
        invalidate();
    }
    switch (CODY::take_widget_open_request()) {
    case CODY::WidgetOpenRequest::Statistics: {
        const auto error = CODY::open_device_dashboard(ui.scene);
        if (error == ESP_ERR_NOT_FINISHED) break;
        if (error != ESP_OK) return error;
        select_statistics();
        break;
    }
    case CODY::WidgetOpenRequest::Visibility:
        ui.model.editor_screen = WidgetScreen::Reset;
        ui.model.save = Save::Idle;
        ui.model.editing = true;
        invalidate();
        break;
    default: break;
    }
    bool changed = false;
    const auto error = CODY::update_onboarding(ui.scene, ui.completed_scene, ui.awake_us, ui.awake_us, changed);
    if (changed) {
        ui.scene_dirty = true;
        if (ui.model.editing && ui.scene.valid && ui.scene.definition->id != CODY::SceneId::MakeItYours) {
            ui.model.editing = false;
            invalidate();
        }
    }
    return error;
}

// No parameters: apply the current setup timeout through the existing power owner.
esp_err_t apply_standby_override()
{
    auto &ui = *s_ui;
    const uint32_t seconds = !ui.asleep && ui.scene.valid && !CODY::onboarding_dashboard_active()
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
    }
    if (command == Command::Sleep || command == Command::Shutdown) {
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
    const bool shaken = CODY::update_device_navigation(ui.model, now_us);
    const bool safe_options = ui.scene.valid && ui.scene.definition->id == CODY::SceneId::MakeItYours &&
        ui.model.save != Save::Pending;
    bool may_return_home = shaken && (safe_options || CODY::onboarding_dashboard_active());
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    may_return_home = may_return_home && !voice_visible(now_us);
#endif
    if (may_return_home) {
        if (safe_options) {
            error = CODY::open_device_dashboard(ui.scene);
            if (error != ESP_OK && error != ESP_ERR_NOT_FINISHED) return error;
            may_return_home = error == ESP_OK;
        }
    }
    if (may_return_home) {
        ui.model.editing = false;
        ui.diagnostics = false;
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
    error = prepare_current_view(frame, now_us);
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
        if (ui.model.voice.input.phase == VoicePhase::Preparing) {
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
    if (esp_timer_get_time() < ui.branding_due_us || !LUNASHELL_display_runtime_writes_allowed()) {
        return ESP_OK;
    }
    const auto next = static_cast<uint16_t>(ui.branding_frame + 1U);
    if (next == VIBEWARE_BOOT_FRAME_COUNT) {
        ui.branding_active = false;
        ESP_LOGI(kTag, "Boot disk animation completed: %u frames retired", VIBEWARE_BOOT_FRAME_COUNT);
        return ESP_OK;
    }
    ui.branding_frame = next;
    FrameView frame{};
    frame.layer = Layer::Branding;
    const auto error = render(frame, true);
    if (error == ESP_OK) {
        ui.branding_due_us = esp_timer_get_time() + VIBEWARE_boot_frames[next].duration_ms * 1000LL;
    } else if (error == ESP_ERR_NOT_FINISHED) {
        --ui.branding_frame;
    }
    return error;
}

// context: Unused; the task exclusively owns native navigation and presentation.
void device_ui_worker(void *context)
{
    (void)context;
    auto &ui = *s_ui;
    esp_err_t error = ESP_OK;
    while (error == ESP_OK) {
        CODY::update_device_uptime(ui.model.dashboard, esp_timer_get_time());
        if (ui.branding_active) {
            error = advance_branding(ui);
            if (error == ESP_ERR_NOT_FINISHED) error = ESP_OK;
            if (error == ESP_OK && ui.branding_active) vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
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
        const uint32_t wait_ms = ui.model.screensaver.phase == CODY::ScreensaverPhase::Visible
            ? static_cast<uint32_t>(CODY::kScreensaverFrameUs / 1000)
            : ui.view.countdown.animating() ? 33 : kPollMs;
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
    s_ui->branding_active = esp_reset_reason() != ESP_RST_DEEPSLEEP;
    ESP_LOGI(kTag, "Boot disk animation: reset_reason=%u requested=%u frames=%u",
        static_cast<unsigned>(esp_reset_reason()), s_ui->branding_active, VIBEWARE_BOOT_FRAME_COUNT);
    if (s_ui->branding_active) {
        frame.layer = Layer::Branding;
        error = VIBEWARE_boot_validate();
        if (error == ESP_OK) error = CODY::BootBranding::prepare(
            CODY::SceneData::title_font, CODY::SceneData::body_font, s_ui->branding);
    } else {
        frame.layer = Layer::Message;
        error = CODY::prepare_device_message(s_ui->view, frame.presentation, "Device", "Starting services");
    }
    if (error == ESP_OK) error = render(frame, true);
    if (error == ESP_OK && s_ui->branding_active) {
        s_ui->branding_due_us = esp_timer_get_time() + VIBEWARE_boot_frames[0].duration_ms * 1000LL;
        ESP_LOGI(kTag, "Boot disk first frame retired; starting the complete animation cycle");
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

