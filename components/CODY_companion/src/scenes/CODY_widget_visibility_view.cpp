#include "CODY_widget_visibility_view.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_widget_visibility";
constexpr DamageRect kContentBounds{73, 94, 393, 374};
constexpr CODY2D::Rect kHeadingBounds{73, 94, 320, 28};
constexpr CODY2D::Rect kPageBounds{73, 128, 320, 26};
constexpr CODY2D::Rect kNameBounds{73, 162, 320, 54};
constexpr CODY2D::Rect kToggleBounds{113, 226, 240, 52};
#if !defined(CODY_DEVICE_UI_ONLY)
constexpr CODY2D::Rect kSubscreensBounds{113, 289, 240, 48};
#endif
constexpr CODY2D::Rect kStatusBounds{73, 344, 320, 30};
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kCyan = 0x6F9E;
constexpr uint16_t kAmber = 0xFE27;

// operation: Failed visibility editor operation.
// error: Original error retained for the caller.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error));
    }
    return error;
}

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

// font: Immutable native glyphs.
// text: Nonempty ASCII caption retained through frame retirement.
// bounds: Logical centering area within the circular display.
// color: Native RGB565 foreground.
// line: Prepared borrowed text line.
// Exception: Returns a font or layout error if the full caption cannot fit.
esp_err_t prepare_line(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line)
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
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
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
    error = CODY2D::draw_capsule_rows(view.toggle, strip, first_row, first_column);
#if !defined(CODY_DEVICE_UI_ONLY)
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

// screen: Selected configurable statistics page in firmware, or legacy root entry.
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
esp_err_t prepare_widget_visibility_view(WidgetScreen screen,
#if !defined(CODY_DEVICE_UI_ONLY)
    CompanionPanel panel, bool children,
#endif
    const WidgetVisibility &visibility, bool saving, bool save_failed, bool load_failed,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    WidgetVisibilityView &view, SceneContent &content)
{
    if (!valid_widget_visibility(visibility)) {
        return result("settings policy", ESP_ERR_INVALID_ARG);
    }
#if defined(CODY_DEVICE_UI_ONLY)
    const auto index = catalog_index(kWidgetScreens, screen);
    if (index == 0 || index >= kWidgetScreens.size()) {
        return result("catalog selection", ESP_ERR_INVALID_ARG);
    }
    const auto count = kWidgetScreens.size() - 1U;
    const auto page = index;
    const char *const heading = "Visible screens";
    const char *const label = kWidgetScreens[index].label;
    const bool same_selection = view.screen == screen;
#else
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
#endif
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

#if defined(CODY_DEVICE_UI_ONLY)
    const bool enabled = widget_visible(visibility, screen);
    constexpr bool can_toggle = true;
#else
    const bool enabled = children ? widget_panel_enabled(visibility, panel) : widget_visible(visibility, screen);
    WidgetVisibility candidate = visibility;
    const bool can_toggle = children || set_widget_visibility(candidate, screen, !enabled);
#endif
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

#if !defined(CODY_DEVICE_UI_ONLY)
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
#endif

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
#if !defined(CODY_DEVICE_UI_ONLY)
    view.panel = panel;
    view.children = children;
#endif
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
#if !defined(CODY_DEVICE_UI_ONLY)
    if (view.subscreens_visible && CODY2D::capsule_contains(view.subscreens, point)) {
        return WidgetVisibilityControl::OpenChildren;
    }
#endif
    return WidgetVisibilityControl::None;
}

} // namespace CODY
