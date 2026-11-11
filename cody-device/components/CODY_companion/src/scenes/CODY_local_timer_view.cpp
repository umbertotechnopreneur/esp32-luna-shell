#include "CODY_local_timer_view.hpp"
#include "CODY_title_style.hpp"
#include "CODY_typography.hpp"
#include "CODY_ui_components.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_local_timer_view";
constexpr DamageRect kContentBounds{73, 90, 393, 383};
constexpr CODY2D::Rect kHeading{73, 90, 320, 44};
constexpr CODY2D::Rect kStatus{73, 145, 320, 30};
constexpr CODY2D::Rect kReadout{73, 181, 320, 54};
constexpr CODY2D::Rect kAlarmStatus{73, 249, 320, 56};
constexpr CODY2D::Rect kDecrease{73, 247, 154, 60};
constexpr CODY2D::Rect kIncrease{239, 247, 154, 60};
constexpr CODY2D::Rect kSound{73, 321, 154, 60};
constexpr CODY2D::Rect kStart{239, 321, 154, 60};
constexpr CODY2D::Rect kSingleAction{113, 321, 240, 60};
constexpr uint16_t kWhite = UI::Palette::text;
constexpr uint16_t kMuted = UI::Palette::muted;
constexpr uint16_t kMint = UI::Palette::positive;
constexpr uint16_t kAmber = UI::Palette::warning;

// operation: Timer presentation operation that may fail.
// error: Original error preserved for the display owner.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error));
    }
    return error;
}

// font: Immutable native glyphs.
// text: Nonempty ASCII caption retained through frame retirement.
// bounds: Admitted logical text slot.
// color: Native RGB565 foreground.
// line: Receives the measured and centered text.
// Exception: Returns the original glyph error or rejects a caption that cannot fit.
esp_err_t prepare_line(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line)
{
    return UI::prepare_text(font, text, bounds, color, line, 28);
}

// action: Persistent control presentation with its semantic identity.
// id: Released action associated with this control's complete bounds.
// bounds: Admitted native control rectangle.
// label: Immutable action caption.
// font: Native font for action text.
// enabled: Whether the current timer settings admit this action.
// Exception: Returns the original caption preparation error.
esp_err_t prepare_action(LocalTimerView::Action &action, LocalTimerControl id, CODY2D::Rect bounds,
    const char *label, const CODY2D::MonoFont &font, bool enabled = true)
{
    action.id = id;
    auto &control = action.control;
    control.bounds = bounds;
    control.enabled = enabled;
    control.corner_radius = 28;
    const bool primary = id == LocalTimerControl::Start || id == LocalTimerControl::Dismiss;
    return UI::prepare_button(control, font, label, enabled ? kWhite : kMuted, 28,
        primary ? UI::kContentPrimary : UI::kContentSecondary);
}

// state: Immutable view borrowed until the complete frame retires.
// strip: Borrowed RGB565 tile in logical orientation.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the first original raster or surface failure.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto *view = static_cast<const LocalTimerView *>(state);
    if (view == nullptr || !view->valid) return result("draw state", ESP_ERR_INVALID_STATE);
    for (size_t index = 0; index < view->captions.size(); ++index) {
        const auto &line = view->captions[index];
        if (!line.valid) continue;
        const esp_err_t error = index == 0 ? ESP_OK :
            CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
        if (error != ESP_OK) return result("draw caption", error);
    }
    for (const auto &action : view->actions) {
        if (action.id == LocalTimerControl::None) continue;
        const esp_err_t error = CODY2D::draw_capsule_rows(action.control, strip, first_row, first_column);
        if (error != ESP_OK) return result("draw action", error);
    }
    return ESP_OK;
}

// view: Complete persistent timer presentation.
// content: Receives the borrowed central renderer and damage envelope.
esp_err_t attach_content(const LocalTimerView &view, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.state = &view;
    content.revision = view.revision;
    content.draw_rows = draw_rows;
    return prepare_styled_title(view.captions[0].text, kHeading, content.heading, 3, true);
}

// model: Confirmed idle duration and selected alarm sound.
// font: Immutable control caption glyphs.
// view: Presentation owner with cleared action storage.
// Exception: Preserves the first control preparation failure.
esp_err_t prepare_idle_actions(const LocalTimerModel &model, const CODY2D::MonoFont &font, LocalTimerView &view)
{
    esp_err_t error = prepare_action(view.actions[0], LocalTimerControl::Decrease, kDecrease,
        "- 1 min", font, model.duration_minutes > kLocalTimerMinMinutes);
    if (error == ESP_OK) {
        error = prepare_action(view.actions[1], LocalTimerControl::Increase, kIncrease,
            "+ 1 min", font, model.duration_minutes < kLocalTimerMaxMinutes);
    }
    if (error == ESP_OK) {
        const char *label = model.sound == LocalTimerSound::Bell ? "Sound: Bell" : "Sound: Chime";
        error = prepare_action(view.actions[2], LocalTimerControl::CycleSound, kSound, label, font);
    }
    if (error == ESP_OK) {
        error = prepare_action(view.actions[3], LocalTimerControl::Start, kStart, "Start", font);
    }
    return error;
}

// alarm: Sound disposition reported by the timer owner.
// font: Immutable status caption glyphs.
// line: Receives a caption only for muted, delayed or failed playback.
// Exception: Preserves the original caption preparation error.
esp_err_t prepare_alarm_status(LocalTimerAlarm alarm, const CODY2D::MonoFont &font, CODY2D::TextLine &line)
{
    switch (alarm) {
    case LocalTimerAlarm::Pending:
        return prepare_line(font, "Alarm pending", kAlarmStatus, kMuted, line);
    case LocalTimerAlarm::Silent:
        return prepare_line(font, "Alarm muted", kAlarmStatus, kMuted, line);
    case LocalTimerAlarm::Failed:
        return prepare_line(font, "Sound unavailable", kAlarmStatus, kAmber, line);
    case LocalTimerAlarm::None:
    case LocalTimerAlarm::Playing:
    case LocalTimerAlarm::Complete:
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

} // namespace

// model: Canonical timer configuration and phase.
// now_us: Current monotonic time shared with the timer owner.
// title_font: Native headline and digits.
// body_font: Native caption font for status and controls.
// view: Persistent storage updated after the previous frame retires.
// content: Receives the prepared callback without lateral navigation.
// Exception: Preserves native layout failures and rejects invalid timer values.
esp_err_t prepare_local_timer_view(const LocalTimerModel &model, int64_t now_us,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    LocalTimerView &view, SceneContent &content)
{
    if (now_us < 0 || model.duration_minutes < kLocalTimerMinMinutes ||
        model.duration_minutes > kLocalTimerMaxMinutes ||
        (model.sound != LocalTimerSound::Bell && model.sound != LocalTimerSound::Chime)) {
        return result("timer values", ESP_ERR_INVALID_ARG);
    }

    uint32_t seconds = 0;
    const char *status = nullptr;
    switch (model.phase) {
    case LocalTimerPhase::Idle:
        seconds = static_cast<uint32_t>(model.duration_minutes) * 60U;
        status = "Set duration";
        break;
    case LocalTimerPhase::Running:
        seconds = local_timer_seconds_remaining(model, now_us);
        status = "Time remaining";
        break;
    case LocalTimerPhase::Finished:
        status = "Time is up";
        break;
    default:
        return result("timer phase", ESP_ERR_INVALID_ARG);
    }
    if (seconds > static_cast<uint32_t>(model.duration_minutes) * 60U) {
        return result("remaining seconds", ESP_ERR_INVALID_STATE);
    }

    if (view.valid && view.phase == model.phase && view.sound == model.sound && view.alarm == model.alarm &&
        view.seconds == seconds && view.title_font == &title_font && view.body_font == &body_font) {
        return attach_content(view, content);
    }
    if (view.revision == std::numeric_limits<uint32_t>::max()) {
        return result("view revision", ESP_ERR_INVALID_STATE);
    }

    view.valid = false;
    view.captions = {};
    view.actions = {};
    const int written = std::snprintf(view.readout, sizeof(view.readout), "%02u:%02u",
        static_cast<unsigned>(seconds / 60U), static_cast<unsigned>(seconds % 60U));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(view.readout)) {
        return result("format time", ESP_ERR_INVALID_SIZE);
    }
    const bool finished = model.phase == LocalTimerPhase::Finished;
    view.captions[0].text = {"Timer", 5};
    esp_err_t error = ESP_OK;
    if (error == ESP_OK) {
        error = prepare_line(body_font, status, kStatus, finished ? kMint : kMuted, view.captions[1]);
    }
    if (error == ESP_OK) {
        error = prepare_line(Typography::monospace_large(), view.readout, kReadout, finished ? kMint : kWhite, view.captions[2]);
    }
    if (error != ESP_OK) return result("prepare timer captions", error);

    if (model.phase == LocalTimerPhase::Idle) {
        error = prepare_idle_actions(model, body_font, view);
    } else if (finished) {
        error = prepare_alarm_status(model.alarm, body_font, view.captions[3]);
        if (error == ESP_OK) {
            error = prepare_action(view.actions[0], LocalTimerControl::Dismiss, kSingleAction, "OK", Typography::system());
        }
    } else {
        error = prepare_action(view.actions[0], LocalTimerControl::Cancel, kSingleAction, "Cancel", Typography::system());
    }
    if (error != ESP_OK) return result("prepare timer controls", error);

    view.phase = model.phase;
    view.sound = model.sound;
    view.alarm = model.alarm;
    view.seconds = seconds;
    view.title_font = &title_font;
    view.body_font = &body_font;
    ++view.revision;
    view.valid = true;
    return attach_content(view, content);
}

// view: Last complete timer presentation.
// point: Logical touch position matching the visible control geometry.
LocalTimerControl local_timer_control_at(const LocalTimerView &view, CODY2D::Point point)
{
    if (!view.valid) return LocalTimerControl::None;
    for (const auto &action : view.actions) {
        if (action.id != LocalTimerControl::None && CODY2D::capsule_contains(action.control, point)) {
            return action.id;
        }
    }
    return LocalTimerControl::None;
}

} // namespace CODY
