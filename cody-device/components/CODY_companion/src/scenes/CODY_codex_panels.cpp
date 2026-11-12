#include "CODY_codex_panels.hpp"
#include "CODY_title_style.hpp"
#include "CODY_ui_components.hpp"
#include "CODY_typography.hpp"
#include "CODY_theme_generated.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"
#include "CODY_codex_event_presentation.hpp"
#include "CODY_home_dashboard.hpp"
#include "home_dashboard_fonts.hpp"
#include "scene_data.hpp"

namespace CODY {
namespace {

constexpr uint16_t kWhite = Theme::text_default;
constexpr uint16_t kMuted = Theme::text_muted;
constexpr uint16_t kAmber = Theme::status_warning_content;
constexpr uint16_t kMint = Theme::secondary_muted;
constexpr uint16_t kCyan = Theme::firmware_codex_panels_cyan;
constexpr uint16_t kRule = Theme::firmware_codex_panels_rule;
constexpr CODY2D::Rect kList{73, 155, 320, 234};
constexpr uint16_t kRowHeight = 78;
constexpr CODY2D::Rect kPreview{94, 204, 282, 25};
constexpr DamageRect kBounds{73, 73, 393, 391};

// source: Original canonical validity and owner error, already aged at acquisition.
bool current(const LUNASHELL::ComponentSnapshot &source)
{
    return source.data_valid && !source.data_stale && source.last_error == ESP_OK;
}

// view: Persistent glyph owner; borrowed text is static or retained by view.
// text: Bounded source caption.
// font: Immutable native font metrics.
// box: Logical text slot.
// color: Semantic RGB565 text color.
// center: Centers short text; long titles remain clipped to the safe column.
// offset: Horizontal position of the one scrolling preview.
// Exception: Returns the original native measurement or glyph error.
esp_err_t add_line(CodexPanelView &view, const char *text, const CODY2D::MonoFont &font,
    CODY2D::Rect box, uint16_t color, bool center = true, uint16_t offset = 0)
{
    if (text[0] == '\0') return ESP_OK;
    if (view.line_count >= view.lines.size() || font.height > box.height) return ESP_ERR_INVALID_SIZE;
    const CODY2D::TextSpan span{text, static_cast<uint16_t>(std::strlen(text))};
    if (view.line_count == 0) {
        view.lines[0].text = span;
        view.line_boxes[0] = box;
        ++view.line_count;
        return ESP_OK;
    }
    uint32_t width = 0;
    auto error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) return error;
    auto window = span;
    int x = box.x;
    if (width > box.width) {
        int16_t origin_offset = 0;
        error = CODY2D::select_text_window(font, span, offset, box.width, window, origin_offset);
        if (error != ESP_OK) return error;
        x += origin_offset;
    } else if (center) {
        x += (box.width - width) / 2U;
    }
    error = CODY2D::prepare_text_line(font, window,
        {static_cast<int16_t>(x), static_cast<int16_t>(box.y + (box.height - font.height) / 2U)},
        color, view.lines[view.line_count]);
    if (error == ESP_OK) {
        view.line_boxes[view.line_count] = box;
        ++view.line_count;
    }
    return error;
}

// strip: Borrowed native tile.
// row: First logical row represented by the tile.
// column: First logical column represented by the tile.
// box: Logical rectangle clipped before any write.
// color: Native RGB565 fill.
// Exception: Returns the original fill error.
esp_err_t fill(CODY2D::Surface strip, uint16_t row, uint16_t column, CODY2D::Rect box, uint16_t color)
{
    const int left = std::max<int>(column, box.x);
    const int right = std::min<int>(column + strip.width, box.x + box.width);
    const int top = std::max<int>(row, box.y);
    const int bottom = std::min<int>(row + strip.height, box.y + box.height);
    if (left >= right || top >= bottom) return ESP_OK;
    return CODY2D::fill_rect(strip, {static_cast<uint16_t>(left - column), static_cast<uint16_t>(top - row),
        static_cast<uint16_t>(right - left), static_cast<uint16_t>(bottom - top)}, color);
}

struct Segment { CODY2D::Vec2 start; CODY2D::Vec2 end; };
constexpr Segment kShell[]{
    {{105, 191}, {160, 191}}, {{160, 191}, {160, 232}}, {{160, 232}, {105, 232}},
    {{105, 232}, {105, 191}}, {{115, 202}, {125, 211}}, {{125, 211}, {115, 220}}, {{132, 220}, {150, 220}},
};
constexpr Segment kWeb[]{
    {{126, 185}, {142, 190}}, {{142, 190}, {152, 201}}, {{152, 201}, {153, 211}},
    {{126, 185}, {109, 190}}, {{109, 190}, {100, 203}}, {{100, 203}, {100, 219}},
    {{100, 219}, {110, 231}}, {{110, 231}, {127, 236}}, {{127, 236}, {135, 233}},
    {{104, 200}, {149, 200}}, {{100, 218}, {125, 218}},
    {{126, 185}, {117, 199}}, {{117, 199}, {115, 219}}, {{115, 219}, {126, 235}},
    {{126, 185}, {135, 198}}, {{135, 198}, {138, 209}},
    {{135, 215}, {146, 210}}, {{146, 210}, {156, 216}}, {{156, 216}, {158, 227}},
    {{158, 227}, {151, 235}}, {{151, 235}, {140, 236}}, {{140, 236}, {132, 228}},
    {{132, 228}, {135, 215}}, {{155, 233}, {165, 243}},
};
constexpr Segment kEditing[]{
    {{107, 230}, {115, 214}}, {{115, 214}, {147, 185}}, {{147, 185}, {160, 198}},
    {{160, 198}, {130, 228}}, {{130, 228}, {107, 230}}, {{140, 193}, {153, 205}},
};
constexpr Segment kCompacting[]{
    {{101, 211}, {119, 211}}, {{111, 202}, {120, 211}}, {{120, 211}, {111, 220}},
    {{129, 196}, {149, 196}}, {{129, 211}, {149, 211}}, {{129, 226}, {149, 226}},
    {{178, 211}, {159, 211}}, {{168, 202}, {159, 211}}, {{159, 211}, {168, 220}},
};

// glyph: Semantic observed activity, separate from its caption.
// strip: Borrowed native tile.
// row: First logical row represented by the tile.
// column: First logical column represented by the tile.
// Exception: Returns the original path raster error.
esp_err_t draw_glyph(TurnGlyph glyph, CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    if (glyph == TurnGlyph::None || row >= 247 || row + strip.height <= 180) return ESP_OK;
    if (glyph == TurnGlyph::Tool) {
        return CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Gear, {103, 185, 56, 56},
            kCyan, strip, row, column);
    }
    const Segment *segments = nullptr;
    size_t count = 0;
    switch (glyph) {
    case TurnGlyph::Shell: segments = kShell; count = std::size(kShell); break;
    case TurnGlyph::Web: segments = kWeb; count = std::size(kWeb); break;
    case TurnGlyph::Editing: segments = kEditing; count = std::size(kEditing); break;
    case TurnGlyph::Tool: return ESP_OK; // Rendered with the shared browser glyph above.
    case TurnGlyph::Compacting: segments = kCompacting; count = std::size(kCompacting); break;
    case TurnGlyph::None: return ESP_OK;
    }
    const auto color = glyph == TurnGlyph::Compacting ? kAmber : kCyan;
    for (size_t index = 0; index < count; ++index) {
        const auto error = CODY2D::stroke_rows(strip, row, segments[index].start, segments[index].end, 4, color, column);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}

// state: Immutable prepared panel borrowed through frame retirement.
// strip: Borrowed native tile.
// row: First logical row represented by the tile.
// column: First logical column represented by the tile.
// Exception: Returns the first original native raster error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    const auto &view = *static_cast<const CodexPanelView *>(state);
    auto error = fill(strip, row, column, {80, 153, 303, 1}, kRule);
    if (error == ESP_OK && view.kind == CodexPanelKind::Events && view.inbox_current) {
        for (uint8_t index = 0; error == ESP_OK && index < view.page.count; ++index) {
            const auto y = static_cast<uint16_t>(kList.y + index * kRowHeight);
            const bool question = view.page.rows[index].kind == LUNASHELL::CodexEventKind::Question;
            if (index == 0) error = fill(strip, row, column, {78, y, 302, 72}, Theme::codex_panels_selected_row);
            if (error == ESP_OK) error = fill(strip, row, column, {83, static_cast<uint16_t>(y + 9), 3, 55},
                question ? kAmber : kMint);
            if (error == ESP_OK) error = fill(strip, row, column, {80, static_cast<uint16_t>(y + 74), 303, 1}, kRule);
        }
        if (error == ESP_OK && view.page.total > LUNASHELL::kCodexInboxRows) {
            constexpr uint16_t track_height = 192;
            const auto thumb = static_cast<uint16_t>(std::max<unsigned>(8, track_height * view.page.count / view.page.total));
            const auto top = static_cast<uint16_t>(169 + (track_height - thumb) * view.page.offset /
                (view.page.total - view.page.count));
            error = fill(strip, row, column, {387, 169, 2, track_height}, kRule);
            if (error == ESP_OK) error = fill(strip, row, column, {387, top, 2, thumb}, kMuted);
        }
    } else if (error == ESP_OK) {
        error = draw_glyph(view.glyph, strip, row, column);
        if (error == ESP_OK && view.kind == CodexPanelKind::Reply) {
            error = UI::draw_text_action(view.reply_action, strip, row, column);
        } else if (error == ESP_OK && view.kind == CodexPanelKind::CurrentTurn) {
            error = draw_chat_title(view.chat_title, strip, row, column);
        }
    }
    for (uint8_t index = 0; error == ESP_OK && index < view.line_count; ++index) {
        const auto &line = view.lines[index];
        const auto &box = view.line_boxes[index];
        if (row >= box.y + box.height || row + strip.height <= box.y) continue;
        const int left = std::max<int>(column, box.x);
        const int right = std::min<int>(column + strip.width, box.x + box.width);
        if (left >= right) continue;
        auto clipped = strip;
        const size_t skip = left - column;
        clipped.pixels += skip;
        clipped.pixel_capacity -= skip;
        clipped.width = static_cast<uint16_t>(right - left);
        error = index == 0 ? ESP_OK :
            CODY2D::draw_text_rows(line, line.text.length, clipped, row, left);
    }
    return error;
}

// view: Prepared panel whose text storage remains stable until frame retirement.
// content: Receives the native damage and draw callback.
esp_err_t attach(const CodexPanelView &view, SceneContent &content)
{
    content = {};
    content.bounds = kBounds;
    content.revision = view.revision;
    content.state = &view;
    content.draw_rows = draw_rows;
    return prepare_styled_title(view.lines[0].text, view.line_boxes[0], content.heading, 3, true);
}

// view: Persistent panel; borrows only its own storage or static captions.
// content: Receives the callback only after successful preparation.
// error: Original preparation result.
esp_err_t finish(CodexPanelView &view, SceneContent &content, esp_err_t error)
{
    if (error != ESP_OK) return error;
    if (view.revision == std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
    ++view.revision;
    view.valid = true;
    return attach(view, content);
}

// left: Previous bounded authenticated page, excluding the heartbeat.
// right: New bounded page; order and exact identities are significant.
bool same_page(const LUNASHELL::CodexInboxPage &left, const LUNASHELL::CodexInboxPage &right)
{
    if (left.total != right.total || left.offset != right.offset || left.count != right.count) return false;
    for (uint8_t index = 0; index < left.count; ++index) {
        const auto &a = left.rows[index];
        const auto &b = right.rows[index];
        if (a.kind != b.kind || std::strcmp(a.request_id, b.request_id) != 0 ||
            std::strcmp(a.title, b.title) != 0 || std::strcmp(a.preview, b.preview) != 0) return false;
    }
    return true;
}

} // namespace

// view: Persistent panel and glyph owner.
// inbox: Canonical current Events page.
// now_us: Monotonic time used to scroll only the lead question preview.
// content: Receives the bounded Events render.
// Exception: Returns the original font or geometry error.
esp_err_t prepare_codex_events(CodexPanelView &view, const LUNASHELL::CodexInboxSnapshot &inbox,
    int64_t now_us, SceneContent &content)
{
    const bool available = current(inbox);
    const bool unchanged = view.valid && view.kind == CodexPanelKind::Events &&
        view.inbox_current == available && (!available || same_page(view.page, inbox));
    if (!unchanged) view.marquee_started_us = now_us;
    uint16_t offset = 0;
    if (available && inbox.count > 0 && inbox.rows[0].preview[0] != '\0') {
        const auto &font = HomeFonts::small_font;
        const auto &preview = inbox.rows[0].preview;
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(font, {preview, static_cast<uint16_t>(std::strlen(preview))}, width);
        if (error != ESP_OK) return error;
        if (width > kPreview.width) {
            constexpr int64_t step_us = 50000;
            const int64_t travel = width - kPreview.width;
            const auto cycle = 2800000 + travel * step_us;
            const auto elapsed = std::max<int64_t>(0, now_us - view.marquee_started_us) % cycle;
            offset = static_cast<uint16_t>(std::clamp<int64_t>((elapsed - 1200000) / step_us, 0, travel));
        }
    }
    if (unchanged && offset == view.marquee_offset) {
        return attach(view, content);
    }
    view.valid = false;
    view.kind = CodexPanelKind::Events;
    view.glyph = TurnGlyph::None;
    view.inbox_current = available;
    view.marquee_offset = offset;
    view.line_count = 0;
    if (available) view.page = inbox;
    else view.page = {};
    if (available) std::snprintf(view.subtitle, sizeof(view.subtitle), "%u %s", unsigned(inbox.total),
        inbox.total == 1 ? "event" : "events");
    else std::snprintf(view.subtitle, sizeof(view.subtitle), "%s",
        inbox.data_stale ? "Events unavailable" : "Waiting for PC events");
    auto error = add_line(view, "Events", SceneData::title_font, {73, 75, 320, 50}, kWhite);
    if (error == ESP_OK) error = add_line(view, view.subtitle, SceneData::body_font, {73, 125, 320, 25}, kMuted);
    for (uint8_t index = 0; error == ESP_OK && index < view.page.count; ++index) {
        const auto &entry = view.page.rows[index];
        const bool question = entry.kind == LUNASHELL::CodexEventKind::Question;
        const auto y = static_cast<uint16_t>(kList.y + index * kRowHeight);
        error = add_line(view, question ? "QUESTION" : "REPLY READY", HomeFonts::small_font,
            {94, static_cast<uint16_t>(y + 3), 282, 21}, question ? kAmber : kMint, false);
        const CODY2D::Rect title_bounds{94, static_cast<uint16_t>(y + 23), 282, 25};
        const CODY2D::MonoFont *title_face = &Typography::monospace();
        if (error == ESP_OK && entry.title[0] != '\0')
            error = Typography::label_font(entry.title, title_bounds, *title_face, title_face);
        if (error == ESP_OK) error = add_line(view, entry.title, *title_face, title_bounds, kWhite, false);
        if (error == ESP_OK && index == 0 && question) {
            error = add_line(view, entry.preview, HomeFonts::small_font, kPreview, kMuted, false, offset);
        }
    }
    return finish(view, content, error);
}

// observation: Authenticated activity for the currently followed chat.
// now_us: Monotonic time for the device's link lease.
bool current_turn_has_data(const LUNASHELL::CodexTechnicalObservation &observation, int64_t now_us)
{
    using Phase = LUNASHELL::CodexActivityState;
    const auto &activity = observation.link.activity;
    return LUNASHELL::codex_presence::ready_current(observation.link, now_us) && current(activity) &&
        activity.state != Phase::None && activity.state != Phase::Unavailable && activity.chat_id[0] != '\0';
}

// view: Persistent panel and text owner.
// observation: Canonical read-only activity and technical source.
// now_us: Monotonic time used only for freshness and elapsed duration.
// content: Receives the bounded turn render.
// Exception: Returns the original font or geometry error.
esp_err_t prepare_current_turn(CodexPanelView &view, const LUNASHELL::CodexTechnicalObservation &observation,
    int64_t now_us, SceneContent &content)
{
    const auto &activity = observation.link.activity;
    const auto &technical = observation.technical;
    const bool live = current_turn_has_data(observation, now_us);
    const char *title = live ? codex_chat_title_caption(activity, now_us) : "Chat unavailable";
    const char *detail = "";
    const char *status = "Activity unavailable";
    char elapsed[12]{};
    TurnGlyph glyph = TurnGlyph::None;
    using Phase = LUNASHELL::CodexActivityState;
    using Compact = LUNASHELL::CodexCompactionState;
    const bool compacting = live && current(technical) && technical.compaction_valid &&
        technical.compaction == Compact::Active && technical.revision == activity.revision &&
        std::strcmp(technical.chat_id, activity.chat_id) == 0;
    if (compacting) {
        status = "Compacting";
        glyph = TurnGlyph::Compacting;
        if (technical.compaction_age_valid && technical.received_at_us > 0 && now_us >= technical.received_at_us) {
            const auto seconds = (technical.compaction_age_at_receive_us + now_us - technical.received_at_us) / 1000000U;
            std::snprintf(elapsed, sizeof(elapsed), "%02u:%02u", unsigned(seconds / 60U), unsigned(seconds % 60U));
        }
    } else if (live) {
        switch (activity.state) {
        case Phase::Running: status = "Shell"; glyph = TurnGlyph::Shell; break;
        case Phase::Editing: status = "Editing"; glyph = TurnGlyph::Editing; break;
        case Phase::Working:
            if (std::strcmp(activity.description, "Tool: Web lookup") == 0) {
                status = "Web search";
                glyph = TurnGlyph::Web;
            } else {
                status = std::strncmp(activity.description, "Tool: ", 6) == 0 ? "Using a tool" : "Working";
                if (std::strncmp(activity.description, "Tool: ", 6) == 0) glyph = TurnGlyph::Tool;
            }
            break;
        case Phase::ResponseReady: status = "Reply ready"; break;
        case Phase::Paused: status = "Waiting for you"; break;
        case Phase::Interrupted: status = "Interrupted"; break;
        case Phase::Ended: status = "Turn ended"; break;
        case Phase::None:
        case Phase::Active: status = "No turn in progress"; break;
        case Phase::Unavailable: break;
        }
        if (activity.state == Phase::Working || activity.state == Phase::Running || activity.state == Phase::Editing) {
            detail = activity.description;
        }
    }
    const bool same_view = view.valid && view.kind == CodexPanelKind::CurrentTurn;
    bool title_changed = false;
    auto error = prepare_chat_title(view.chat_title, title, live ? activity.chat_id : "", SceneData::body_font,
        now_us, !same_view, title_changed);
    if (error != ESP_OK) return error;
    if (!same_view || std::strcmp(view.detail, detail) != 0) view.marquee_started_us = now_us;
    uint16_t detail_offset = 0;
    if (detail[0] != '\0') {
        uint32_t width = 0;
        const CODY2D::TextSpan span{detail, static_cast<uint16_t>(std::strlen(detail))};
        error = CODY2D::measure_text_width(SceneData::body_font, span, width);
        if (error != ESP_OK) return error;
        if (width > 320) {
            constexpr int64_t step_us = 50000;
            const int64_t travel = width - 320;
            const auto cycle = 2800000 + travel * step_us;
            const auto elapsed_time = std::max<int64_t>(0, now_us - view.marquee_started_us) % cycle;
            detail_offset = static_cast<uint16_t>(std::clamp<int64_t>((elapsed_time - 1200000) / step_us, 0, travel));
        }
    }
    if (same_view && !title_changed && view.glyph == glyph && std::strcmp(view.detail, detail) == 0 &&
        view.marquee_offset == detail_offset && std::strcmp(view.elapsed, elapsed) == 0 && view.line_count >= 2 &&
        std::strcmp(view.lines[1].text.characters, status) == 0) {
        return attach(view, content);
    }
    view.valid = false;
    view.kind = CodexPanelKind::CurrentTurn;
    view.glyph = glyph;
    view.marquee_offset = detail_offset;
    view.line_count = 0;
    std::snprintf(view.detail, sizeof(view.detail), "%s", detail);
    std::snprintf(view.elapsed, sizeof(view.elapsed), "%s", elapsed);
    error = add_line(view, "Current turn", SceneData::title_font, {73, 83, 320, 46}, kWhite);
    if (error == ESP_OK) error = add_line(view, status, SceneData::body_font,
        glyph == TurnGlyph::None ? CODY2D::Rect{73, 191, 320, 44}
            : CODY2D::Rect{185, 185, 195, static_cast<uint16_t>(compacting ? 25 : 50)},
        compacting ? kAmber : kCyan);
    if (error == ESP_OK && compacting) {
        error = add_line(view, "context", Typography::system(), {185, 210, 195, 25}, kAmber);
        if (error == ESP_OK) error = add_line(view, view.elapsed, SceneData::title_font,
            {73, 257, 320, 46}, kWhite);
        if (error == ESP_OK && view.elapsed[0] != '\0') {
            error = add_line(view, "Elapsed", Typography::system(), {73, 306, 320, 27}, kMuted);
        }
    } else if (error == ESP_OK) {
        error = add_line(view, view.detail, SceneData::body_font, {73, 264, 320, 28}, kWhite, true, detail_offset);
    }
    return finish(view, content, error);
}

// view: Persistent reply panel and text owner.
// title: Authenticated reply row title.
// pending: True until the exact dismissal receives confirmation.
// content: Receives the bounded reply render.
// Exception: Returns the original font or geometry error.
esp_err_t prepare_codex_reply(CodexPanelView &view, const char *title, bool pending, SceneContent &content)
{
    if (view.valid && view.kind == CodexPanelKind::Reply && std::strcmp(view.subtitle, title) == 0 &&
        view.reply_action.enabled == !pending) {
        return attach(view, content);
    }
    view.valid = false;
    view.kind = CodexPanelKind::Reply;
    view.glyph = TurnGlyph::None;
    view.line_count = 0;
    std::snprintf(view.subtitle, sizeof(view.subtitle), "%s", title);
    auto &action = view.reply_action;
    action = {};
    action.bounds = {113, 330, 240, 56};
    action.top_color = Theme::codex_panels_action_top;
    action.bottom_color = Theme::codex_panels_action_bottom;
    action.border_color = kRule;
    action.icon_color = kMint;
    action.enabled = !pending;
    auto error = add_line(view, "Chat waiting", SceneData::title_font, {73, 83, 320, 48}, kMint);
    if (error == ESP_OK) error = add_line(view, view.subtitle, SceneData::body_font, {73, 190, 320, 32}, kWhite);
    if (error == ESP_OK) error = event_presentation::prepare_read_confirmation(SceneData::body_font, action);
    return finish(view, content, error);
}

// point: Logical touch location on a retired Events page.
int codex_event_row_at(CODY2D::Point point)
{
    if (point.x < kList.x || point.x >= kList.x + kList.width ||
        point.y < kList.y || point.y >= kList.y + kList.height) return -1;
    return (point.y - kList.y) / kRowHeight;
}

} // namespace CODY
