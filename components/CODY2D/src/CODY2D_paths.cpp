#include "CODY2D_paths.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_paths";
constexpr uint16_t kCoverageSamples = 4;
constexpr uint16_t kHorizontalCoverageUnits = 256;
constexpr uint16_t kFullCoverage = kCoverageSamples * kHorizontalCoverageUnits;
constexpr uint16_t kCoverageWordBits = 32;
constexpr uint16_t kCoverageEventWords = kMaxDimension / kCoverageWordBits + 1;
static_assert(kMaxPathVertices <= std::numeric_limits<uint8_t>::max() + 1U,
              "Fill edge indices no longer fit in the compact shortlist");
static_assert(kFullCoverage <= std::numeric_limits<int16_t>::max(),
              "Coverage differences no longer fit in the bounded row scratch");

// point: Canvas coordinate whose bounded extent makes integer raster conversion safe.
bool valid_point(Vec2 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::abs(point.x) <= 2048 && std::abs(point.y) <= 2048;
}

// differences: Bounded row scratch, initialized only where an event is first touched.
// events: Zero-initialized bitset recording which coverage changes are readable this row.
// column: Tile-local change position, including the one-past-width sentinel.
// change: Signed coverage contribution to accumulate at this position.
void add_coverage_change(int16_t *differences, uint32_t *events, int column, int change)
{
    if (change == 0) {
        return;
    }
    const uint32_t mask = uint32_t{1} << (column % kCoverageWordBits);
    uint32_t &word = events[column / kCoverageWordBits];
    if ((word & mask) == 0) {
        // The bitset owns initialization, so untouched scratch is never read.
        differences[column] = change;
        word |= mask;
    } else {
        differences[column] += change;
    }
}

// events: Readable row bitset of touched coverage changes, including its terminal sentinel.
// first: First tile-local change position to consider.
// limit: Exclusive search limit, returned when no earlier event exists.
int next_coverage_event(const uint32_t *events, int first, int limit)
{
    if (first >= limit) {
        return limit;
    }
    int word_index = first / kCoverageWordBits;
    const int bit_offset = first % kCoverageWordBits;
    uint32_t remaining = events[word_index] &
        (std::numeric_limits<uint32_t>::max() << bit_offset);
    while (remaining == 0) {
        ++word_index;
        if (word_index * kCoverageWordBits >= limit) {
            return limit;
        }
        remaining = events[word_index];
    }

    // Find the first set bit with bounded integer steps on both host and ESP32.
    // Long opaque spans skip entire words instead of inspecting every pixel.
    int offset = 0;
    if ((remaining & 0xffffU) == 0) {
        remaining >>= 16;
        offset += 16;
    }
    if ((remaining & 0xffU) == 0) {
        remaining >>= 8;
        offset += 8;
    }
    if ((remaining & 0xfU) == 0) {
        remaining >>= 4;
        offset += 4;
    }
    if ((remaining & 0x3U) == 0) {
        remaining >>= 2;
        offset += 2;
    }
    if ((remaining & 0x1U) == 0) {
        ++offset;
    }
    return std::min(limit, word_index * kCoverageWordBits + offset);
}

// differences: Bounded coverage changes whose initialized positions are recorded by events.
// events: Row bitset receiving every touched coverage boundary.
// left: Clipped tile-local span start in fixed 1/256-pixel units.
// right: Clipped tile-local exclusive span end in the same fixed units, greater than left.
void add_coverage_span(int16_t *differences, uint32_t *events, int left, int right)
{
    const int first_pixel = left / kHorizontalCoverageUnits;
    const int last_pixel = (right - 1) / kHorizontalCoverageUnits;
    if (first_pixel == last_pixel) {
        const int length = right - left;
        add_coverage_change(differences, events, first_pixel, length);
        add_coverage_change(differences, events, first_pixel + 1, -length);
        return;
    }

    const int first_coverage = kHorizontalCoverageUnits - left % kHorizontalCoverageUnits;
    const int last_coverage = right - last_pixel * kHorizontalCoverageUnits;
    // Only four changes describe an arbitrarily long span. The prefix sum
    // later combines all four subscanlines before any destination is blended.
    add_coverage_change(differences, events, first_pixel, first_coverage);
    add_coverage_change(differences, events, first_pixel + 1,
                        kHorizontalCoverageUnits - first_coverage);
    add_coverage_change(differences, events, last_pixel, last_coverage - kHorizontalCoverageUnits);
    add_coverage_change(differences, events, last_pixel + 1, -last_coverage);
}

// strip: Validated writable RGB565 tile with explicit byte order and no padding writes.
// first_row: Absolute canvas y origin shared by every tile of this polygon.
// vertices: Validated readable polygon coordinates.
// count: Validated polygon size bounded by kMaxPathVertices.
// palette: Validated readable native RGB565 color table.
// first_color: First slot of the ordered gradient, or the solid color slot.
// last_color: Last slot of the ordered gradient.
// opacity: Nonzero source opacity, combined with geometric coverage once per pixel.
// first_column: Absolute canvas x origin shared by every tile of this polygon.
// top: Minimum polygon y used for the common gradient origin.
// bottom: Maximum polygon y used for the common gradient extent.
esp_err_t fill_coverage_rows(Surface strip, uint16_t first_row,
                             const Vec2 *vertices, uint16_t count,
                             const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                             uint8_t opacity, uint16_t first_column, float top, float bottom)
{
    if (bottom <= first_row || top >= first_row + strip.height || top == bottom) {
        return ESP_OK;
    }

    // The shortlist covers the complete subscanline domain, including caps
    // which do not cross the old center sample at y + 0.5.
    constexpr float first_sample_offset = .5f / kCoverageSamples;
    const float first_sample_y = first_row + first_sample_offset;
    const float last_sample_y = first_row + strip.height - first_sample_offset;
    uint8_t candidate_edges[kMaxPathVertices];
    uint16_t candidate_edge_count = 0;
    for (uint16_t index = 0; index < count; ++index) {
        const uint16_t next_index = index + 1 < count ? index + 1 : 0;
        const Vec2 a = vertices[index];
        const Vec2 b = vertices[next_index];
        const float edge_top = std::min(a.y, b.y);
        const float edge_bottom = std::max(a.y, b.y);
        if (edge_top <= last_sample_y && edge_bottom > first_sample_y) {
            candidate_edges[candidate_edge_count++] = static_cast<uint8_t>(index);
        }
    }

    // One bounded row and an intersection list are enough; no frame-sized
    // mask, runtime allocation, or shared mutable scratch is needed.
    int16_t coverage_differences[kMaxDimension + 1];
    uint32_t coverage_events[kCoverageEventWords];
    float intersections[kMaxPathVertices];
    uint8_t row_candidate_edges[kMaxPathVertices];
    const float tile_right = first_column + strip.width;
    const bool is_solid_color = first_color == last_color;
    const uint16_t solid_color = palette[first_color];
    for (uint16_t row = 0; row < strip.height; ++row) {
        const uint16_t canvas_row = first_row + row;
        if (canvas_row + 1 <= top || canvas_row >= bottom) {
            continue;
        }
        // A tile can contain many contour edges, but only a few can cross any
        // of this row's four samples. Reject the others once, preserving the
        // original edge order and the exact intersection arithmetic below.
        const float row_first_sample_y = canvas_row + first_sample_offset;
        const float row_last_sample_y = canvas_row + 1 - first_sample_offset;
        uint16_t row_candidate_count = 0;
        for (uint16_t candidate = 0; candidate < candidate_edge_count; ++candidate) {
            const uint16_t index = candidate_edges[candidate];
            const uint16_t next_index = index + 1 < count ? index + 1 : 0;
            const Vec2 a = vertices[index];
            const Vec2 b = vertices[next_index];
            const float edge_top = std::min(a.y, b.y);
            const float edge_bottom = std::max(a.y, b.y);
            if (edge_top <= row_last_sample_y && edge_bottom > row_first_sample_y) {
                row_candidate_edges[row_candidate_count++] = static_cast<uint8_t>(index);
            }
        }
        if (row_candidate_count == 0) {
            continue;
        }
        const uint16_t event_words = strip.width / kCoverageWordBits + 1;
        std::fill(coverage_events, coverage_events + event_words, uint32_t{0});
        int first_covered_pixel = strip.width;
        int past_last_covered_pixel = 0;
        for (uint16_t sample = 0; sample < kCoverageSamples; ++sample) {
            const float scan_y = canvas_row + (sample + .5f) / kCoverageSamples;
            uint16_t hits = 0;
            for (uint16_t candidate = 0; candidate < row_candidate_count; ++candidate) {
                const uint16_t index = row_candidate_edges[candidate];
                const uint16_t next_index = index + 1 < count ? index + 1 : 0;
                const Vec2 a = vertices[index];
                const Vec2 b = vertices[next_index];
                if ((a.y <= scan_y && b.y > scan_y) || (b.y <= scan_y && a.y > scan_y)) {
                    intersections[hits++] = a.x + (scan_y - a.y) * (b.x - a.x) / (b.y - a.y);
                }
            }
            if (hits % 2 != 0) {
                ESP_LOGE(kTag, "Coverage fill row=%u sample=%u has odd intersection count %u (%s)",
                         canvas_row, sample, hits, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                return ESP_ERR_INVALID_RESPONSE;
            }
            std::sort(intersections, intersections + hits);
            for (uint16_t hit = 0; hit < hits; hit += 2) {
                const float left = std::max(static_cast<float>(first_column), intersections[hit]);
                const float right = std::min(tile_right, intersections[hit + 1]);
                if (left >= right) {
                    continue;
                }
                // Quantize absolute positions before removing the tile origin.
                // This keeps horizontal coverage identical across tile seams.
                // Clipping makes both values nonnegative and bounded; integer
                // truncation is exactly floor here, without a libm call.
                const int fixed_origin = first_column * kHorizontalCoverageUnits;
                const int fixed_left = static_cast<int>(left * kHorizontalCoverageUnits + .5f) -
                    fixed_origin;
                const int fixed_right = static_cast<int>(right * kHorizontalCoverageUnits + .5f) -
                    fixed_origin;
                if (fixed_left == fixed_right) {
                    continue;
                }
                add_coverage_span(coverage_differences, coverage_events, fixed_left, fixed_right);
                first_covered_pixel = std::min(
                    first_covered_pixel, fixed_left / kHorizontalCoverageUnits
                );
                past_last_covered_pixel = std::max(
                    past_last_covered_pixel,
                    (fixed_right + kHorizontalCoverageUnits - 1) / kHorizontalCoverageUnits
                );
            }
        }

        uint16_t color = solid_color;
        if (!is_solid_color) {
            // A partially covered cap can have its center outside the polygon.
            // Clamp only this raster lookup domain, never authored settings.
            const float ratio = std::max(0.f, std::min(
                1.f, (canvas_row + .5f - top) / (bottom - top)
            ));
            const uint16_t slot = first_color + static_cast<uint16_t>(
                ratio * (last_color - first_color) + .5f
            );
            color = palette[slot];
        }

        const uint16_t stored_color = rgb565_native_to_stored(color, strip.byte_order);
        int coverage = 0;
        int column = first_covered_pixel;
        while (column < past_last_covered_pixel) {
            coverage += coverage_differences[column];
            if (coverage < 0 || coverage > kFullCoverage) {
                ESP_LOGE(kTag, "Coverage fill row=%u column=%u rejected coverage=%d (%s)",
                         canvas_row, first_column + column, coverage,
                         esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                return ESP_ERR_INVALID_RESPONSE;
            }
            const int past_run = next_coverage_event(
                coverage_events, column + 1, past_last_covered_pixel
            );
            const uint8_t effective_opacity = static_cast<uint8_t>(
                (static_cast<uint32_t>(opacity) * coverage + kFullCoverage / 2) / kFullCoverage
            );
            if (effective_opacity != 0) {
                uint16_t *destination = strip.pixels + row * strip.stride + column;
                uint16_t *past_destination = strip.pixels + row * strip.stride + past_run;
                if (effective_opacity == 255) {
                    std::fill(destination, past_destination, stored_color);
                } else if (strip.byte_order == Rgb565ByteOrder::Native) {
                    while (destination != past_destination) {
                        *destination = blend_rgb565(color, *destination, effective_opacity);
                        ++destination;
                    }
                } else {
                    // Coverage still blends native channels; only storage is in panel order.
                    while (destination != past_destination) {
                        const uint16_t native_destination = rgb565_stored_to_native(
                            *destination, strip.byte_order
                        );
                        const uint16_t blended_color = blend_rgb565(
                            color, native_destination, effective_opacity
                        );
                        *destination = rgb565_native_to_stored(blended_color, strip.byte_order);
                        ++destination;
                    }
                }
            }
            column = past_run;
        }
    }
    return ESP_OK;
}

// strip: Validated caller-owned destination; no row padding is touched.
// first_row: Canvas origin of this tile, making gradient and clipping strip-independent.
// vertices: Validated readable evaluated polygon.
// count: Validated vertex count, including an optional repeated closure.
// palette: Readable table of 256 native RGB565 entries.
// first_color: Top slot, also the solid slot for a constant-color polygon.
// last_color: Bottom slot in the same ordered ramp.
// opacity: Nonzero source opacity, composited using the original pixel-center rule.
// first_column: Canvas x origin; only destination indexing uses tile-local coordinates.
// top: Minimum polygon y used for the original gradient origin.
// bottom: Maximum polygon y used for the original gradient extent.
esp_err_t fill_pixel_center_rows(Surface strip, uint16_t first_row,
                                 const Vec2 *vertices, uint16_t count,
                                 const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                                 uint8_t opacity, uint16_t first_column, float top, float bottom)
{
    // Keep the original edge order, but do not revisit edges that cannot cross
    // any pixel-center scanline in this tile. The row loop still applies the
    // original crossing predicate, so this shortlist is only a conservative
    // rejection and cannot change polygon coverage.
    const float tile_first_scan_y = first_row + .5f;
    const float tile_last_scan_y = first_row + strip.height - .5f;
    uint8_t candidate_edges[kMaxPathVertices];
    uint16_t candidate_edge_count = 0;
    for (uint16_t index = 0; index < count; ++index) {
        const Vec2 a = vertices[index];
        const Vec2 b = vertices[(index + 1) % count];
        const float edge_top = std::min(a.y, b.y);
        const float edge_bottom = std::max(a.y, b.y);
        if (edge_top <= tile_last_scan_y && edge_bottom > tile_first_scan_y) {
            candidate_edges[candidate_edge_count++] = static_cast<uint8_t>(index);
        }
    }

    const bool is_solid_color = first_color == last_color;
    const uint16_t solid_color = palette[first_color];
    const uint16_t solid_stored_color = rgb565_native_to_stored(
        solid_color, strip.byte_order
    );
    float intersections[kMaxPathVertices];
    for (uint16_t row = 0; row < strip.height; ++row) {
        const float scan_y = first_row + row + .5f;
        if (scan_y < top || scan_y >= bottom) {
            continue;
        }
        uint16_t hits = 0;
        for (uint16_t candidate = 0; candidate < candidate_edge_count; ++candidate) {
            const uint16_t index = candidate_edges[candidate];
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
        uint16_t color = solid_color;
        uint16_t stored_color = solid_stored_color;
        if (!is_solid_color) {
            const float ratio = (scan_y - top) / (bottom - top);
            const uint16_t slot = first_color + static_cast<uint16_t>(
                std::floor(ratio * (last_color - first_color) + .5f));
            color = palette[slot];
            stored_color = rgb565_native_to_stored(color, strip.byte_order);
        }
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
                          strip.pixels + row * strip.stride + (right - first_column),
                          stored_color);
            } else if (strip.byte_order == Rgb565ByteOrder::Native) {
                // Preserve the native hot path and its exact blend expression.
                for (int column = left; column < right; ++column) {
                    auto &destination = strip.pixels[row * strip.stride + column - first_column];
                    destination = blend_rgb565(color, destination, opacity);
                }
            } else {
                for (int column = left; column < right; ++column) {
                    auto &destination = strip.pixels[row * strip.stride + column - first_column];
                    const uint16_t native_destination = rgb565_stored_to_native(
                        destination, strip.byte_order
                    );
                    const uint16_t native_blended = blend_rgb565(
                        color, native_destination, opacity
                    );
                    destination = rgb565_native_to_stored(
                        native_blended, strip.byte_order
                    );
                }
            }
        }
    }
    return ESP_OK;
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
// antialiasing: Explicit boundary sampling policy; Disabled preserves the original raster output.
esp_err_t fill_path_rows(Surface strip, uint16_t first_row,
                        const Vec2 *vertices, uint16_t count,
                        const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                        uint8_t opacity, uint16_t first_column, FillAntialiasing antialiasing)
{
    const esp_err_t error = validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    if (!vertices || !palette || count < 3 || count > kMaxPathVertices ||
        first_color > last_color || first_row + strip.height > kMaxDimension ||
        first_column + strip.width > kMaxDimension ||
        (antialiasing != FillAntialiasing::Disabled && antialiasing != FillAntialiasing::Coverage)) {
        ESP_LOGE(kTag, "Fill rejected: vertices=%p count=%u palette=%p slots=%u..%u row=%u column=%u aa=%u (%s)",
                 vertices, count, palette, first_color, last_color, first_row, first_column,
                 static_cast<unsigned int>(antialiasing),
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
    // Each sampling policy owns its scratch in a separate raster routine;
    // coverage does not need the pixel-center intersection storage as well.
    if (antialiasing == FillAntialiasing::Coverage) {
        return fill_coverage_rows(strip, first_row, vertices, count, palette,
                                  first_color, last_color, opacity, first_column, top, bottom);
    }
    return fill_pixel_center_rows(strip, first_row, vertices, count, palette,
                                  first_color, last_color, opacity, first_column, top, bottom);
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
    const uint16_t stored_color = rgb565_native_to_stored(color_, strip.byte_order);
    if (squared_length_ == 0) {
        // Point markers always used this same circle predicate, but the
        // generic loop also calculated a zero projection for every pixel.
        // Keep its pixel decisions identical and leave capsule arithmetic to
        // the nonzero strokes that actually need it.
        for (int row = top; row <= bottom; ++row) {
            const float py = row + .5f - from_.y;
            for (int column = left; column <= right; ++column) {
                const float px = column + .5f - from_.x;
                if (px*px + py*py <= squared_radius_) {
                    strip.pixels[(row - first_row)*strip.stride + column - first_column] =
                        stored_color;
                }
            }
        }
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
            if (projection <= 0) {
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
                strip.pixels[(row - first_row)*strip.stride + column - first_column] = stored_color;
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
