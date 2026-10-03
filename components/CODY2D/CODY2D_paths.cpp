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
    const uint32_t alpha = opacity;
    const uint32_t inverse = 255U - alpha;
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
// first_column: Canvas x origin; only destination indexing uses tile-local coordinates.
esp_err_t fill_path_rows(Surface strip, uint16_t first_row,
                        const Vec2 *vertices, uint16_t count,
                        const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                        uint8_t opacity, uint16_t first_column)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    if (!vertices || !palette || count < 3 || count > kMaxPathVertices ||
        first_color > last_color || first_row + strip.height > kMaxDimension ||
        first_column + strip.width > kMaxDimension) {
        ESP_LOGE(kTag, "Fill rejected: vertices=%p count=%u palette=%p slots=%u..%u row=%u column=%u (%s)",
                 vertices, count, palette, first_color, last_color, first_row, first_column,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    float top = vertices[0].y;
    float bottom = vertices[0].y;
    for (uint16_t index = 0; index < count; ++index) {
        if (!valid_point(vertices[index])) {
            ESP_LOGE(kTag, "Fill point %u rejected: %f,%f (%s)", index,
                     vertices[index].x, vertices[index].y, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        top = std::min(top, vertices[index].y);
        bottom = std::max(bottom, vertices[index].y);
    }
    if (opacity == 0) {
        return ESP_OK;
    }
    float intersections[kMaxPathVertices];
    for (uint16_t row = 0; row < strip.height; ++row) {
        const float scan_y = first_row + row + .5f;
        if (scan_y < top || scan_y >= bottom) {
            continue;
        }
        uint16_t hits = 0;
        for (uint16_t index = 0; index < count; ++index) {
            const Vec2 a = vertices[index];
            const Vec2 b = vertices[(index + 1) % count];
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
            const int left = std::max(static_cast<int>(first_column),
                                     static_cast<int>(std::ceil(intersections[hit] - .5f)));
            const int right = std::min(static_cast<int>(first_column + strip.width),
                                      static_cast<int>(std::ceil(intersections[hit + 1] - .5f)));
            if (left >= right) {
                continue;
            }
            if (opacity == 255) {
                // The common opaque span needs neither a destination read nor
                // a per-pixel palette lookup/coverage branch.
                std::fill(strip.pixels + row * strip.stride + (left - first_column),
                          strip.pixels + row * strip.stride + (right - first_column), color);
            } else {
                for (int column = left; column < right; ++column) {
                    auto &destination = strip.pixels[row * strip.stride + column - first_column];
                    destination = blend_rgb565(color, destination, opacity);
                }
            }
        }
    }
    return ESP_OK;
}

// from: Validated segment start in canvas coordinates.
// to: Validated segment end in canvas coordinates.
// width: Validated positive stroke diameter.
// color: Native RGB565 stroke color.
PreparedStroke::PreparedStroke(Vec2 from, Vec2 to, float width, uint16_t color)
{
    valid_ = true;
    from_ = from;
    color_ = color;
    const float radius = width * .5f;
    loose_left_ = std::min(from.x, to.x) - radius;
    loose_right_ = std::max(from.x, to.x) + radius;
    loose_top_ = std::min(from.y, to.y) - radius;
    loose_bottom_ = std::max(from.y, to.y) + radius;
    raster_left_ = static_cast<int>(std::floor(loose_left_));
    raster_right_ = static_cast<int>(std::ceil(loose_right_));
    raster_top_ = static_cast<int>(std::floor(loose_top_));
    raster_bottom_ = static_cast<int>(std::ceil(loose_bottom_));
    dx_ = to.x - from.x;
    dy_ = to.y - from.y;
    squared_length_ = dx_*dx_ + dy_*dy_;
    squared_radius_ = radius * radius;
    // Use the whole supported canvas for this error bound, so the tile shape
    // cannot change which rounding-sensitive pixels take the exact predicate.
    const float max_px = std::max(std::abs(.5f - from.x), std::abs(kMaxDimension - .5f - from.x));
    const float max_py = std::max(std::abs(.5f - from.y), std::abs(kMaxDimension - .5f - from.y));
    const float displacement_error =
        (max_px + max_py + std::abs(dx_) + std::abs(dy_)) *
        (16.f * std::numeric_limits<float>::epsilon());
    cross_threshold_ = squared_radius_ * squared_length_;
    cross_guard_ = squared_length_ * displacement_error *
        (2.f * radius + displacement_error) +
        cross_threshold_ * (8.f * std::numeric_limits<float>::epsilon());
    bound_scanline_ = std::abs(dy_) > .001f;
    band_half_width_ = bound_scanline_ ?
        radius * std::sqrt(squared_length_) / std::abs(dy_) : 0.f;
    slope_ = bound_scanline_ ? dx_ / dy_ : 0.f;
}

// from: Start of the line in canvas coordinates.
// to: End of the line, equal to from for a round point marker.
// width: Round stroke diameter in pixels.
// color: Native RGB565 fill.
// prepared: Receives a complete value only after every argument is accepted.
esp_err_t prepare_stroke(Vec2 from, Vec2 to, float width, uint16_t color,
                         PreparedStroke &prepared)
{
    if (!valid_point(from) || !valid_point(to) || !std::isfinite(width) ||
        width <= 0 || width > kMaxStrokeWidth) {
        ESP_LOGE(kTag, "Stroke rejected: from=%f,%f to=%f,%f width=%f (%s)",
                 from.x, from.y, to.x, to.y, width,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }

    prepared = PreparedStroke(from, to, width, color);
    return ESP_OK;
}

// strip: Validated writable destination tile.
// first_row: Validated canvas row represented by the tile.
// first_column: Validated canvas column represented by the tile.
esp_err_t PreparedStroke::raster_rows(
    Surface strip, uint16_t first_row, uint16_t first_column
) const
{
    // Most debug links miss any particular tile. These loose bounds are
    // deliberately wider than the pixel-center coverage.
    if (loose_bottom_ < first_row ||
        loose_top_ > first_row + strip.height ||
        loose_right_ < first_column ||
        loose_left_ > first_column + strip.width) {
        return ESP_OK;
    }
    const int left = std::max(static_cast<int>(first_column),
                             raster_left_);
    const int right = std::min(static_cast<int>(first_column + strip.width) - 1,
                              raster_right_);
    const int top = std::max(static_cast<int>(first_row),
                            raster_top_);
    const int bottom = std::min(static_cast<int>(first_row + strip.height) - 1,
                               raster_bottom_);
    if (left > right || top > bottom) {
        return ESP_OK;
    }
    // For interior pixels, compare the squared cross product with r² * |d|².
    // This avoids software division and closest-point construction. A small
    // guard repeats the original projection at rounding-sensitive boundaries.
    // Endpoint caps still use the original direct squared-distance expression.
    // The old box visits every pixel between diagonal endpoints. Bound each
    // scanline by the infinite stroke band instead; the original segment test
    // below still supplies the exact round caps and pixel coverage.
    for (int row = top; row <= bottom; ++row) {
        int scan_left = left;
        int scan_right = right;
        if (bound_scanline_) {
            const float center = from_.x +
                (row + .5f - from_.y) * slope_;
            // One extra pixel conservatively covers floating-point rounding.
            // Clip floats before conversion: nearly horizontal lines can have
            // a very wide band even though their canvas bounds remain small.
            const float band_margin = 1.f + (std::abs(center) + band_half_width_) *
                (4.f * std::numeric_limits<float>::epsilon());
            const float band_left = center - band_half_width_ - band_margin;
            const float band_right = center + band_half_width_ + band_margin;
            if (band_right < left || band_left > right) {
                continue;
            }
            scan_left = static_cast<int>(std::floor(std::max(static_cast<float>(left), band_left)));
            scan_right = static_cast<int>(std::ceil(std::min(static_cast<float>(right), band_right)));
        }
        for (int column = scan_left; column <= scan_right; ++column) {
            const float px = column + .5f - from_.x;
            const float py = row + .5f - from_.y;
            const float projection = px*dx_ + py*dy_;
            bool covered = false;
            if (squared_length_ == 0 || projection <= 0) {
                covered = px*px + py*py <= squared_radius_;
            } else if (projection >= squared_length_) {
                const float ex = px - dx_;
                const float ey = py - dy_;
                covered = ex*ex + ey*ey <= squared_radius_;
            } else {
                const float cross = px*dy_ - py*dx_;
                const float squared_cross = cross * cross;
                covered = squared_cross <= cross_threshold_;
                if (std::abs(squared_cross - cross_threshold_) <= cross_guard_) {
                    const float exact_position = std::max(
                        0.f, std::min(1.f, projection / squared_length_)
                    );
                    const float exact_x = px - exact_position * dx_;
                    const float exact_y = py - exact_position * dy_;
                    covered = exact_x*exact_x + exact_y*exact_y <= squared_radius_;
                }
            }
            if (covered) {
                strip.pixels[(row - first_row)*strip.stride + column - first_column] = color_;
            }
        }
    }
    return ESP_OK;
}

// strip: Valid writable tile, checked before any raster work.
// first_row: Tile origin within the full logical canvas.
// prepared: Valid immutable stroke produced by prepare_stroke.
// first_column: Canvas x origin; coverage remains in unchanged absolute coordinates.
esp_err_t stroke_prepared_rows(Surface strip, uint16_t first_row,
                               const PreparedStroke &prepared,
                               uint16_t first_column)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    if (!prepared.valid_ || first_row + strip.height > kMaxDimension ||
        first_column + strip.width > kMaxDimension) {
        ESP_LOGE(kTag, "Prepared stroke rejected: valid=%u row=%u column=%u (%s)",
                 prepared.valid_, first_row, first_column,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return prepared.raster_rows(strip, first_row, first_column);
}

// strip: Valid writable tile, checked once for the complete ordered batch.
// first_row: Tile origin within the full logical canvas.
// prepared: Readable prepared strokes in paint order.
// count: Number of strokes; zero is accepted without dereferencing prepared.
// first_column: Canvas x origin; coverage remains in unchanged absolute coordinates.
esp_err_t stroke_prepared_batch_rows(Surface strip, uint16_t first_row,
                                     const PreparedStroke *prepared, uint16_t count,
                                     uint16_t first_column)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    if ((count > 0 && prepared == nullptr) ||
        first_row + strip.height > kMaxDimension ||
        first_column + strip.width > kMaxDimension) {
        ESP_LOGE(kTag, "Prepared stroke batch rejected: strokes=%p count=%u row=%u column=%u (%s)",
                 prepared, count, first_row, first_column,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // Reject the whole batch before painting, so an invalid entry cannot leave
    // a partially updated layer behind.
    for (uint16_t index = 0; index < count; ++index) {
        if (!prepared[index].valid_) {
            ESP_LOGE(kTag, "Prepared stroke batch entry %u rejected (%s)",
                     index, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (uint16_t index = 0; index < count; ++index) {
        const esp_err_t raster_error = prepared[index].raster_rows(
            strip, first_row, first_column
        );
        if (raster_error != ESP_OK) {
            return raster_error;
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
// first_column: Canvas x origin; coverage remains in unchanged absolute coordinates.
esp_err_t stroke_rows(Surface strip, uint16_t first_row,
                      Vec2 from, Vec2 to, float width, uint16_t color,
                      uint16_t first_column)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    // Keep the original combined rejection and log for existing callers.
    if (!valid_point(from) || !valid_point(to) || !std::isfinite(width) ||
        width <= 0 || width > kMaxStrokeWidth || first_row + strip.height > kMaxDimension ||
        first_column + strip.width > kMaxDimension) {
        ESP_LOGE(kTag, "Stroke rejected: from=%f,%f to=%f,%f width=%f row=%u column=%u (%s)",
                 from.x, from.y, to.x, to.y, width, first_row, first_column,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }

    const float radius = width * .5f;
    // Preserve the old hot miss: a stroke outside this tile performs no
    // square root, division or integer bound conversion.
    if (std::max(from.y, to.y) + radius < first_row ||
        std::min(from.y, to.y) - radius > first_row + strip.height ||
        std::max(from.x, to.x) + radius < first_column ||
        std::min(from.x, to.x) - radius > first_column + strip.width) {
        return ESP_OK;
    }
    const PreparedStroke prepared(from, to, width, color);
    return prepared.raster_rows(strip, first_row, first_column);
}
}  // namespace CODY2D
