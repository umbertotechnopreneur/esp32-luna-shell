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

} // namespace CODY::UI
