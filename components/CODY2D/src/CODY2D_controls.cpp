#include "CODY2D_controls.hpp"

#include <algorithm>
#include <array>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_controls";

// icon: Stable action identity, independent of caption text and control ordering.
constexpr std::array<uint8_t, 8> icon_rows(ActionIcon icon)
{
    switch (icon) {
    case ActionIcon::Link: return {0x0E, 0x11, 0x21, 0x42, 0x84, 0x84, 0x88, 0x70};
    case ActionIcon::Wifi: return {0x7E, 0x81, 0x3C, 0x42, 0x18, 0x24, 0x00, 0x18};
    case ActionIcon::Gear: return {0x18, 0x5A, 0x7E, 0xC3, 0xC3, 0x7E, 0x5A, 0x18};
    case ActionIcon::Check: return {0x00, 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00};
    case ActionIcon::Close: return {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
    case ActionIcon::Arrow: return {0x08, 0x0C, 0x06, 0xFF, 0xFF, 0x06, 0x0C, 0x08};
    case ActionIcon::Qr: return {0xE7, 0xA5, 0xE7, 0x00, 0xEA, 0xAB, 0xE4, 0x0B};
    case ActionIcon::Retry: return {0x3C, 0x42, 0x81, 0x81, 0x01, 0xA2, 0xCC, 0xF0};
    case ActionIcon::None: return {};
    }
    return {};
}

// Radius: Whole-pixel circular corner radius, encoded by the template argument.
// No runtime parameters: build a symmetric corner's 4x4 coverage in flash.
template <uint16_t Radius>
constexpr std::array<uint8_t, Radius * Radius> make_corner_coverage()
{
    std::array<uint8_t, Radius * Radius> mask{};
    for (uint16_t y = 0; y < Radius; ++y) {
        for (uint16_t x = 0; x < Radius; ++x) {
            unsigned inside = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const int dx = 8 * (Radius - x) - 2 * sx - 1;
                    const int dy = 8 * (Radius - y) - 2 * sy - 1;
                    inside += dx * dx + dy * dy <= 64 * Radius * Radius;
                }
            }
            mask[y * Radius + x] = static_cast<uint8_t>((inside * 255U + 8U) / 16U);
        }
    }
    return mask;
}

// Radius: Compile-time corner identity shared by symmetric raster corners.
// x: Column strictly below Radius.
// y: Row strictly below Radius.
template <uint16_t Radius>
uint8_t cached_corner_coverage(uint16_t x, uint16_t y)
{
    static constexpr auto mask = make_corner_coverage<Radius>();
    return mask[y * Radius + x];
}

// radius: Authored radius; cached cases include the matching two-pixel inner border.
// x: Pixel column measured from the nearest corner's outer edge.
// y: Pixel row measured from the nearest corner's outer edge.
uint8_t corner_coverage(float radius, uint16_t x, uint16_t y)
{
    // Static masks cover every current Cody control without mutable caches or allocation.
    const unsigned whole_radius = static_cast<unsigned>(radius);
    if (radius == whole_radius) {
        switch (whole_radius) {
        case 17: return cached_corner_coverage<17>(x, y);
        case 19: return cached_corner_coverage<19>(x, y);
        case 20: return cached_corner_coverage<20>(x, y);
        case 22: return cached_corner_coverage<22>(x, y);
        case 24: return cached_corner_coverage<24>(x, y);
        case 26: return cached_corner_coverage<26>(x, y);
        case 28: return cached_corner_coverage<28>(x, y);
        case 30: return cached_corner_coverage<30>(x, y);
        case 34: return cached_corner_coverage<34>(x, y);
        case 36: return cached_corner_coverage<36>(x, y);
        default: break;
        }
    }
    // The public primitive also accepts other radii and odd heights. Keep their
    // coverage exact to the same sampling rule instead of substituting geometry.
    unsigned inside = 0;
    for (unsigned sy = 0; sy < 4; ++sy) {
        for (unsigned sx = 0; sx < 4; ++sx) {
            const float dx = radius - x - (sx + .5f) / 4.f;
            const float dy = radius - y - (sy + .5f) / 4.f;
            inside += dx * dx + dy * dy <= radius * radius;
        }
    }
    return static_cast<uint8_t>((inside * 255U + 8U) / 16U);
}

// bounds: Logical rounded rectangle, already validated by the drawing entrypoint.
// radius: Circular radius shared with hit testing.
// x: Logical pixel column.
// y: Logical pixel row.
uint8_t rounded_coverage(Rect bounds, float radius, uint16_t x, uint16_t y)
{
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width || y >= bounds.y + bounds.height) {
        return 0;
    }
    const uint16_t corner_x = std::min<uint16_t>(x - bounds.x, bounds.x + bounds.width - 1U - x);
    const uint16_t corner_y = std::min<uint16_t>(y - bounds.y, bounds.y + bounds.height - 1U - y);
    if (corner_x >= radius || corner_y >= radius) {
        return 255;
    }
    return corner_coverage(radius, corner_x, corner_y);
}

// glyph: Original flash-mask rows for one semantic icon.
// x: Source column, which may fall outside the mask during edge interpolation.
// y: Source row, which may fall outside the mask during edge interpolation.
constexpr int icon_sample(const std::array<uint8_t, 8> &glyph, int x, int y)
{
    return x >= 0 && x < 8 && y >= 0 && y < 8 && (glyph[y] & (0x80U >> x)) != 0 ? 1 : 0;
}

// icon: Stable semantic identity; evaluated once at compilation.
constexpr std::array<uint8_t, 24 * 24> make_icon_coverage(ActionIcon icon)
{
    const auto glyph = icon_rows(icon);
    std::array<uint8_t, 24 * 24> mask{};
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            unsigned inside = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    // Bilinear contour reconstruction rounds diagonal block corners.
                    // Coordinates use 24 units per source pixel and true floor division.
                    const int px = x * 8 + sx * 2 + 1 - 12;
                    const int py = y * 8 + sy * 2 + 1 - 12;
                    const int gx = px < 0 ? -1 : px / 24;
                    const int gy = py < 0 ? -1 : py / 24;
                    const int fx = px - gx * 24;
                    const int fy = py - gy * 24;
                    const int weight = icon_sample(glyph, gx, gy) * (24 - fx) * (24 - fy) +
                        icon_sample(glyph, gx + 1, gy) * fx * (24 - fy) +
                        icon_sample(glyph, gx, gy + 1) * (24 - fx) * fy +
                        icon_sample(glyph, gx + 1, gy + 1) * fx * fy;
                    inside += weight >= 288;
                }
            }
            mask[y * 24 + x] = static_cast<uint8_t>((inside * 255U + 8U) / 16U);
        }
    }
    return mask;
}

// Icon: Semantic identity encoded in the immutable flash mask.
// No runtime parameters: return coverage storage with firmware lifetime.
template <ActionIcon Icon>
const uint8_t *cached_icon_coverage()
{
    static constexpr auto mask = make_icon_coverage(Icon);
    return mask.data();
}

// icon: Validated semantic identity; None has no raster coverage.
const uint8_t *icon_coverage(ActionIcon icon)
{
    switch (icon) {
    case ActionIcon::Link: return cached_icon_coverage<ActionIcon::Link>();
    case ActionIcon::Wifi: return cached_icon_coverage<ActionIcon::Wifi>();
    case ActionIcon::Gear: return cached_icon_coverage<ActionIcon::Gear>();
    case ActionIcon::Check: return cached_icon_coverage<ActionIcon::Check>();
    case ActionIcon::Close: return cached_icon_coverage<ActionIcon::Close>();
    case ActionIcon::Arrow: return cached_icon_coverage<ActionIcon::Arrow>();
    case ActionIcon::Qr: return cached_icon_coverage<ActionIcon::Qr>();
    case ActionIcon::Retry: return cached_icon_coverage<ActionIcon::Retry>();
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
// level: Row interpolation weight in 0..255, preserving RGB565 precision.
uint16_t gradient_color(uint16_t top, uint16_t bottom, uint32_t level)
{
    const uint32_t red = (((top >> 11U) & 31U) * (255U - level) + ((bottom >> 11U) & 31U) * level + 127U) / 255U;
    const uint32_t green = (((top >> 5U) & 63U) * (255U - level) + ((bottom >> 5U) & 63U) * level + 127U) / 255U;
    const uint32_t blue = ((top & 31U) * (255U - level) + (bottom & 31U) * level + 127U) / 255U;
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
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const float radius = control_radius(control);
    const bool has_inner = bounds.height > 4 && bounds.width > 4;
    const Rect inner_bounds{static_cast<uint16_t>(bounds.x + 2U), static_cast<uint16_t>(bounds.y + 2U),
        static_cast<uint16_t>(has_inner ? bounds.width - 4U : 0),
        static_cast<uint16_t>(has_inner ? bounds.height - 4U : 0)};
    const float inner_radius = std::max(0.f, radius - 2.f);
    const uint16_t border = control.enabled ? control.border_color : fade_rgb565_to_black(control.border_color, 128);
    for (uint16_t row = top; row < bottom; ++row) {
        const uint32_t level = (row - bounds.y) * 255U / (bounds.height - 1U);
        uint16_t color = gradient_color(control.top_color, control.bottom_color, level);
        if (!control.enabled) {
            color = fade_rgb565_to_black(color, 128);
        }
        for (uint16_t column = left; column < right; ++column) {
            const uint8_t outer = rounded_coverage(bounds, radius, column, row);
            if (outer == 0) {
                continue;
            }
            uint16_t foreground = color;
            if (control.border_color != 0) {
                const uint8_t inner = has_inner ? rounded_coverage(inner_bounds, inner_radius, column, row) : 0;
                // Inner and ring coverage partition the outer shape. Blend the
                // ring first, then composite once against the existing scene.
                const uint8_t ring = static_cast<uint8_t>((static_cast<unsigned>(outer - inner) * 255U + outer / 2U) / outer);
                foreground = blend_rgb565(border, color, ring);
            }
            uint16_t &destination = strip.pixels[(row - first_row) * strip.stride + column - first_column];
            const uint16_t background = rgb565_stored_to_native(destination, strip.byte_order);
            destination = rgb565_native_to_stored(
                outer == 255 ? foreground : blend_rgb565(foreground, background, outer), strip.byte_order);
        }
    }
    const uint8_t *glyph = icon_coverage(control.icon);
    if (glyph != nullptr) {
        // Static antialiased 24x24 coverage: no per-frame supersampling or buffers.
        const int left = control.icon_center.x - 12;
        const int top = control.icon_center.y - 12;
        if (left < bounds.x || top < bounds.y || left + 24 > bounds.x + bounds.width ||
            top + 24 > bounds.y + bounds.height) {
            ESP_LOGE(kTag, "Action icon leaves control bounds (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        for (int y = std::max<int>(first_row, top); y < std::min<int>(first_row + strip.height, top + 24); ++y) {
            for (int x = std::max<int>(first_column, left); x < std::min<int>(first_column + strip.width, left + 24); ++x) {
                const uint8_t coverage = static_cast<uint8_t>((glyph[(y - top) * 24 + x - left] *
                    rounded_coverage(bounds, radius, x, y) + 127U) / 255U);
                if (coverage != 0) {
                    uint16_t &destination = strip.pixels[(y - first_row) * strip.stride + x - first_column];
                    const uint16_t background = rgb565_stored_to_native(destination, strip.byte_order);
                    const uint16_t color = control.enabled ? control.icon_color : fade_rgb565_to_black(control.icon_color, 128);
                    destination = rgb565_native_to_stored(blend_rgb565(color, background, coverage), strip.byte_order);
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
