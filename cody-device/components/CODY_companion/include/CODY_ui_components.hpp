#pragma once

#include <cstddef>
#include <cstring>
#include <limits>

#include "CODY2D_controls.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY::UI {

// RGB565 roles mirror the matching device entries in assets/cody/palette/theme.json.
namespace Palette {
inline constexpr uint16_t text = Theme::text_default;
inline constexpr uint16_t muted = Theme::text_muted;
inline constexpr uint16_t cyan = Theme::accent_cyan;
inline constexpr uint16_t positive = Theme::secondary_bright;
inline constexpr uint16_t warning = Theme::status_warning_content;
}

enum class TextAlignment : uint8_t { Center, Left };

// font: Immutable native glyphs retained by the display owner.
// text: Nonempty caption borrowed through frame retirement.
// bounds: Admitted logical text slot.
// color: Native RGB565 foreground.
// line: Receives the prepared borrowed text line.
// maximum: Maximum accepted caption length for the calling view.
// alignment: Horizontal placement within the slot.
// Exception: Preserves glyph errors and rejects text that cannot fit.
inline esp_err_t prepare_text(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line, size_t maximum, TextAlignment alignment = TextAlignment::Center)
{
    const auto length = std::strlen(text);
    if (length == 0 || length > maximum || length > std::numeric_limits<uint16_t>::max() ||
        font.height > bounds.height) return ESP_ERR_INVALID_SIZE;
    const CODY2D::TextSpan span{text, static_cast<uint16_t>(length)};
    uint32_t width = 0;
    const auto error = CODY2D::measure_text_width(font, span, width);
    if (error != ESP_OK) return error;
    if (width > bounds.width) return ESP_ERR_INVALID_SIZE;
    return CODY2D::prepare_text_line(font, span,
        {static_cast<int16_t>(bounds.x + (alignment == TextAlignment::Left ? 0U : (bounds.width - width) / 2U)),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, line);
}

// Existing content-view gradients; these are distinct from the setup palette roles.
struct ControlColors { uint16_t top; uint16_t bottom; uint16_t border; };
inline constexpr ControlColors kContentPrimary{Theme::controls_content_primary_top,
    Theme::controls_content_primary_bottom, Theme::controls_content_primary_border};
inline constexpr ControlColors kContentSecondary{Theme::controls_content_secondary_top,
    Theme::controls_content_secondary_bottom, Theme::controls_content_secondary_border};
inline constexpr ControlColors kContentSelected{Theme::controls_content_selected_top,
    Theme::controls_content_selected_bottom, Theme::controls_content_selected_border};

// control: Caller-owned control; geometry, caption and enabled state are preserved.
// colors: Exact gradient and border values for its existing visual role.
inline void apply_control_colors(CODY2D::CapsuleControl &control, ControlColors colors)
{
    control.top_color = colors.top;
    control.bottom_color = colors.bottom;
    control.border_color = colors.border;
}

// control: Caller-owned button with bounds and enabled state already set.
// font: Immutable caption glyphs.
// text: Nonempty caption retained through frame retirement.
// color: Native RGB565 caption color.
// maximum: Maximum accepted caption length for the calling view.
// colors: Existing semantic gradient and border.
// Exception: Returns the original text measurement or preparation failure.
inline esp_err_t prepare_button(CODY2D::CapsuleControl &control, const CODY2D::MonoFont &font,
    const char *text, uint16_t color, size_t maximum, ControlColors colors)
{
    apply_control_colors(control, colors);
    return prepare_text(font, text, control.bounds, color, control.caption, maximum);
}

// control: Prepared action and unchanged interaction geometry.
// strip: Borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// size: Square icon side in logical pixels; defaults to the standard 24-pixel action icon.
// Exception: Returns the original icon raster error or rejects an out-of-bounds icon.
inline esp_err_t draw_action_icon(const CODY2D::CapsuleControl &control, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t size = 24)
{
    if (control.icon == CODY2D::ActionIcon::None) return CODY2D::validate_surface(strip);
    if (size == 0) return ESP_ERR_INVALID_SIZE;
    const int left = control.icon_center.x - static_cast<int>(size / 2U);
    const int top = control.icon_center.y - static_cast<int>(size / 2U);
    const auto &bounds = control.bounds;
    if (left < bounds.x || top < bounds.y || left + size > bounds.x + bounds.width ||
        top + size > bounds.y + bounds.height) return ESP_ERR_INVALID_SIZE;
    return CODY2D::draw_action_icon_rows(control.icon,
        {static_cast<uint16_t>(left), static_cast<uint16_t>(top), size, size},
        control.enabled ? control.icon_color : Palette::muted, strip, first_row, first_column);
}

// control: Prepared caption, optional existing icon and unchanged interaction geometry.
// strip: Borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// icon_size: Square side for the optional icon, without changing touch bounds.
// Exception: Returns the original icon, text or surface raster error.
inline esp_err_t draw_text_action(const CODY2D::CapsuleControl &control, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t icon_size = 24)
{
    const auto error = draw_action_icon(control, strip, first_row, first_column, icon_size);
    if (error != ESP_OK || !control.caption.valid) return error;
    return CODY2D::draw_text_rows(control.caption, control.caption.text.length, strip, first_row, first_column);
}

} // namespace CODY::UI
