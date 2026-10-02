#include "CODY2D_paths.hpp"

#include <algorithm>
#include <cmath>
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
        for (uint16_t hit = 0; hit < hits; hit += 2) {
            const int left = std::max(0, static_cast<int>(std::ceil(intersections[hit] - .5f)));
            const int right = std::min(static_cast<int>(strip.width),
                                      static_cast<int>(std::ceil(intersections[hit + 1] - .5f)));
            for (int column = left; column < right; ++column) {
                auto &destination = strip.pixels[row * strip.stride + column];
                destination = opacity == 255 ? palette[slot] : blend_rgb565(palette[slot], destination, opacity);
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
        width <= 0 || width > 20 || first_row + strip.height > kMaxDimension) {
        ESP_LOGE(kTag, "Stroke rejected: from=%f,%f to=%f,%f width=%f row=%u (%s)",
                 from.x, from.y, to.x, to.y, width, first_row, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const float radius = width * .5f;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int right = std::min(static_cast<int>(strip.width) - 1,
                              static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int top = std::max(static_cast<int>(first_row),
                            static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int bottom = std::min(static_cast<int>(first_row + strip.height) - 1,
                               static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float squared_length = dx*dx + dy*dy;
    for (int row = top; row <= bottom; ++row) {
        for (int column = left; column <= right; ++column) {
            const float px = column + .5f - from.x, py = row + .5f - from.y;
            const float position = squared_length == 0 ? 0 :
                std::max(0.f, std::min(1.f, (px*dx + py*dy) / squared_length));
            const float ex = px - position*dx, ey = py - position*dy;
            if (ex*ex + ey*ey <= radius*radius) {
                strip.pixels[(row - first_row)*strip.stride + column] = color;
            }
        }
    }
    return ESP_OK;
}
}  // namespace CODY2D
