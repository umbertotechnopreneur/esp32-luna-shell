#pragma once

#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"

namespace CODY::UI {

inline constexpr CODY2D::Rect kBackBounds{10, 203, 60, 60};
inline constexpr CODY2D::Rect kForwardBounds{396, 203, 60, 60};
enum class NavigationDirection : uint8_t { Previous, Next };
struct NavigationColors { uint16_t top; uint16_t bottom; uint16_t ink; };

// direction: Icon orientation in the same logical coordinate basis as touch.
// enabled: Disabled actions keep their full target but use dimmer ink.
// strip: Caller-owned destination tile, including the lateral region.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// opacity: Current visibility across the navigation fade.
// background: Native RGB565 scene color.
// colors: Existing navigation gradient and ink in native RGB565.
// Exception: Returns the first real capsule or stroke drawing error.
inline esp_err_t draw_navigation_chevron(NavigationDirection direction, bool enabled,
                                  CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
                                  uint8_t opacity, uint16_t background, NavigationColors colors)
{
    if (opacity == 0) {
        return ESP_OK;
    }
    const auto bounds = direction == NavigationDirection::Previous ? kBackBounds : kForwardBounds;
    CODY2D::CapsuleControl control{bounds, {}, colors.top, colors.bottom, enabled};
    if (opacity != 255) {
        const uint8_t brightness = enabled ? 255 : 128;
        control.top_color = CODY2D::blend_rgb565(
            CODY2D::fade_rgb565_to_black(control.top_color, brightness), background, opacity);
        control.bottom_color = CODY2D::blend_rgb565(
            CODY2D::fade_rgb565_to_black(control.bottom_color, brightness), background, opacity);
        control.enabled = true; // Disabled shading was applied before the background blend.
    }
    esp_err_t error = CODY2D::draw_capsule_rows(control, strip, first_row, first_column);
    const float center_x = bounds.x + bounds.width / 2.f;
    const float center_y = bounds.y + bounds.height / 2.f;
    const float sign = direction == NavigationDirection::Previous ? -1.f : 1.f;
    const CODY2D::Vec2 points[] = {{center_x - sign * 3.f, center_y - 8.f},
        {center_x + sign * 5.f, center_y}, {center_x - sign * 3.f, center_y + 8.f}};
    const uint16_t ink = CODY2D::blend_rgb565(
        CODY2D::fade_rgb565_to_black(colors.ink, enabled ? 255 : 128), background, opacity);
    for (uint16_t index = 0; error == ESP_OK && index < 2; ++index) {
        error = CODY2D::stroke_rows(strip, first_row, points[index], points[index + 1U], 2.f, ink, first_column);
    }
    return error;
}

// direction: Requested logical navigation target.
// point: Logical contact position in the display orientation.
// enabled: Hidden or disabled navigation is not admitted by this helper.
inline bool navigation_contains(NavigationDirection direction, CODY2D::Point point, bool enabled = true)
{
    const CODY2D::CapsuleControl control{
        direction == NavigationDirection::Previous ? kBackBounds : kForwardBounds, {}, 0, 0, enabled};
    return CODY2D::capsule_contains(control, point);
}

// point: Diagnostic view's existing integer-centered circular hit test.
inline bool diagnostic_back_contains(CODY2D::Point point)
{
    const int x = point.x - (kBackBounds.x + kBackBounds.width / 2);
    const int y = point.y - (kBackBounds.y + kBackBounds.height / 2);
    const int radius = kBackBounds.width / 2;
    return x * x + y * y <= radius * radius;
}

} // namespace CODY::UI
