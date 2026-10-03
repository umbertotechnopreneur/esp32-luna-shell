#include "CODY2D_controls.hpp"

#include <algorithm>
#include <cmath>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_controls";

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
    if (!control.enabled || bounds.height == 0 || bounds.width < bounds.height ||
        point.x < bounds.x || point.y < bounds.y ||
        point.x >= bounds.x + bounds.width || point.y >= bounds.y + bounds.height) {
        return false;
    }
    const float radius = bounds.height * .5f;
    const float x = point.x + .5f;
    const float left_center = bounds.x + radius;
    const float right_center = bounds.x + bounds.width - radius;
    const float dx = x < left_center ? x - left_center : x > right_center ? x - right_center : 0.f;
    const float dy = point.y + .5f - (bounds.y + radius);
    return dx * dx + dy * dy <= radius * radius;
}

// control: Valid logical capsule with a flash-backed optional caption.
// strip: Valid externally owned tile; no framebuffer or heap storage is created.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
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
        static_cast<uint32_t>(first_column) + strip.width > 466) {
        ESP_LOGE(kTag, "Capsule geometry or tile origin rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const float radius = bounds.height * .5f;
    for (uint16_t row = top; row < bottom; ++row) {
        const float dy = row + .5f - (bounds.y + radius);
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
    }
    if (!control.caption.valid) {
        return ESP_OK;
    }
    return draw_text_rows(control.caption, control.caption.text.length, strip, first_row, first_column);
}

} // namespace CODY2D
