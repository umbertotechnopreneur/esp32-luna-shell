#include "CODY2D_paths.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_paths";

// point: Canvas coordinate whose bounded extent makes integer raster conversion safe.
bool valid_point(Vec2 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::abs(point.x) <= 2048 && std::abs(point.y) <= 2048;
}

// source: Native RGB565 source color from the immutable palette.
// destination: Current native RGB565 pixel below this path.
// opacity: Source coverage in 0..255; integer rounding is shared with the host renderer.
uint16_t blend_rgb565(uint16_t source, uint16_t destination, uint8_t opacity)
{
    const uint32_t alpha = opacity, inverse = 255U - alpha;
    const uint32_t red = (((source >> 11) & 31U) * alpha + ((destination >> 11) & 31U) * inverse + 127U) / 255U;
    const uint32_t green = (((source >> 5) & 63U) * alpha + ((destination >> 5) & 63U) * inverse + 127U) / 255U;
    const uint32_t blue = ((source & 31U) * alpha + (destination & 31U) * inverse + 127U) / 255U;
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}
}  // namespace

// strip: Caller-owned destination; no row padding is touched.
// first_row: Canvas origin of this tile, making gradient and clipping strip-independent.
// vertices: Readable evaluated polygon; all inputs are checked before writing.
// count: Number of supplied vertices, including an optional repeated closure.
// palette: Readable table of 256 native RGB565 entries.
// first_color: Top slot, also the solid slot for a constant-color polygon.
// last_color: Bottom slot in the same ordered ramp.
// opacity: Source coverage; zero still checks the supplied geometry before returning without writes.
esp_err_t fill_path_rows(Surface strip, uint16_t first_row,
                        const Vec2 *vertices, uint16_t count,
                        const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                        uint8_t opacity)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) return error;
    if (!vertices || !palette || count < 3 || count > kMaxPathVertices ||
        first_color > last_color || first_row + strip.height > kMaxDimension) {
        ESP_LOGE(kTag, "Fill rejected: vertices=%p count=%u palette=%p slots=%u..%u row=%u (%s)",
                 vertices, count, palette, first_color, last_color, first_row,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    float top = vertices[0].y, bottom = vertices[0].y;
    for (uint16_t index = 0; index < count; ++index) {
        if (!valid_point(vertices[index])) {
            ESP_LOGE(kTag, "Fill point %u rejected: %f,%f (%s)", index,
                     vertices[index].x, vertices[index].y, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        top = std::min(top, vertices[index].y);
        bottom = std::max(bottom, vertices[index].y);
    }
    if (opacity == 0) return ESP_OK;
    float intersections[kMaxPathVertices];
    for (uint16_t row = 0; row < strip.height; ++row) {
        const float scan_y = first_row + row + .5f;
        if (scan_y < top || scan_y >= bottom) continue;
        uint16_t hits = 0;
        for (uint16_t index = 0; index < count; ++index) {
            const Vec2 a = vertices[index], b = vertices[(index + 1) % count];
            if ((a.y <= scan_y && b.y > scan_y) || (b.y <= scan_y && a.y > scan_y)) {
                intersections[hits++] = a.x + (scan_y - a.y) * (b.x - a.x) / (b.y - a.y);
            }
        }
        if (hits % 2 != 0) {
            ESP_LOGE(kTag, "Fill scanline %u has odd intersection count %u (%s)",
                     first_row + row, hits, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
        std::sort(intersections, intersections + hits);
        const float ratio = (scan_y - top) / (bottom - top);
        const uint16_t slot = first_color + static_cast<uint16_t>(
            std::floor(ratio * (last_color - first_color) + .5f));
        const uint16_t color = palette[slot];
        for (uint16_t hit = 0; hit < hits; hit += 2) {
            const int left = std::max(0, static_cast<int>(std::ceil(intersections[hit] - .5f)));
            const int right = std::min(static_cast<int>(strip.width),
                                      static_cast<int>(std::ceil(intersections[hit + 1] - .5f)));
            if (left >= right) {
                continue;
            }
            if (opacity == 255) {
                // The common opaque span needs neither a destination read nor
                // a per-pixel palette lookup/coverage branch.
                std::fill(strip.pixels + row * strip.stride + left,
                          strip.pixels + row * strip.stride + right, color);
            } else {
                for (int column = left; column < right; ++column) {
                    auto &destination = strip.pixels[row * strip.stride + column];
                    destination = blend_rgb565(color, destination, opacity);
                }
            }
        }
    }
    return ESP_OK;
}

// strip: Valid writable tile, checked before any raster work.
// first_row: Tile origin within the full logical canvas.
// from: Start of the line, which may lie outside the tile.
// to: End of the line, equal to from for a round point marker.
// width: Round stroke diameter in pixels.
// color: Native RGB565 fill.
esp_err_t stroke_rows(Surface strip, uint16_t first_row,
                      Vec2 from, Vec2 to, float width, uint16_t color)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) return error;
    if (!valid_point(from) || !valid_point(to) || !std::isfinite(width) ||
        width <= 0 || width > kMaxStrokeWidth || first_row + strip.height > kMaxDimension) {
        ESP_LOGE(kTag, "Stroke rejected: from=%f,%f to=%f,%f width=%f row=%u (%s)",
                 from.x, from.y, to.x, to.y, width, first_row, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const float radius = width * .5f;
    // Most debug links miss any particular tile. Reject their loose float
    // bounds before integer conversion, projection setup and square roots.
    // These bounds are deliberately wider than the pixel-center coverage.
    if (std::max(from.y, to.y) + radius < first_row ||
        std::min(from.y, to.y) - radius > first_row + strip.height ||
        std::max(from.x, to.x) + radius < 0 ||
        std::min(from.x, to.x) - radius > strip.width) {
        return ESP_OK;
    }
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int right = std::min(static_cast<int>(strip.width) - 1,
                              static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int top = std::max(static_cast<int>(first_row),
                            static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int bottom = std::min(static_cast<int>(first_row + strip.height) - 1,
                               static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    if (left > right || top > bottom) {
        return ESP_OK;
    }
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float squared_length = dx*dx + dy*dy;
    const float squared_radius = radius * radius;
    // For interior pixels, compare the squared cross product with r² * |d|².
    // This avoids software division and closest-point construction. A small
    // guard repeats the original projection at rounding-sensitive boundaries.
    // Endpoint caps still use the original direct squared-distance expression.
    const float max_px = std::max(std::abs(.5f - from.x), std::abs(strip.width - .5f - from.x));
    const float max_py = std::max(std::abs(.5f - from.y), std::abs(kMaxDimension - .5f - from.y));
    const float displacement_error = (max_px + max_py + std::abs(dx) + std::abs(dy)) *
        (16.f * std::numeric_limits<float>::epsilon());
    const float cross_threshold = squared_radius * squared_length;
    const float cross_guard = squared_length * displacement_error * (2.f * radius + displacement_error) +
        cross_threshold * (8.f * std::numeric_limits<float>::epsilon());
    // The old box visits every pixel between diagonal endpoints. Bound each
    // scanline by the infinite stroke band instead; the original segment test
    // below still supplies the exact round caps and pixel coverage.
    const bool bound_scanline = std::abs(dy) > .001f;
    const float band_half_width = bound_scanline ?
        radius * std::sqrt(squared_length) / std::abs(dy) : 0.f;
    const float slope = bound_scanline ? dx / dy : 0.f;
    for (int row = top; row <= bottom; ++row) {
        int scan_left = left;
        int scan_right = right;
        if (bound_scanline) {
            const float center = from.x + (row + .5f - from.y) * slope;
            // One extra pixel conservatively covers floating-point rounding.
            // Clip floats before conversion: nearly horizontal lines can have
            // a very wide band even though their canvas bounds remain small.
            const float band_margin = 1.f + (std::abs(center) + band_half_width) *
                (4.f * std::numeric_limits<float>::epsilon());
            const float band_left = center - band_half_width - band_margin;
            const float band_right = center + band_half_width + band_margin;
            if (band_right < left || band_left > right) {
                continue;
            }
            scan_left = static_cast<int>(std::floor(std::max(static_cast<float>(left), band_left)));
            scan_right = static_cast<int>(std::ceil(std::min(static_cast<float>(right), band_right)));
        }
        for (int column = scan_left; column <= scan_right; ++column) {
            const float px = column + .5f - from.x;
            const float py = row + .5f - from.y;
            const float projection = px*dx + py*dy;
            bool covered = false;
            if (squared_length == 0 || projection <= 0) {
                covered = px*px + py*py <= squared_radius;
            } else if (projection >= squared_length) {
                const float ex = px - dx;
                const float ey = py - dy;
                covered = ex*ex + ey*ey <= squared_radius;
            } else {
                const float cross = px*dy - py*dx;
                const float squared_cross = cross * cross;
                covered = squared_cross <= cross_threshold;
                if (std::abs(squared_cross - cross_threshold) <= cross_guard) {
                    const float exact_position = std::max(0.f, std::min(1.f, projection / squared_length));
                    const float exact_x = px - exact_position * dx;
                    const float exact_y = py - exact_position * dy;
                    covered = exact_x*exact_x + exact_y*exact_y <= squared_radius;
                }
            }
            if (covered) {
                strip.pixels[(row - first_row)*strip.stride + column] = color;
            }
        }
    }
    return ESP_OK;
}
}  // namespace CODY2D
