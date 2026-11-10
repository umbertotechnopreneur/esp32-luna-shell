/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Cody display adapter for the Muse Gadget SDK avatar.
 */

#include "CODY_pixel_avatar.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_muse_avatar";
constexpr int kGridSize = kPixelAvatarGridSize;
constexpr uint32_t kCoordinateEnd = 65536;

// viewport: Integer-scaled logical bounds for the Muse grid.
// Exception: Logs rejected geometry and returns ESP_ERR_INVALID_SIZE.
esp_err_t validate_viewport(CODY2D::Rect viewport)
{
    const uint32_t right = static_cast<uint32_t>(viewport.x) + viewport.width;
    const uint32_t bottom = static_cast<uint32_t>(viewport.y) + viewport.height;
    if (viewport.width == 0 || viewport.width != viewport.height ||
        viewport.width % kGridSize != 0 || viewport.width > CODY2D::kMaxDimension ||
        right > kCoordinateEnd || bottom > kCoordinateEnd) {
        ESP_LOGE(kTag, "Viewport rejected: x=%u y=%u width=%u height=%u (%s)",
            viewport.x, viewport.y, viewport.width, viewport.height,
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

}  // namespace

// frame: Display-owned snapshot of the upstream renderer output.
// pose: Semantic presentation state.
// time_ms: Active animation clock.
// pose_ms: Time in the current state.
// reaction_ms: Time since the accepted tap.
// audio_level: Normalized energy from the caller.
// working: Active-work variant of Thinking.
// style: Independent pose, face, particle, symbol and transition settings.
// Exception: Logs rejected composition input and clears the frame.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms, float audio_level, bool working,
    MuseVisualStyle style)
{
    const auto status = compose_muse_frame(frame, {pose, time_ms, pose_ms, reaction_ms, audio_level, working, style});
    if (status == PixelAvatarComposeStatus::Ok) {
        return;
    }
    if (status == PixelAvatarComposeStatus::InvalidPose) {
        ESP_LOGE(kTag, "Compose rejected pose=%u (%s)",
            static_cast<unsigned>(pose), esp_err_to_name(ESP_ERR_INVALID_ARG));
    } else if (status == PixelAvatarComposeStatus::InvalidAudioLevel) {
        ESP_LOGE(kTag, "Compose rejected non-finite audio level (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
    } else {
        ESP_LOGE(kTag, "Compose rejected visual style: eyes=%u/%u mouth=%u mouth_open=%g particles=%u size=%u symbol=%u scale=%u (%s)",
            static_cast<unsigned>(style.face.left_eye), static_cast<unsigned>(style.face.right_eye),
            static_cast<unsigned>(style.face.mouth), static_cast<double>(style.face.talking_openness),
            static_cast<unsigned>(style.particles.count), static_cast<unsigned>(style.particles.size),
            static_cast<unsigned>(style.symbol.symbol), static_cast<unsigned>(style.symbol.scale),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
    }
    frame.cells.fill(0);
    frame.palette.fill(0);
    frame.dim_palette.fill(0);
}

// previous: Borrowed retired frame, including its own palettes.
// current: Borrowed newly composed frame.
// viewport: Integer-scaled logical square, independent of display orientation.
// damage: Display-owned row scratch, reduced to at most eight rectangles.
// Exception: Invalid viewport leaves the plan empty after logging its values.
void plan_pixel_avatar_damage(const PixelAvatarFrame &previous,
    const PixelAvatarFrame &current, CODY2D::Rect viewport, PixelAvatarDamage &damage)
{
    damage.count = 0;
    if (validate_viewport(viewport) != ESP_OK) return;

    const int scale = viewport.width / kGridSize;
    const bool grid = scale >= 3;
    for (int y = 0; y < kGridSize; ++y) {
        int left = kGridSize;
        int right = 0;
        for (int x = 0; x < kGridSize; ++x) {
            const size_t index = static_cast<size_t>(y * kGridSize + x);
            const auto before = previous.cells[index];
            const auto after = current.cells[index];
            if (previous.palette[before] != current.palette[after] ||
                (grid && previous.dim_palette[before] != current.dim_palette[after])) {
                left = std::min(left, x);
                right = x + 1;
            }
        }
        if (left < right) {
            damage.rectangles[damage.count++] = {
                static_cast<uint16_t>(viewport.x + left * scale),
                static_cast<uint16_t>(viewport.y + y * scale),
                static_cast<uint16_t>((right - left) * scale),
                static_cast<uint16_t>(scale),
            };
        }
    }

    // Like Muse, merge the cheapest neighboring spans before dispatching strips.
    constexpr size_t max_rectangles = 8;
    constexpr int extra_merge_pixels = 2048;
    while (damage.count > 1) {
        size_t best = 0;
        int best_cost = CODY2D::kMaxDimension * CODY2D::kMaxDimension + 1;
        CODY2D::Rect combined{};
        for (size_t i = 0; i + 1 < damage.count; ++i) {
            const auto &upper = damage.rectangles[i];
            const auto &lower = damage.rectangles[i + 1];
            const auto left = std::min(upper.x, lower.x);
            const int right = std::max(upper.x + upper.width, lower.x + lower.width);
            const int width = right - left;
            const int height = lower.y + lower.height - upper.y;
            const int cost = width * height - upper.width * upper.height - lower.width * lower.height;
            if (cost < best_cost) {
                best = i;
                best_cost = cost;
                combined = {left, upper.y, static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
            }
        }
        if (damage.count <= max_rectangles && best_cost > extra_merge_pixels) break;

        damage.rectangles[best] = combined;
        for (size_t i = best + 1; i + 1 < damage.count; ++i) {
            damage.rectangles[i] = damage.rectangles[i + 1];
        }
        --damage.count;
    }
}

// frame: Borrowed indexed artwork; uses the retained Muse palettes.
// viewport: Integer-scaled logical square containing the avatar.
// strip: Borrowed RGB565 tile, preserving padding and storage byte order.
// first_row: Logical row of the tile's first pixel.
// first_column: Logical column of the tile's first pixel.
// Exception: Returns original surface errors or rejected bounds.
esp_err_t draw_pixel_avatar_rows(const PixelAvatarFrame &frame,
    CODY2D::Rect viewport, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    const esp_err_t viewport_error = validate_viewport(viewport);
    if (viewport_error != ESP_OK) {
        return viewport_error;
    }
    const uint32_t tile_right = static_cast<uint32_t>(first_column) + strip.width;
    const uint32_t tile_bottom = static_cast<uint32_t>(first_row) + strip.height;
    if (tile_right > kCoordinateEnd || tile_bottom > kCoordinateEnd) {
        ESP_LOGE(kTag, "Draw rejected tile: x=%u y=%u width=%u height=%u (%s)",
            first_column, first_row, strip.width, strip.height,
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t left = std::max<uint32_t>(viewport.x, first_column);
    const uint32_t top = std::max<uint32_t>(viewport.y, first_row);
    const uint32_t right = std::min<uint32_t>(viewport.x + viewport.width, tile_right);
    const uint32_t bottom = std::min<uint32_t>(viewport.y + viewport.height, tile_bottom);
    if (left >= right || top >= bottom) {
        return ESP_OK;
    }
    const uint32_t scale = viewport.width / kGridSize;
    const bool grid = scale >= 3;
    const uint32_t first_cell = (left - viewport.x) / scale;
    const uint32_t first_cell_right = viewport.x + (first_cell + 1) * scale;
    const size_t row_bytes = (right - left) * sizeof(uint16_t);
    const uint16_t *previous = nullptr;
    uint32_t previous_cell_row = 0;
    bool previous_row_edge = false;

    for (uint32_t y = top; y < bottom; ++y) {
        const uint32_t row = y - viewport.y;
        const uint32_t cell_row = row / scale;
        const bool row_edge = grid && row + 1 == (cell_row + 1) * scale;
        uint16_t *destination = strip.pixels + (y - first_row) * strip.stride + (left - first_column);
        if (previous != nullptr && cell_row == previous_cell_row && row_edge == previous_row_edge) {
            // Reuse only the clipped pixels already expanded in this tile.
            std::memcpy(destination, previous, row_bytes);
            continue;
        }

        const auto *source = frame.cells.data() + cell_row * kGridSize;
        const auto &palette = row_edge ? frame.dim_palette : frame.palette;
        uint32_t x = left;
        uint32_t cell_right = first_cell_right;
        for (uint32_t cell = first_cell; x < right; ++cell) {
            const uint32_t end = std::min(right, cell_right);
            // As in Muse, only the last pixel of each column is dimmed on a regular row.
            const bool dim_last = grid && !row_edge && end == cell_right;
            const auto color = source[cell];
            const uint32_t count = end - x;
            std::fill_n(destination + (x - left), count - static_cast<uint32_t>(dim_last),
                CODY2D::rgb565_native_to_stored(palette[color], strip.byte_order));
            if (dim_last) {
                destination[end - left - 1] = CODY2D::rgb565_native_to_stored(
                    frame.dim_palette[color], strip.byte_order);
            }
            x = end;
            cell_right += scale;
        }
        previous = destination;
        previous_cell_row = cell_row;
        previous_row_edge = row_edge;
    }
    return ESP_OK;
}

}  // namespace CODY
