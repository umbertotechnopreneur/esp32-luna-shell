/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "CODY2D_controls.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_controls";

// Radius: Whole-pixel circular corner radius, encoded by the template argument.
// No runtime parameters: build a symmetric corner's 8x8 coverage in flash.
template <uint16_t Radius>
constexpr std::array<uint8_t, Radius * Radius> make_corner_coverage()
{
    std::array<uint8_t, Radius * Radius> mask{};
    for (uint16_t y = 0; y < Radius; ++y) {
        for (uint16_t x = 0; x < Radius; ++x) {
            // Only boundary cells need sampling. Entirely interior/exterior
            // cells keep constant-evaluation cost bounded for large radii.
            const int far_x = Radius - x;
            const int far_y = Radius - y;
            const int near_x = far_x - 1;
            const int near_y = far_y - 1;
            if (far_x * far_x + far_y * far_y <= Radius * Radius) {
                mask[y * Radius + x] = 255;
                continue;
            }
            if (near_x * near_x + near_y * near_y >= Radius * Radius) {
                continue;
            }
            unsigned inside = 0;
            for (int sy = 0; sy < 8; ++sy) {
                for (int sx = 0; sx < 8; ++sx) {
                    const int dx = 16 * (Radius - x) - 2 * sx - 1;
                    const int dy = 16 * (Radius - y) - 2 * sy - 1;
                    inside += dx * dx + dy * dy <= 256 * Radius * Radius;
                }
            }
            mask[y * Radius + x] = static_cast<uint8_t>((inside * 255U + 32U) / 64U);
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
        case 10: return cached_corner_coverage<10>(x, y);
        case 12: return cached_corner_coverage<12>(x, y);
        case 14: return cached_corner_coverage<14>(x, y);
        case 16: return cached_corner_coverage<16>(x, y);
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
    for (unsigned sy = 0; sy < 8; ++sy) {
        for (unsigned sx = 0; sx < 8; ++sx) {
            const float dx = radius - x - (sx + .5f) / 8.f;
            const float dy = radius - y - (sy + .5f) / 8.f;
            inside += dx * dx + dy * dy <= radius * radius;
        }
    }
    return static_cast<uint8_t>((inside * 255U + 32U) / 64U);
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

// x: Continuous sample column in the native 24-pixel icon viewport.
// y: Continuous sample row in the same viewport.
// ax: Start column of the round-ended stroke.
// ay: Start row of the stroke.
// bx: End column of the stroke.
// by: End row of the stroke.
// radius: Half the consistent visual stroke weight.
constexpr bool stroke_contains(float x, float y, float ax, float ay,
                               float bx, float by, float radius = 1.f)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length_squared = dx * dx + dy * dy;
    const float along = length_squared == 0.f ? 0.f :
        std::clamp(((x - ax) * dx + (y - ay) * dy) / length_squared, 0.f, 1.f);
    const float distance_x = x - ax - along * dx;
    const float distance_y = y - ay - along * dy;
    return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

// x: Continuous native icon column.
// y: Continuous native icon row.
// cx: Ring center column.
// cy: Ring center row.
// radius: Centerline radius of the curved stroke.
// weight: Half the stroke width.
constexpr bool ring_contains(float x, float y, float cx, float cy,
                             float radius, float weight = 1.f)
{
    const float dx = x - cx;
    const float dy = y - cy;
    const float squared = dx * dx + dy * dy;
    return squared >= (radius - weight) * (radius - weight) &&
        squared <= (radius + weight) * (radius + weight);
}

// x: Continuous native icon column.
// y: Continuous native icon row.
// left: Finder's outer left edge.
// top: Finder's outer top edge.
constexpr bool qr_finder_contains(float x, float y, float left, float top)
{
    const float dx = x - left;
    const float dy = y - top;
    if (dx < 0.f || dy < 0.f || dx >= 7.f || dy >= 7.f) {
        return false;
    }
    return dx < 1.5f || dy < 1.5f || dx >= 5.5f || dy >= 5.5f ||
        (dx >= 2.5f && dx < 4.5f && dy >= 2.5f && dy < 4.5f);
}

// icon: Stable semantic identity; shape is independent of label and list ordering.
// x: Continuous native 24-pixel viewport column.
// y: Continuous native viewport row.
constexpr bool icon_contains(ActionIcon icon, float x, float y)
{
    const float dx = x - 12.f;
    const float dy = y - 12.f;
    switch (icon) {
    case ActionIcon::Link: {
        const bool lower = stroke_contains(x, y, 5.f, 18.f, 9.f, 14.f, 3.4f) &&
            !stroke_contains(x, y, 5.f, 18.f, 9.f, 14.f, 1.5f);
        const bool upper = stroke_contains(x, y, 15.f, 10.f, 19.f, 6.f, 3.4f) &&
            !stroke_contains(x, y, 15.f, 10.f, 19.f, 6.f, 1.5f);
        return lower || upper || stroke_contains(x, y, 9.f, 15.f, 15.f, 9.f);
    }
    case ActionIcon::Wifi:
        return (y < 20.f - (dx < 0.f ? -dx : dx) &&
            (ring_contains(x, y, 12.f, 20.f, 14.f) ||
             ring_contains(x, y, 12.f, 20.f, 9.5f) ||
             ring_contains(x, y, 12.f, 20.f, 5.f))) ||
            stroke_contains(x, y, 12.f, 20.f, 12.f, 20.f, 1.4f);
    case ActionIcon::Gear: {
        const float ax = dx < 0.f ? -dx : dx;
        const float ay = dy < 0.f ? -dy : dy;
        const float diagonal = ax > ay ? ax - ay : ay - ax;
        const float squared = dx * dx + dy * dy;
        return squared >= 11.f && squared <= 100.f &&
            (squared <= 52.f || ax < 1.4f || ay < 1.4f || diagonal < 1.8f);
    }
    case ActionIcon::Check:
        return stroke_contains(x, y, 4.5f, 12.f, 9.5f, 17.f, 1.1f) ||
            stroke_contains(x, y, 9.5f, 17.f, 19.5f, 6.5f, 1.1f);
    case ActionIcon::Close:
        return stroke_contains(x, y, 5.5f, 5.5f, 18.5f, 18.5f) ||
            stroke_contains(x, y, 18.5f, 5.5f, 5.5f, 18.5f);
    case ActionIcon::Arrow:
        return stroke_contains(x, y, 4.5f, 12.f, 19.f, 12.f) ||
            stroke_contains(x, y, 13.f, 6.f, 19.f, 12.f) ||
            stroke_contains(x, y, 13.f, 18.f, 19.f, 12.f);
    case ActionIcon::Qr:
        return qr_finder_contains(x, y, 2.f, 2.f) || qr_finder_contains(x, y, 15.f, 2.f) ||
            qr_finder_contains(x, y, 2.f, 15.f) ||
            (x >= 14.f && x < 17.f && y >= 14.f && y < 22.f) ||
            (x >= 19.f && x < 22.f && y >= 14.f && y < 17.f) ||
            (x >= 17.f && x < 22.f && y >= 19.f && y < 22.f);
    case ActionIcon::Retry:
        return (ring_contains(x, y, 12.f, 12.f, 8.f) && !(dx > 2.f && dy < -2.f)) ||
            stroke_contains(x, y, 18.f, 4.f, 18.f, 10.f) ||
            stroke_contains(x, y, 12.f, 10.f, 18.f, 10.f);
    case ActionIcon::Motion:
        return stroke_contains(x, y, 9.f, 6.f, 15.f, 6.f, .9f) ||
            stroke_contains(x, y, 15.f, 6.f, 15.f, 18.f, .9f) ||
            stroke_contains(x, y, 15.f, 18.f, 9.f, 18.f, .9f) ||
            stroke_contains(x, y, 9.f, 18.f, 9.f, 6.f, .9f) ||
            (ring_contains(x, y, 12.f, 12.f, 9.f) && (x < 5.f || x > 19.f));
    case ActionIcon::Rotate:
        return (ring_contains(x, y, 12.f, 12.f, 8.f) &&
            !((dx > 2.f && dy < -2.f) || (dx < -2.f && dy > 2.f))) ||
            stroke_contains(x, y, 18.f, 4.f, 18.f, 9.f) ||
            stroke_contains(x, y, 13.f, 9.f, 18.f, 9.f) ||
            stroke_contains(x, y, 6.f, 20.f, 6.f, 15.f) ||
            stroke_contains(x, y, 6.f, 15.f, 11.f, 15.f);
    case ActionIcon::Sound:
        return stroke_contains(x, y, 3.f, 9.f, 3.f, 15.f, .9f) ||
            stroke_contains(x, y, 3.f, 9.f, 7.f, 9.f, .9f) ||
            stroke_contains(x, y, 3.f, 15.f, 7.f, 15.f, .9f) ||
            stroke_contains(x, y, 7.f, 9.f, 12.f, 5.f, .9f) ||
            stroke_contains(x, y, 12.f, 5.f, 12.f, 19.f, .9f) ||
            stroke_contains(x, y, 12.f, 19.f, 7.f, 15.f, .9f) ||
            (x > 15.f && ring_contains(x, y, 12.f, 12.f, 5.5f, .9f)) ||
            (x > 18.f && ring_contains(x, y, 12.f, 12.f, 9.f, .9f));
    case ActionIcon::Orientation:
        return stroke_contains(x, y, 12.f, 3.5f, 12.f, 20.5f) ||
            stroke_contains(x, y, 8.f, 7.5f, 12.f, 3.5f) ||
            stroke_contains(x, y, 16.f, 7.5f, 12.f, 3.5f) ||
            stroke_contains(x, y, 8.f, 16.5f, 12.f, 20.5f) ||
            stroke_contains(x, y, 16.f, 16.5f, 12.f, 20.5f);
    case ActionIcon::None:
        return false;
    }
    return false;
}

// icon: Stable semantic identity sampled directly from its authored curves.
// y: Fixed destination row, evaluated independently at compilation.
constexpr std::array<uint8_t, 24> make_icon_row(ActionIcon icon, int y)
{
    std::array<uint8_t, 24> row{};
    for (int x = 0; x < 24; ++x) {
        unsigned inside = 0;
        for (int sy = 0; sy < 4; ++sy) {
            for (int sx = 0; sx < 4; ++sx) {
                // Final-size vector coverage replaces the enlarged pixel glyph.
                inside += icon_contains(icon, x + (sx + .5f) / 4.f, y + (sy + .5f) / 4.f);
            }
        }
        row[x] = static_cast<uint8_t>((inside * 255U + 8U) / 16U);
    }
    return row;
}

// Separate initializers bound each compiler's constant-evaluation work. A single
// large initializer would repeat every stroke projection under one step budget.
template <ActionIcon Icon, std::size_t Row>
inline constexpr auto kIconRowCoverage = make_icon_row(Icon, Row);

// Icon: Semantic identity encoded by the template, with no runtime shape work.
// rows: Compile-time row indices whose masks have already been evaluated separately.
template <ActionIcon Icon, std::size_t... Rows>
constexpr std::array<uint8_t, 24 * 24> make_icon_coverage(std::index_sequence<Rows...> rows)
{
    (void) rows;
    constexpr std::array<std::array<uint8_t, 24>, 24> row_masks{kIconRowCoverage<Icon, Rows>...};
    std::array<uint8_t, 24 * 24> mask{};
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 24; ++x) {
            mask[y * 24 + x] = row_masks[y][x];
        }
    }
    return mask;
}

// Icon: Semantic identity encoded in the immutable flash mask.
// No runtime parameters: return coverage storage with firmware lifetime.
template <ActionIcon Icon>
const uint8_t *cached_icon_coverage()
{
    static constexpr auto mask = make_icon_coverage<Icon>(std::make_index_sequence<24>{});
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
    case ActionIcon::Motion: return cached_icon_coverage<ActionIcon::Motion>();
    case ActionIcon::Rotate: return cached_icon_coverage<ActionIcon::Rotate>();
    case ActionIcon::Sound: return cached_icon_coverage<ActionIcon::Sound>();
    case ActionIcon::Orientation: return cached_icon_coverage<ActionIcon::Orientation>();
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
        static_cast<uint8_t>(control.icon) > static_cast<uint8_t>(ActionIcon::Orientation)) {
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
