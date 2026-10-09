#include "CODY2D_controls.hpp"

#include <algorithm>
#include <cmath>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_controls";

// icon: Stable action identity, independent of caption text and control ordering.
const uint8_t *icon_rows(ActionIcon icon)
{
    static constexpr uint8_t link[]{0x0E, 0x11, 0x21, 0x42, 0x84, 0x84, 0x88, 0x70};
    static constexpr uint8_t wifi[]{0x7E, 0x81, 0x3C, 0x42, 0x18, 0x24, 0x00, 0x18};
    static constexpr uint8_t gear[]{0x18, 0x5A, 0x7E, 0xC3, 0xC3, 0x7E, 0x5A, 0x18};
    static constexpr uint8_t check[]{0x00, 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00};
    static constexpr uint8_t close[]{0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
    static constexpr uint8_t arrow[]{0x08, 0x0C, 0x06, 0xFF, 0xFF, 0x06, 0x0C, 0x08};
    static constexpr uint8_t qr[]{0xE7, 0xA5, 0xE7, 0x00, 0xEA, 0xAB, 0xE4, 0x0B};
    static constexpr uint8_t retry[]{0x3C, 0x42, 0x81, 0x81, 0x01, 0xA2, 0xCC, 0xF0};
    switch (icon) {
    case ActionIcon::Link: return link;
    case ActionIcon::Wifi: return wifi;
    case ActionIcon::Gear: return gear;
    case ActionIcon::Check: return check;
    case ActionIcon::Close: return close;
    case ActionIcon::Arrow: return arrow;
    case ActionIcon::Qr: return qr;
    case ActionIcon::Retry: return retry;
    case ActionIcon::None: return nullptr;
    }
    return nullptr;
}

// control: Shared authored geometry for rendering and acquired touch coordinates.
float control_radius(const CapsuleControl &control)
{
    return control.corner_radius == 0 ? control.bounds.height * .5f :
        static_cast<float>(control.corner_radius);
}

// top: Native RGB565 top anchor.
// bottom: Native RGB565 bottom anchor.
// level: One of 32 bounded gradient steps.
uint16_t gradient_color(uint16_t top, uint16_t bottom, uint32_t level)
{
    const uint32_t red = (((top >> 11U) & 31U) * (31U - level) + ((bottom >> 11U) & 31U) * level + 15U) / 31U;
    const uint32_t green = (((top >> 5U) & 63U) * (31U - level) + ((bottom >> 5U) & 63U) * level + 15U) / 31U;
    const uint32_t blue = ((top & 31U) * (31U - level) + (bottom & 31U) * level + 15U) / 31U;
    return static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
}

} // namespace

// control: Immutable logical capsule, including its enabled action gate.
// point: Acquired logical point; coordinates are never rotated independently.
bool capsule_contains(const CapsuleControl &control, Point point)
{
    const auto &bounds = control.bounds;
    if (!control.enabled || bounds.height < 2 || bounds.width < bounds.height ||
        control.corner_radius > bounds.height / 2U ||
        point.x < bounds.x || point.y < bounds.y ||
        point.x >= bounds.x + bounds.width || point.y >= bounds.y + bounds.height) {
        return false;
    }
    const float radius = control_radius(control);
    const float x = point.x + .5f;
    const float left_center = bounds.x + radius;
    const float right_center = bounds.x + bounds.width - radius;
    const float dx = x < left_center ? x - left_center : x > right_center ? x - right_center : 0.f;
    const float y = point.y + .5f;
    const float top_center = bounds.y + radius;
    const float bottom_center = bounds.y + bounds.height - radius;
    const float dy = y < top_center ? y - top_center : y > bottom_center ? y - bottom_center : 0.f;
    return dx * dx + dy * dy <= radius * radius;
}

// control: Valid logical capsule with a flash-backed optional caption.
// strip: Valid externally owned tile; no framebuffer or heap storage is created.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns invalid geometry, icon identity, surface or text drawing errors.
esp_err_t draw_capsule_rows(const CapsuleControl &control, Surface strip,
                           uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    const auto &bounds = control.bounds;
    if (bounds.height < 2 || bounds.width < bounds.height ||
        bounds.x + bounds.width > 466 || bounds.y + bounds.height > 466 ||
        static_cast<uint32_t>(first_row) + strip.height > 466 ||
        static_cast<uint32_t>(first_column) + strip.width > 466 ||
        control.corner_radius > bounds.height / 2U ||
        static_cast<uint8_t>(control.icon) > static_cast<uint8_t>(ActionIcon::Retry)) {
        ESP_LOGE(kTag, "Capsule geometry or tile origin rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const float radius = control_radius(control);
    for (uint16_t row = top; row < bottom; ++row) {
        const float y = row + .5f;
        const float top_center = bounds.y + radius;
        const float bottom_center = bounds.y + bounds.height - radius;
        const float dy = y < top_center ? y - top_center : y > bottom_center ? y - bottom_center : 0.f;
        const float inset = radius - std::sqrt(std::max(0.f, radius * radius - dy * dy));
        // Match the pixel-center circle used by hit testing, including the ends.
        const uint16_t inset_pixels = static_cast<uint16_t>(std::ceil(inset - .5f));
        const uint16_t left = std::max<uint16_t>(first_column, bounds.x + inset_pixels);
        const uint16_t right = std::min<uint16_t>(first_column + strip.width,
            bounds.x + bounds.width - inset_pixels);
        if (right <= left) {
            continue;
        }
        const uint32_t level = (row - bounds.y) * 31U / (bounds.height - 1U);
        uint16_t color = gradient_color(control.top_color, control.bottom_color, level);
        if (!control.enabled) {
            color = fade_rgb565_to_black(color, 128);
        }
        const esp_err_t fill_error = fill_rect(strip,
            {static_cast<uint16_t>(left - first_column), static_cast<uint16_t>(row - first_row),
             static_cast<uint16_t>(right - left), 1}, color);
        if (fill_error != ESP_OK) {
            return fill_error;
        }
        if (control.border_color != 0) {
            const uint16_t border = control.enabled ? control.border_color :
                fade_rgb565_to_black(control.border_color, 128);
            const uint16_t edge_left = bounds.x + inset_pixels;
            const uint16_t edge_right = bounds.x + bounds.width - inset_pixels;
            for (uint16_t column = left; column < right; ++column) {
                if (row < bounds.y + 2U || row + 2U >= bounds.y + bounds.height ||
                    column < edge_left + 2U || column + 2U >= edge_right) {
                    strip.pixels[(row - first_row) * strip.stride + column - first_column] =
                        rgb565_native_to_stored(border, strip.byte_order);
                }
            }
        }
    }
    const uint8_t *glyph = icon_rows(control.icon);
    if (glyph != nullptr) {
        // Flash-backed 8x8 masks at 3x scale: no frame-local icon buffers.
        const int left = control.icon_center.x - 12;
        const int top = control.icon_center.y - 12;
        if (left < bounds.x || top < bounds.y || left + 24 > bounds.x + bounds.width ||
            top + 24 > bounds.y + bounds.height) {
            ESP_LOGE(kTag, "Action icon leaves control bounds (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        for (int y = std::max<int>(first_row, top); y < std::min<int>(first_row + strip.height, top + 24); ++y) {
            for (int x = std::max<int>(first_column, left); x < std::min<int>(first_column + strip.width, left + 24); ++x) {
                if ((glyph[(y - top) / 3] & (0x80U >> ((x - left) / 3))) != 0) {
                    strip.pixels[(y - first_row) * strip.stride + x - first_column] =
                        rgb565_native_to_stored(control.enabled ? control.icon_color :
                            fade_rgb565_to_black(control.icon_color, 128), strip.byte_order);
                }
            }
        }
    }
    if (!control.caption.valid) {
        return ESP_OK;
    }
    return draw_text_rows(control.caption, control.caption.text.length, strip, first_row, first_column);
}

} // namespace CODY2D
