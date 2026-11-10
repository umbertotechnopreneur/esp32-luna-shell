#include "CODY_device_view.hpp"
#include "CODY_home_dashboard.hpp"
#include "CODY_voice_waveform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "esp_log.h"
#include "home_dashboard_fonts.hpp"
#include "scene_data.hpp"
#include "sdkconfig.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_device_view";
constexpr DamageRect kMessageBounds{73, 84, 393, 382};
constexpr CODY2D::Rect kHeading{73, 88, 320, 58};
constexpr CODY2D::Rect kStatus{73, 155, 320, 40};
constexpr CODY2D::Rect kDetail{73, 202, 320, 28};
constexpr CODY2D::Rect kVoiceHeading{73, 84, 320, 42};
constexpr CODY2D::Rect kVoiceStatus{73, 127, 320, 25};
constexpr CODY2D::Rect kVoiceDetail{73, 152, 320, 25};
constexpr CODY2D::Rect kVoiceCountdownDigit{185, 176, 96, 74};
constexpr CODY2D::Rect kVoiceFailureIcon{201, 142, 64, 64};
constexpr CODY2D::Rect kVoiceFailureStatus{73, 220, 320, 40};
constexpr CODY2D::Rect kVoiceFailureDetail{73, 270, 320, 28};
static_assert(kVoiceDetail.y + kVoiceDetail.height <= kVoiceRibbonDamageTop,
    "Dictation captions must stay above the animation band");
constexpr CODY2D::Rect kSingleAction{103, 269, 260, 50};
constexpr CODY2D::Rect kFirstAction{103, 255, 260, 50};
constexpr CODY2D::Rect kSecondAction{103, 322, 260, 50};
// Keep the complete action clear of the shared battery banner at y=397.
constexpr CODY2D::Rect kDashboardSingleAction{113, 288, 240, 72};
constexpr CODY2D::Rect kDashboardFirstAction{73, 260, 154, 104};
constexpr CODY2D::Rect kDashboardSecondAction{239, 260, 154, 104};
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kAccent = 0x6F9E;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kOnline = 0x4EE8;
constexpr uint16_t kAttention = 0xFE27;
constexpr uint16_t kDivider = 0x2127;
constexpr DamageRect kOverviewBounds{73, 79, 393, 380};
constexpr CODY2D::Rect kOverviewHeading{73, 79, 320, 58};
constexpr CODY2D::Rect kOverviewChat{73, 291, 320, 28};
constexpr CODY2D::Rect kOverviewRecord{103, 330, 260, 50};

struct OverviewRowLayout {
    const char *label;
    uint16_t center_y;
};
constexpr std::array<OverviewRowLayout, 3> kOverviewRows{{
    {"Codex", 164}, {"Wi-Fi", 216}, {"Battery", 268},
}};

enum class TextAlignment : uint8_t { Center, Left, Right };

bool current(const LUNASHELL::ComponentSnapshot &observation)
{
    return observation.health == LUNASHELL::DeviceHealth::Ready && observation.last_error == ESP_OK &&
        observation.data_valid && !observation.data_stale;
}

// text: Bounded caption stored by the presentation owner.
// font: Preferred native font.
// bounds: Full admitted logical text slot.
// color: Native foreground color.
// line: Receives prepared text backed by the caller's buffer.
// alignment: Alignment within the admitted caption slot.
// Exception: Returns the original glyph error or a layout overflow.
esp_err_t prepare_line(const char *text, const CODY2D::MonoFont &font, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line, TextAlignment alignment = TextAlignment::Center)
{
    if (text[0] == '\0') {
        return ESP_OK;
    }
    const CODY2D::TextSpan span{text, static_cast<uint16_t>(std::strlen(text))};
    uint32_t width = 0;
    esp_err_t error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) {
        return error;
    }
    const auto *selected = &font;
    if (width > bounds.width && selected != &SceneData::body_font) {
        selected = &SceneData::body_font;
        error = CODY2D::measure_text_width(*selected, span, width);
        if (error != ESP_OK) {
            return error;
        }
    }
    if (width > bounds.width || selected->height > bounds.height) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t x = bounds.x;
    if (alignment == TextAlignment::Center) x += (bounds.width - width) / 2U;
    if (alignment == TextAlignment::Right) x += bounds.width - width;
    return CODY2D::prepare_text_line(*selected, span,
        {static_cast<int16_t>(x),
         static_cast<int16_t>(bounds.y + (bounds.height - selected->height) / 2U)}, color, line);
}

// text: Bounded action caption.
// bounds: Admitted capsule and hit bounds.
// primary: Selects the primary visual treatment.
// action: Receives the prepared control.
// Exception: Returns the original caption preparation error.
esp_err_t prepare_action(const char *text, CODY2D::Rect bounds, bool primary, CODY2D::CapsuleControl &action)
{
    action.enabled = text[0] != '\0';
    if (!action.enabled) {
        return ESP_OK;
    }
    action.bounds = bounds;
    action.top_color = primary ? 0x226B : 0x2947;
    action.bottom_color = primary ? 0x10C6 : 0x10A4;
    action.border_color = primary ? 0x4B9B : 0x73AF;
    action.corner_radius = bounds.height < 34 ? bounds.height / 2U : 17;
    return prepare_line(text, SceneData::body_font, bounds, kWhite, action.caption);
}

// state: Immutable message presentation borrowed until the frame retires.
// strip: Borrowed display tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the first original glyph or control raster error.
esp_err_t draw_message_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr || !static_cast<const PlainView *>(state)->valid) {
        return ESP_ERR_INVALID_STATE;
    }
    const auto &plain = *static_cast<const PlainView *>(state);
    if (plain.layout == DeviceMessageLayout::VoiceFailure) {
        const auto error = CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Microphone,
            kVoiceFailureIcon, kAttention, strip, first_row, first_column);
        if (error != ESP_OK) { return error; }
    }
    for (const auto &line : plain.captions) {
        if (line.valid) {
            const esp_err_t error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    for (const auto &action : plain.actions) {
        if (action.enabled) {
            const esp_err_t error = CODY2D::draw_capsule_rows(action, strip, first_row, first_column);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    return ESP_OK;
}

// plain: Prepared owner storage whose lifetime covers rendering.
// frame: Receives the borrowed message renderer and damage bounds.
void attach_message(const PlainView &plain, DeviceFrameContent &frame)
{
    frame.content.bounds = kMessageBounds;
    frame.content.revision = plain.revision;
    frame.content.state = &plain;
    frame.content.draw_rows = draw_message_rows;
}

// data: Canonical observation whose authenticated app presence is aged by its owner.
const char *connection_caption(const DeviceDashboardData &data)
{
    if (data.codex_presence_error != ESP_OK) return "Status unavailable";
    const auto &status = data.codex_presence;
    if (status.codex_app_open) return "Codex connected";
    if (current(status.codex_link) && status.codex_link_state == LUNASHELL::CodexLinkState::Connected) {
        return "PC linked";
    }
    const auto &wifi = data.hardware.wifi;
    if (data.hardware_error == ESP_OK && wifi.connection_state_valid && !wifi.data_stale &&
        (!wifi.connected || !wifi.ip_acquired)) {
        return "Wi-Fi offline";
    }
    if (status.codex_link_state == LUNASHELL::CodexLinkState::Discovering) return "Looking for Codex";
    return "Codex disconnected";
}

struct OverviewStatus {
    const char *text;
    CODY2D::ActionIcon icon;
    uint16_t color;
};

// data: Canonical authenticated app presence and separate PC-link observation.
OverviewStatus overview_codex_status(const DeviceDashboardData &data)
{
    using Icon = CODY2D::ActionIcon;
    using Link = LUNASHELL::CodexLinkState;
    const auto &status = data.codex_presence;
    if (data.codex_presence_error != ESP_OK || status.codex_link.data_stale ||
        (!status.codex_link.data_valid && status.codex_link.last_attempt_at_us <= 0)) {
        return {"Unavailable", Icon::Link, kMuted};
    }
    if (status.codex_app_open) return {"Connected", Icon::Link, kOnline};
    switch (status.codex_link_state) {
    case Link::Connected:
        return current(status.codex_link) ? OverviewStatus{"PC linked", Icon::Link, kAttention} :
            OverviewStatus{"Unavailable", Icon::Link, kMuted};
    case Link::Discovering: return {"Connecting", Icon::Link, kAttention};
    case Link::StartingPairing:
    case Link::Pairing:
    case Link::AwaitingConfirmation: return {"Pairing", Icon::Link, kAttention};
    default: return {"Not connected", Icon::Link, kAttention};
    }
}

// data: Canonical station association, IP acquisition and saved credential state.
OverviewStatus overview_wifi_status(const DeviceDashboardData &data)
{
    using Icon = CODY2D::ActionIcon;
    const auto &wifi = data.hardware.wifi;
    if (data.hardware_error != ESP_OK || !wifi.connection_state_valid || wifi.data_stale) {
        return {"Unavailable", Icon::Wifi, kMuted};
    }
    if (wifi.started && wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0') {
        return {"Connected", Icon::Wifi, kOnline};
    }
    if (!wifi.credentials_configured) return {"Not set up", Icon::WifiOff, kMuted};
    if (wifi.started && (wifi.connected || (wifi.connect_requested && wifi.last_error == ESP_OK))) {
        return {"Connecting", Icon::Wifi, kAttention};
    }
    return {"Offline", Icon::WifiOff, kAttention};
}

// strip: Borrowed display tile, with its absolute origin supplied separately.
// first_row: Tile's first logical y coordinate.
// first_column: Tile's first logical x coordinate.
// bounds: Rectangle in the complete display's logical coordinates.
// color: Native RGB565 fill color.
// Exception: Preserves the native fill error if the clipped rectangle intersects the tile.
esp_err_t fill_overview_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
    CODY2D::Rect bounds, uint16_t color)
{
    const int left = std::max<int>(bounds.x, first_column);
    const int right = std::min<int>(bounds.x + bounds.width, first_column + strip.width);
    const int top = std::max<int>(bounds.y, first_row);
    const int bottom = std::min<int>(bounds.y + bounds.height, first_row + strip.height);
    if (left >= right || top >= bottom) return ESP_OK;
    return CODY2D::fill_rect(strip, {static_cast<uint16_t>(left - first_column),
        static_cast<uint16_t>(top - first_row), static_cast<uint16_t>(right - left),
        static_cast<uint16_t>(bottom - top)}, color);
}

// state: Persistent overview preparation, borrowed until the completed frame retires.
// strip: Borrowed display tile retaining its native storage order.
// first_row: Tile's first logical y coordinate.
// first_column: Tile's first logical x coordinate.
// Exception: Returns the first original caption, outline or action draw error.
esp_err_t draw_overview_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto *overview = static_cast<const DeviceOverviewView *>(state);
    if (overview == nullptr || !overview->valid) return ESP_ERR_INVALID_STATE;
    const auto draw_line = [&](const CODY2D::TextLine &line) {
        return line.valid ? CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column) : ESP_OK;
    };
    const auto fill = [&](CODY2D::Rect bounds, uint16_t color) {
        return fill_overview_rows(strip, first_row, first_column, bounds, color);
    };
    auto error = draw_line(overview->heading);
    if (error == ESP_OK) error = fill({95, 190, 277, 1}, kDivider);
    if (error == ESP_OK) error = fill({95, 242, 277, 1}, kDivider);
    for (size_t index = 0; error == ESP_OK && index < overview->rows.size(); ++index) {
        const auto &row = overview->rows[index];
        if (row.icon != CODY2D::ActionIcon::None) {
            error = CODY2D::draw_action_icon_rows(row.icon,
                {98, static_cast<uint16_t>(kOverviewRows[index].center_y - 14U), 28, 28},
                row.color, strip, first_row, first_column);
        }
        if (error == ESP_OK) error = draw_line(row.label);
        if (error == ESP_OK) error = draw_line(row.status);
    }
    // Thin outline and terminal remain meaningful even without an admitted level.
    constexpr CODY2D::Rect battery_outline[]{{99, 261, 24, 2}, {99, 273, 24, 2},
        {99, 263, 2, 10}, {121, 263, 2, 10}, {123, 265, 4, 6}};
    const uint16_t battery_color = overview->rows.back().color;
    for (const auto &stroke : battery_outline) {
        if (error != ESP_OK) break;
        error = fill(stroke, battery_color);
    }
    if (error == ESP_OK && overview->battery_level > 0) {
        const auto width = static_cast<uint16_t>(16U * overview->battery_level / 100U);
        if (width != 0) error = fill({103, 265, width, 6}, battery_color);
    }
    if (error == ESP_OK) error = draw_line(overview->chat_line);
    if (error == ESP_OK && overview->record.enabled) {
        error = CODY2D::draw_capsule_rows(overview->record, strip, first_row, first_column);
    }
    return error;
}

// overview: Fully prepared display-owned glyphs, held until frame retirement.
// frame: Receives the flat dashboard renderer and its stable damage envelope.
void attach_overview(const DeviceOverviewView &overview, DeviceFrameContent &frame)
{
    frame.content.bounds = kOverviewBounds;
    frame.content.revision = overview.revision;
    frame.content.state = &overview;
    frame.content.draw_rows = draw_overview_rows;
}

// view: Display-owned native text and button layouts.
// frame: Receives the prepared status page.
// data: Current canonical connection, power and voice admission.
// now_us: Monotonic time used to expire received chat metadata.
// Exception: Returns the first original formatting or native geometry failure.
esp_err_t prepare_device_home(DeviceView &view, DeviceFrameContent &frame, const DeviceDashboardData &data,
    int64_t now_us)
{
    view.plain.valid = false;
    auto &overview = view.overview;
    const auto &battery = data.hardware.battery;
    const bool battery_current = data.hardware_error == ESP_OK && current(battery);
    const bool level_valid = battery_current && battery.level_percent_valid &&
        battery.level_percent >= 0 && battery.level_percent <= 100;
    const int16_t level = level_valid ? static_cast<int16_t>(battery.level_percent) : -1;
    char battery_value[16] = "Unavailable";
    if (level_valid) std::snprintf(battery_value, sizeof(battery_value), "%d%%", static_cast<int>(level));
    else if (battery_current && !battery.battery_present) std::snprintf(battery_value, sizeof(battery_value), "Not detected");

    const std::array<OverviewStatus, 3> statuses{{overview_codex_status(data), overview_wifi_status(data),
        {battery_value, CODY2D::ActionIcon::None, !level_valid ? kMuted : level <= 20 ? kAttention : kAccent}}};
    decltype(overview.chat) chat{};
    const auto &activity = data.context.link.activity;
    if (data.codex_presence_error == ESP_OK && data.codex_presence.codex_app_open && current(activity)) {
        const char *title = codex_chat_title_caption(activity, now_us);
        // The existing freshness helper returns the actual buffer only for a current title.
        if (title == activity.title) {
            if (std::strlen(title) < chat.size()) std::snprintf(chat.data(), chat.size(), "%s", title);
            else std::snprintf(chat.data(), chat.size(), "%.26s...", title);
        }
    }

    bool same = overview.valid && overview.battery_level == level && overview.chat == chat &&
        overview.record.enabled == data.voice_ready;
    for (size_t index = 0; same && index < overview.rows.size(); ++index) {
        const auto &row = overview.rows[index];
        same = row.icon == statuses[index].icon && row.color == statuses[index].color &&
            std::strcmp(row.value.data(), statuses[index].text) == 0;
    }
    if (same) {
        attach_overview(overview, frame);
        return ESP_OK;
    }
    if (overview.revision == std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
    overview.valid = false;
    overview.battery_level = level;
    overview.chat = chat;
    overview.chat_line = {};
    overview.record = {};
    auto error = prepare_line("Device", SceneData::title_font, kOverviewHeading, kWhite, overview.heading);
    for (size_t index = 0; error == ESP_OK && index < overview.rows.size(); ++index) {
        auto &row = overview.rows[index];
        const auto &status = statuses[index];
        const auto &layout = kOverviewRows[index];
        const int written = std::snprintf(row.value.data(), row.value.size(), "%s", status.text);
        if (written < 0 || static_cast<size_t>(written) >= row.value.size()) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
        row.icon = status.icon;
        row.color = status.color;
        const auto top = static_cast<uint16_t>(layout.center_y - 20U);
        error = prepare_line(layout.label, SceneData::body_font, {140, top, 80, 40},
            kWhite, row.label, TextAlignment::Left);
        if (error == ESP_OK) {
            error = prepare_line(row.value.data(), SceneData::body_font, {221, top, 151, 40},
                row.color, row.status, TextAlignment::Right);
        }
    }
    if (error == ESP_OK) error = prepare_line(overview.chat.data(), SceneData::body_font,
        kOverviewChat, kMuted, overview.chat_line);
    if (error == ESP_OK) error = prepare_action(data.voice_ready ? "Record voice" : "",
        kOverviewRecord, true, overview.record);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Device overview layout failed (%s)", esp_err_to_name(error));
        return error;
    }
    ++overview.revision;
    overview.valid = true;
    attach_overview(overview, frame);
    return ESP_OK;
}

} // namespace

// chrome: Display-owned prepared text and badge.
// data: Canonical connection state.
// show_connection: Show connection problems; a healthy connected badge is omitted.
// battery: Exact notice admitted by the model, or an empty notice.
// saved_epoch: The last observed UTC timestamp of this disconnected page's saved data.
// Exception: Returns original glyph preparation error.
esp_err_t prepare_device_chrome(DeviceChrome &chrome, const DeviceDashboardData &data,
    bool show_connection, const BatteryAlertNotice &battery, int64_t saved_epoch)
{
    const bool show_problem = show_connection &&
        (data.codex_presence_error != ESP_OK || !data.codex_presence.codex_app_open);
    const char *connection = show_problem && saved_epoch == 0 ? connection_caption(data) : nullptr;
    if (connection != chrome.connection_text || saved_epoch != chrome.saved_epoch) {
        chrome.connection = {};
        const char *caption = connection;
        if (saved_epoch > 0) {
            const auto time = static_cast<std::time_t>(saved_epoch);
            std::tm calendar{};
            if (static_cast<int64_t>(time) != saved_epoch || gmtime_r(&time, &calendar) == nullptr ||
                std::strftime(chrome.saved_text.data(), chrome.saved_text.size(),
                    "Saved %Y-%m-%d %H:%MZ", &calendar) == 0) return ESP_ERR_INVALID_RESPONSE;
            caption = chrome.saved_text.data();
        }
        if (caption != nullptr) {
            const auto error = prepare_status_caption(caption,
                saved_epoch > 0 ? 0xFE27 : kMuted, chrome.connection);
            if (error != ESP_OK) return error;
        }
        chrome.connection_text = connection;
        chrome.saved_epoch = saved_epoch;
        ++chrome.connection_revision;
    }
    if (chrome.battery_sequence == battery.sequence) return ESP_OK;
    chrome.battery = {};
    if (battery.sequence != 0) {
        const bool empty = battery.kind == BatteryAlertKind::Empty;
        const bool charge = battery.kind != BatteryAlertKind::Level50;
        if (empty) {
            std::snprintf(chrome.battery_text.data(), chrome.battery_text.size(), "Empty - connect USB");
        } else {
            std::snprintf(chrome.battery_text.data(), chrome.battery_text.size(), charge ?
                "Battery %u%% - charge" : "Battery %u%%", static_cast<unsigned>(battery.level_percent));
        }
        const auto error = prepare_action(chrome.battery_text.data(), {105, 397, 256, 30}, false, chrome.battery);
        if (error != ESP_OK) return error;
        chrome.battery.border_color = charge ? 0xFD25 : kAccent;
        chrome.battery.top_color = charge ? 0x3204 : 0x0926;
        chrome.battery.bottom_color = 0x0841;
    }
    chrome.battery_sequence = battery.sequence;
    return ESP_OK;
}

// chrome: Prepared text and badge whose backing strings remain live.
// strip: Borrowed display tile.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the first original text or capsule raster error.
esp_err_t draw_device_chrome(const DeviceChrome &chrome, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    esp_err_t error = ESP_OK;
    if (chrome.connection.valid) {
        error = draw_status_caption_rows(chrome.connection, strip, first_row, first_column);
    }
    if (error == ESP_OK && chrome.battery_sequence != 0) {
        error = CODY2D::draw_capsule_rows(chrome.battery, strip, first_row, first_column);
    }
    return error;
}

// view: Display-owned caption and action storage.
// frame: Receives borrowed layout.
// heading: Main heading.
// status: Primary caption.
// detail: Secondary caption.
// first: Optional primary action.
// second: Optional secondary action.
// extra: Optional third status line.
// layout: Selects the compact, dashboard or voice presentation slots.
// Exception: Returns the original bounded text or preparation error.
esp_err_t prepare_device_message(DeviceView &view, DeviceFrameContent &frame,
    const char *heading, const char *status, const char *detail, const char *first, const char *second,
    const char *extra, DeviceMessageLayout layout)
{
    frame = {};
    view.overview.valid = false;
    auto &plain = view.plain;
    decltype(plain.text) text{};
    const char *const requested[]{heading, status, detail, first, second, extra};
    for (size_t index = 0; index < text.size(); ++index) {
        if (requested[index] == nullptr) {
            continue;
        }
        const int written = std::snprintf(text[index].data(), text[index].size(), "%s", requested[index]);
        if (written < 0 || static_cast<size_t>(written) >= text[index].size()) {
            ESP_LOGE(kTag, "Message caption %u exceeds its storage", static_cast<unsigned>(index));
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (plain.valid && plain.text == text && plain.layout == layout) {
        attach_message(plain, frame);
        return ESP_OK;
    }
    if (plain.revision == std::numeric_limits<uint32_t>::max()) {
        return ESP_ERR_INVALID_STATE;
    }
    plain.valid = false;
    plain.text = text;
    plain.layout = layout;
    plain.captions = {};
    plain.actions = {};

    const bool voice_layout = layout == DeviceMessageLayout::Voice || layout == DeviceMessageLayout::VoiceCountdown;
    const bool voice_failure = layout == DeviceMessageLayout::VoiceFailure;
    esp_err_t error = prepare_line(plain.text[0].data(), SceneData::title_font,
        voice_layout || voice_failure ? kVoiceHeading : kHeading, kWhite, plain.captions[0]);
    if (error == ESP_OK) {
        error = prepare_line(plain.text[1].data(), SceneData::body_font,
            voice_failure ? kVoiceFailureStatus : voice_layout ? kVoiceStatus : kStatus,
            voice_failure ? kAttention : kAccent, plain.captions[1]);
    }
    if (error == ESP_OK) {
        error = prepare_line(plain.text[2].data(), SceneData::body_font,
            voice_failure ? kVoiceFailureDetail : voice_layout ? kVoiceDetail : kDetail,
            kMuted, plain.captions[2]);
    }
    if (error == ESP_OK && layout == DeviceMessageLayout::VoiceCountdown) {
        error = prepare_line(plain.text[5].data(), HomeFonts::value_font, kVoiceCountdownDigit, kWhite, plain.captions[3]);
    } else if (error == ESP_OK) {
        const uint16_t top = voice_failure ? 312 : layout == DeviceMessageLayout::Dashboard ? 230 : 234;
        error = prepare_line(plain.text[5].data(), SceneData::body_font, {73, top, 320, 27},
            voice_failure ? kMuted : kWhite, plain.captions[3]);
    }
    const bool two_actions = plain.text[3][0] != '\0' && plain.text[4][0] != '\0';
    const bool dashboard_actions = layout == DeviceMessageLayout::Dashboard;
    const auto single_bounds = dashboard_actions ? kDashboardSingleAction : kSingleAction;
    const auto first_bounds = dashboard_actions ? kDashboardFirstAction : kFirstAction;
    const auto second_bounds = dashboard_actions ? kDashboardSecondAction : kSecondAction;
    if (error == ESP_OK) {
        error = prepare_action(plain.text[3].data(), two_actions ? first_bounds : single_bounds, true, plain.actions[0]);
    }
    if (error == ESP_OK) {
        error = prepare_action(plain.text[4].data(), two_actions ? second_bounds : single_bounds, false, plain.actions[1]);
    }
    if (voice_layout && plain.actions[1].enabled) {
        auto &cancel = plain.actions[1];
        cancel.top_color = 0x70C8;
        cancel.bottom_color = 0x38A5;
        cancel.border_color = 0xBA0B;
    }
    if (dashboard_actions && !two_actions) {
        for (auto &action : plain.actions) {
            if (action.enabled) action.corner_radius = action.bounds.height / 2U;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Message layout failed (%s)", esp_err_to_name(error));
        return error;
    }
    ++plain.revision;
    plain.valid = true;
    attach_message(plain, frame);
    return ESP_OK;
}

// view: Stable glyph and action buffers whose lifetime covers native frame retirement.
// frame: Receives the live-only warning with bounded native controls.
// model: Current warned expiry; no saved snapshot is consulted.
// Exception: Preserves UTC conversion, bounded formatting and native preparation errors.
esp_err_t prepare_device_voucher_warning(DeviceView &view, DeviceFrameContent &frame, const DeviceModel &model)
{
    const auto expiry = model.vouchers.warning_expiry;
    if (expiry == 0 || pending_device_voucher_warning(model) != expiry) return ESP_ERR_INVALID_STATE;
    char date[32]{};
    char time[32]{};
    const auto instant = static_cast<time_t>(expiry);
    std::tm calendar{};
    if (static_cast<uint64_t>(instant) != expiry || gmtime_r(&instant, &calendar) == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (std::strftime(date, sizeof(date), "%Y-%m-%d", &calendar) == 0 ||
        std::strftime(time, sizeof(time), "%H:%M:%S UTC", &calendar) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return prepare_device_message(view, frame, "Reset voucher expiring", date, time,
        "Dismiss", nullptr, "Expires within 24 hours",
        DeviceMessageLayout::Dashboard);
}

// view: Presentation matching the last retired frame.
// point: Logical input position.
int device_action_at(const DeviceView &view, CODY2D::Point point)
{
    if (view.overview.valid) return CODY2D::capsule_contains(view.overview.record, point) ? 1 : 0;
    if (!view.plain.valid) {
        return 0;
    }
    for (size_t index = 0; index < view.plain.actions.size(); ++index) {
        if (CODY2D::capsule_contains(view.plain.actions[index], point)) {
            return static_cast<int>(index + 1U);
        }
    }
    return 0;
}

// view: Retained presentation state.
// frame: Receives prepared content.
// screen: Selected dashboard route.
// interval: Selected token history time window.
// data: Current read-only device model projection.
// now_us: Monotonic time for countdown and title freshness.
// vouchers: Live expiry viewport for Credits, empty on other routes.
// Exception: Returns the original preparation or unsupported screen error.
esp_err_t prepare_device_dashboard(DeviceView &view, DeviceFrameContent &frame,
    WidgetScreen screen, TokenHistoryInterval interval, const DeviceDashboardData &data, int64_t now_us,
    const VoucherList &vouchers)
{
    frame = {};
    if (screen == WidgetScreen::Device) {
        return prepare_device_home(view, frame, data, now_us);
    }
    view.overview.valid = false;
    view.plain.valid = false;
    const auto &title_font = SceneData::title_font;
    const auto &body_font = SceneData::body_font;
    if (screen == WidgetScreen::TokenHistory) {
        return prepare_token_history(interval, data.intervals, title_font, body_font, view.history, frame.content, &data.history_local);
    }
    if (screen == WidgetScreen::Reset) {
        const bool saved = data.saved.weekly_observed_utc > 0;
        return view.countdown.prepare(saved ? data.saved.weekly : data.weekly, data.clock, now_us,
            title_font, body_font, frame.content, &data.reset_local, saved);
    }

    if (screen == WidgetScreen::Context) {
        return prepare_context_dashboard(data.context, title_font, body_font, view.account, frame.content);
    }

    using Page = AccountDashboardPage;
    Page page;
    switch (screen) {
    case WidgetScreen::Limits: page = Page::QuotaWindows; break;
    case WidgetScreen::Today: page = Page::Today; break;
    case WidgetScreen::LastSevenDays: page = Page::LastSevenDays; break;
    case WidgetScreen::Credits: page = Page::Credits; break;
    case WidgetScreen::Account: page = Page::Account; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    return prepare_account_dashboard(page, data.details, data.weekly, data.clock, title_font, body_font,
        view.account, frame.content, device_saved_page_epoch(data, screen) > 0 ? &data.saved : nullptr, vouchers);
}

// view: Persistent information presentation, changed after the previous frame retires.
// frame: Receives native content only; navigation belongs to the retained model.
// model: Canonical diagnostics, selected child and build identity supplied by the device owner.
// Exception: Preserves original formatting and native geometry failures.
esp_err_t prepare_device_info(DeviceView &view, DeviceFrameContent &frame, const DeviceModel &model)
{
    frame = {};
    view.overview.valid = false;
    view.plain.valid = false;
    SystemInfoInput input{};
    input.page = model.info_page;
    input.diagnostics = &model.diagnostics;
    input.diagnostics_error = model.diagnostics_error;
    input.build = model.dashboard.build;
    input.image_version = model.dashboard.build_version;
    input.uptime_seconds = model.dashboard.uptime_seconds;
    input.previous_uptime_seconds = model.dashboard.previous_uptime_seconds;
    input.previous_uptime_valid = model.dashboard.previous_uptime_valid;
    return prepare_system_info(input, SceneData::title_font, SceneData::body_font, view.info, frame.content);
}

// view: Retained presentation state.
// frame: Receives prepared content.
// voice: Canonical recording state.
// Exception: Returns the original caption or geometry error.
esp_err_t prepare_device_voice(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::VoiceInputObservation &voice)
{
    using Phase = LUNASHELL::VoiceInputPhase;
    // status: Current voice phase caption.
    // detail: Optional instruction or original error.
    // first: Optional Send action.
    // second: Optional Cancel action.
    // Exception: Preserves the native layout error.
    const auto prepare = [&](const char *status, const char *detail = "", const char *first = nullptr,
                             const char *second = nullptr) {
        const bool has_destination = voice.destination_title[0] != '\0';
        return prepare_device_message(view, frame, has_destination ? "Dictation to" : "Dictation", status,
            has_destination ? voice.destination_title : detail, first, second, has_destination ? detail : "",
            DeviceMessageLayout::Voice);
    };
    char detail[32]{};
    switch (voice.phase) {
    case Phase::Idle:
        return prepare("Recording unavailable");
    case Phase::Preparing:
        return prepare("Preparing", voice.destination_title[0] == '\0' ? "Finding destination" : "", nullptr, "Cancel");
    case Phase::StartCountdown:
    case Phase::SendCountdown: {
        if (voice.countdown_seconds < 1 || voice.countdown_seconds > 3) return ESP_ERR_INVALID_RESPONSE;
        const char digit[]{static_cast<char>('0' + voice.countdown_seconds), '\0'};
        return prepare_device_message(view, frame, "Voice recording",
            voice.phase == Phase::StartCountdown ? "Recording starts in" : "Sending in",
            voice.destination_title, nullptr, "Cancel", digit, DeviceMessageLayout::VoiceCountdown);
    }
    case Phase::AwaitingPc:
        return prepare("Waiting for PC", "", nullptr, "Cancel");
    case Phase::Listening:
        // The capture owner checks for PCM on release. Its public observation
        // does not publish the private buffer size for every incoming block.
        return prepare("Listening", "", "Send", "Cancel");
    case Phase::Sending:
        return prepare("Sending audio");
    case Phase::Cancelling:
        return prepare("Cancelling");
    case Phase::Received:
        return prepare("On your PC", "Waiting for Codex");
    case Phase::Delivered:
        return prepare("Sent to Codex");
    case Phase::Cancelled:
        return prepare("Discarded");
    case Phase::Unconfirmed:
        return prepare("Delivery unconfirmed", "Check on your PC");
    case Phase::Failed: {
        std::snprintf(detail, sizeof(detail), "Error 0x%lx", static_cast<unsigned long>(voice.error));
        // The owner already returns after three seconds. A terminal failure has
        // no retry/dismiss capability; do not expose a button that cancels again.
        const bool rejected = voice.failure == LUNASHELL::VoiceInputFailure::PcRejected;
        const bool destination = voice.failure == LUNASHELL::VoiceInputFailure::Destination;
        const char *destination_status = voice.error == ESP_ERR_NOT_SUPPORTED ? "Chat title unsupported" : "Chat unavailable";
        const char *status = destination ? destination_status : rejected ? "PC rejected audio" :
            voice.failure == LUNASHELL::VoiceInputFailure::Microphone ? "Microphone failed" :
            voice.failure == LUNASHELL::VoiceInputFailure::Transport ? "Audio link failed" : "Audio failed";
        return prepare_device_message(view, frame, "Dictation", status,
            destination ? "Check Codex chat on PC" : rejected ? "Check Codex audio on PC" : "Returning to your screen",
            nullptr, nullptr, detail, DeviceMessageLayout::VoiceFailure);
    }
    }
    return ESP_ERR_INVALID_ARG;
}

// view: Retained presentation state.
// frame: Receives prepared content.
// observed: Canonical diagnostics projection.
// available: True when the projection was read successfully.
// Exception: Returns the original caption or geometry error.
esp_err_t prepare_device_diagnostics(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::DiagnosticsObservation &observed, bool available)
{
    if (!available) {
        return prepare_device_message(view, frame, "Diagnostics", "System data unavailable", "", "OK");
    }

    char cpu[32] = "CPU unavailable";
    const auto &processor = observed.processor;
    if (current(processor) && processor.cores[0].utilization_available) {
        const unsigned first = processor.cores[0].utilization_percent;
        if (processor.core_count > 1 && processor.cores[1].utilization_available) {
            std::snprintf(cpu, sizeof(cpu), "CPU1 %u%%  /  CPU2 %u%%", first,
                static_cast<unsigned>(processor.cores[1].utilization_percent));
        } else {
            std::snprintf(cpu, sizeof(cpu), "CPU1 %u%%", first);
        }
    }
    char memory[32] = "Memory unavailable";
    if (current(observed.memory)) {
        std::snprintf(memory, sizeof(memory), "Free heap %lu KiB",
            static_cast<unsigned long>(observed.memory.heap_free_bytes / 1024U));
    }
    return prepare_device_message(view, frame, "Diagnostics", cpu, memory, "OK");
}

} // namespace CODY
