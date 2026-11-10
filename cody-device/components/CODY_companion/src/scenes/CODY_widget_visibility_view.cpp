#include "CODY_widget_visibility_view.hpp"
#include "CODY_flat_menu_row.hpp"
#include "CODY_page_marks.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_widget_visibility";
#if defined(CODY_DEVICE_UI_ONLY)
constexpr DamageRect kContentBounds{73, 82, 393, 402};
constexpr CODY2D::Rect kHeadingBounds{73, 82, 320, 40};
constexpr CODY2D::Rect kStatusBounds{103, 376, 260, 25};
constexpr uint16_t kRowTop = 130;
constexpr uint16_t kRowHeight = 72;
constexpr uint16_t kRowStep = 80;
#else
constexpr DamageRect kContentBounds{73, 94, 393, 374};
constexpr CODY2D::Rect kHeadingBounds{73, 94, 320, 28};
constexpr CODY2D::Rect kPageBounds{73, 128, 320, 26};
constexpr CODY2D::Rect kNameBounds{73, 162, 320, 54};
constexpr CODY2D::Rect kToggleBounds{113, 226, 240, 52};
constexpr CODY2D::Rect kSubscreensBounds{113, 289, 240, 48};
constexpr CODY2D::Rect kStatusBounds{73, 344, 320, 30};
#endif
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kAmber = 0xFE27;

#if defined(CODY_DEVICE_UI_ONLY)
// screen: Stable screen identity rather than the row's current list position.
CODY2D::ActionIcon screen_icon(WidgetScreen screen)
{
    using Icon = CODY2D::ActionIcon;
    switch (screen) {
    case WidgetScreen::Reset: return Icon::Clock;
    case WidgetScreen::Context: return Icon::PlanAccount;
    case WidgetScreen::Limits: return Icon::Statistics;
    case WidgetScreen::Today: return Icon::Clock;
    case WidgetScreen::LastSevenDays: return Icon::Statistics;
    case WidgetScreen::TokenHistory: return Icon::Statistics;
    case WidgetScreen::Credits: return Icon::PlanPro;
    case WidgetScreen::Account: return Icon::PlanAccount;
    case WidgetScreen::About: return Icon::Gear;
    default: return Icon::None;
    }
}
#endif

// operation: Failed visibility editor operation.
// error: Original error retained for the caller.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error));
    }
    return error;
}

#if !defined(CODY_DEVICE_UI_ONLY)
// catalog: Ordered editor descriptors.
// id: Entry to locate by stable identity.
template <typename Id, std::size_t Size>
std::size_t catalog_index(const std::array<WidgetDescriptor<Id>, Size> &catalog, Id id)
{
    for (std::size_t index = 0; index < Size; ++index) {
        if (catalog[index].id == id) {
            return index;
        }
    }
    return Size;
}
#endif

// font: Immutable native glyphs.
// text: Nonempty ASCII caption retained through frame retirement.
// bounds: Logical centering area within the circular display.
// color: Native RGB565 foreground.
// line: Prepared borrowed text line.
// left_aligned: Align a row label to the left edge of its slot.
// Exception: Returns a font or layout error if the full caption cannot fit.
esp_err_t prepare_line(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line, bool left_aligned = false)
{
    const auto length = std::strlen(text);
    if (length == 0 || length > 32 || font.height > bounds.height) {
        return ESP_ERR_INVALID_SIZE;
    }
    const CODY2D::TextSpan span{text, static_cast<uint16_t>(length)};
    uint32_t width = 0;
    const esp_err_t error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) {
        return error;
    }
    if (width > bounds.width) {
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, span,
        {static_cast<int16_t>(bounds.x + (left_aligned ? 0U : (bounds.width - width) / 2U)),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, line);
}

// state: Immutable editor view kept alive by the display owner.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original tile, caption or control drawing error.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) {
        return result("draw state", ESP_ERR_INVALID_ARG);
    }
    const auto &view = *static_cast<const WidgetVisibilityView *>(state);
    if (!view.valid) {
        return result("draw state", ESP_ERR_INVALID_STATE);
    }
    esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) {
        return result("draw tile", error);
    }
    for (const auto &line : view.captions) {
        if (!line.valid) {
            continue;
        }
        error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
        if (error != ESP_OK) {
            return result("draw caption", error);
        }
    }
#if defined(CODY_DEVICE_UI_ONLY)
    if (error == ESP_OK) error = draw_page_marks(view.page, kVisibilityPageCount, strip, first_row, first_column);
    for (const auto &row : view.rows) {
        if (error != ESP_OK) break;
        if (row.screen == WidgetScreen::Device) continue;
        error = draw_flat_menu_row(row.control, 30, strip, first_row, first_column);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(row.status, row.status.text.length, strip, first_row, first_column);
        }
        if (error != ESP_OK) break;
    }
#else
    error = CODY2D::draw_capsule_rows(view.toggle, strip, first_row, first_column);
    if (error == ESP_OK && view.subscreens_visible) {
        error = CODY2D::draw_capsule_rows(view.subscreens, strip, first_row, first_column);
    }
#endif
    return result("draw control", error);
}

// view: Editor view retained until all native callbacks retire.
// content: Receives the central editor slot without the lateral controls.
void attach_content(const WidgetVisibilityView &view, SceneContent &content)
{
    content = {};
    content.bounds = kContentBounds;
    content.state = &view;
    content.revision = view.revision;
    content.draw_rows = draw_rows;
}

} // namespace

#if defined(CODY_DEVICE_UI_ONLY)
// page: Zero-based group of configurable screens.
// visibility: Last confirmed visibility policy.
// saving: True while the settings owner persists a change.
// save_failed: True when the latest save failed.
// load_failed: True when there is no confirmed policy.
// title_font: Native heading glyphs.
// body_font: Native row and status glyphs.
// view: Display-owned layout retired before the next mutation.
// content: Receives the editor's central render slot.
// Exception: Preserves the original policy, layout or caption failure.
esp_err_t prepare_widget_visibility_view(uint8_t page, const WidgetVisibility &visibility,
    bool saving, bool save_failed, bool load_failed, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, WidgetVisibilityView &view, SceneContent &content)
{
    if (page >= kVisibilityPageCount || !valid_widget_visibility(visibility)) {
        return result("page or settings policy", ESP_ERR_INVALID_ARG);
    }
    if (view.valid && view.page == page && view.visibility.screen_mask == visibility.screen_mask &&
        view.saving == saving && view.save_failed == save_failed && view.load_failed == load_failed &&
        view.title_font == &title_font && view.body_font == &body_font) {
        attach_content(view, content);
        return ESP_OK;
    }
    if (view.revision == std::numeric_limits<uint32_t>::max()) {
        return result("view revision", ESP_ERR_INVALID_STATE);
    }
    view.valid = false;
    view.rows = {};
    view.captions = {};
    const char *status = load_failed ? "Settings unavailable" :
        saving ? "Saving..." : save_failed ? "Could not save" : "";
    auto error = prepare_line(title_font, "Visible screens", kHeadingBounds, kWhite, view.captions[0]);
    if (error == ESP_OK && status[0] != '\0') {
        error = prepare_line(body_font, status, kStatusBounds,
            load_failed || save_failed ? kAmber : kMuted, view.captions[1]);
    }
    const size_t first = 1U + page * kVisibilityRowsPerPage;
    for (size_t row_index = 0; error == ESP_OK && row_index < view.rows.size() &&
         first + row_index < kWidgetScreens.size(); ++row_index) {
        const auto &descriptor = kWidgetScreens[first + row_index];
        const bool visible = !load_failed && widget_visible(visibility, descriptor.id);
        const auto top = static_cast<uint16_t>(kRowTop + row_index * kRowStep);
        auto &row = view.rows[row_index];
        row.screen = descriptor.id;
        row.control.bounds = {93, top, 280, kRowHeight};
        row.control.enabled = !saving && !load_failed;
        row.control.corner_radius = 12;
        row.control.top_color = visible ? 0x1147 : 0x18E5;
        row.control.bottom_color = visible ? 0x08C4 : 0x10A3;
        row.control.border_color = visible ? 0x2B4F : 0x4A8C;
        row.control.icon = screen_icon(descriptor.id);
        row.control.icon_center = {126, static_cast<int16_t>(top + 36U)};
        row.control.icon_color = visible ? 0x7759 : 0xBD9F;
        error = prepare_line(body_font, descriptor.label, {156, static_cast<uint16_t>(top + 5U), 207, 30},
            kWhite, row.control.caption, true);
        if (error == ESP_OK) {
            error = prepare_line(body_font, load_failed ? "N/A" : visible ? "Visible" : "Hidden",
                {156, static_cast<uint16_t>(top + 36U), 207, 28}, visible ? 0x5FF1 : kMuted, row.status, true);
        }
    }
    if (error != ESP_OK) return result("prepare screen list", error);

    view.page = page;
    view.visibility = visibility;
    view.saving = saving;
    view.save_failed = save_failed;
    view.load_failed = load_failed;
    view.title_font = &title_font;
    view.body_font = &body_font;
    ++view.revision;
    view.valid = true;
    attach_content(view, content);
    return ESP_OK;
}

// view: Editor state associated with the fully retired frame.
// point: Logical touch point in the current display orientation.
std::optional<size_t> widget_visibility_row_at(const WidgetVisibilityView &view, CODY2D::Point point)
{
    if (view.valid) {
        for (size_t index = 0; index < view.rows.size(); ++index) {
            if (CODY2D::capsule_contains(view.rows[index].control, point)) return index;
        }
    }
    return std::nullopt;
}
#else
// screen: Selected legacy root entry.
// panel: Selected legacy Cody child entry.
// children: True when editing legacy Cody child preferences.
// visibility: Confirmed settings policy.
// saving: True while a change is pending with the settings owner.
// save_failed: True when the pending change failed.
// load_failed: True when no confirmed settings policy can be displayed or edited.
// title_font: Immutable native glyphs for the entry name.
// body_font: Immutable native glyphs for secondary captions and controls.
// view: Persistent display-owned editor view.
// content: Receives the bounded central renderer.
// Exception: Preserves original validation, layout or native preparation errors.
esp_err_t prepare_widget_visibility_view(WidgetScreen screen, CompanionPanel panel, bool children,
    const WidgetVisibility &visibility, bool saving, bool save_failed, bool load_failed,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    WidgetVisibilityView &view, SceneContent &content)
{
    if (!valid_widget_visibility(visibility)) {
        return result("settings policy", ESP_ERR_INVALID_ARG);
    }
    const auto count = children ? kWidgetPanels.size() : kWidgetScreens.size();
    const auto index = children ? catalog_index(kWidgetPanels, panel) : catalog_index(kWidgetScreens, screen);
    if (index >= count) {
        return result("catalog selection", ESP_ERR_INVALID_ARG);
    }
    const auto page = index + 1U;
    const char *const heading = children ? "Cody subscreens" : "Widget visibility";
    const char *const label = children ? kWidgetPanels[index].label : kWidgetScreens[index].label;
    const bool same_selection = view.screen == screen && view.panel == panel && view.children == children &&
        view.visibility.panel_mask == visibility.panel_mask;
    if (view.valid && same_selection && view.visibility.screen_mask == visibility.screen_mask &&
        view.saving == saving && view.save_failed == save_failed && view.load_failed == load_failed &&
        view.title_font == &title_font && view.body_font == &body_font) {
        attach_content(view, content);
        return ESP_OK;
    }
    if (view.revision == std::numeric_limits<uint32_t>::max()) {
        return result("view revision", ESP_ERR_INVALID_STATE);
    }

    view.valid = false;
    view.captions = {};
    const int written = std::snprintf(view.page_index, sizeof(view.page_index), "%u / %u",
        static_cast<unsigned>(page), static_cast<unsigned>(count));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(view.page_index)) {
        return result("format page index", ESP_ERR_INVALID_SIZE);
    }
    esp_err_t error = prepare_line(body_font, heading, kHeadingBounds, kMuted, view.captions[0]);
    if (error == ESP_OK) {
        error = prepare_line(body_font, view.page_index, kPageBounds, kMuted, view.captions[1]);
    }
    if (error == ESP_OK) {
        error = prepare_line(title_font, label, kNameBounds, kCyan, view.captions[2]);
    }
    if (error != ESP_OK) {
        return result("prepare heading", error);
    }

    const bool enabled = children ? widget_panel_enabled(visibility, panel) : widget_visible(visibility, screen);
    WidgetVisibility candidate = visibility;
    const bool can_toggle = children || set_widget_visibility(candidate, screen, !enabled);
    view.toggle = {};
    view.toggle.bounds = kToggleBounds;
    view.toggle.enabled = !saving && !load_failed && can_toggle;
    const bool show_visible = !load_failed && enabled;
    view.toggle.top_color = show_visible ? 0x246E : 0x2947;
    view.toggle.bottom_color = show_visible ? 0x10C6 : 0x10A4;
    view.toggle.border_color = show_visible ? 0x5FF1 : 0x73AF;
    const char *const toggle_text = load_failed ? "Unavailable" : (enabled ? "Visible" : "Hidden");
    error = prepare_line(body_font, toggle_text, kToggleBounds, load_failed ? kMuted : kWhite, view.toggle.caption);
    if (error != ESP_OK) {
        return result("prepare visibility toggle", error);
    }

    view.subscreens_visible = !children && screen == WidgetScreen::Cody;
    if (view.subscreens_visible) {
        view.subscreens = {};
        view.subscreens.bounds = kSubscreensBounds;
        view.subscreens.enabled = !saving || load_failed;
        view.subscreens.top_color = 0x226B;
        view.subscreens.bottom_color = 0x10C6;
        view.subscreens.border_color = 0x4B9B;
        error = prepare_line(body_font, "Subscreens", kSubscreensBounds, kWhite, view.subscreens.caption);
        if (error != ESP_OK) {
            return result("prepare subscreens control", error);
        }
    }

    const char *status = nullptr;
    if (load_failed) {
        status = "Settings unavailable";
    } else if (saving) {
        status = "Saving...";
    } else if (save_failed) {
        status = "Could not save";
    } else if (!can_toggle) {
        status = "Keep one widget visible";
    }
    if (status != nullptr) {
        const uint16_t status_color = saving && !load_failed ? kMuted : kAmber;
        error = prepare_line(body_font, status, kStatusBounds, status_color, view.captions[3]);
        if (error != ESP_OK) {
            return result("prepare settings status", error);
        }
    }

    view.visibility = visibility;
    view.screen = screen;
    view.panel = panel;
    view.children = children;
    view.saving = saving;
    view.save_failed = save_failed;
    view.load_failed = load_failed;
    view.title_font = &title_font;
    view.body_font = &body_font;
    ++view.revision;
    view.valid = true;
    attach_content(view, content);
    return ESP_OK;
}

// view: Editor state associated with the fully retired frame.
// point: Logical touch point in the current display orientation.
WidgetVisibilityControl widget_visibility_control_at(const WidgetVisibilityView &view, CODY2D::Point point)
{
    if (!view.valid) {
        return WidgetVisibilityControl::None;
    }
    if (CODY2D::capsule_contains(view.toggle, point)) {
        return WidgetVisibilityControl::Toggle;
    }
    if (view.subscreens_visible && CODY2D::capsule_contains(view.subscreens, point)) {
        return WidgetVisibilityControl::OpenChildren;
    }
    return WidgetVisibilityControl::None;
}
#endif

} // namespace CODY
