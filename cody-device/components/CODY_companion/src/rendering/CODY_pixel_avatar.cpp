/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Cody display adapter for the Muse Gadget SDK avatar.
 */

#include "CODY_pixel_avatar.hpp"

#include <algorithm>
#include <cstddef>

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
// Exception: Logs rejected composition input and clears the frame.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms, float audio_level, bool working)
{
    const auto status = compose_muse_frame(frame, {pose, time_ms, pose_ms, reaction_ms, audio_level, working});
    if (status == PixelAvatarComposeStatus::Ok) {
        return;
    }
    if (status == PixelAvatarComposeStatus::InvalidPose) {
        ESP_LOGE(kTag, "Compose rejected pose=%u (%s)",
            static_cast<unsigned>(pose), esp_err_to_name(ESP_ERR_INVALID_ARG));
    } else {
        ESP_LOGE(kTag, "Compose rejected non-finite audio level (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
    }
    frame.cells.fill(0);
    frame.palette.fill(0);
    frame.dim_palette.fill(0);
}

// previous: Borrowed previous frame; includes its own palette.
// current: Borrowed newly composed frame.
// viewport: Integer-scaled logical square, independent of display orientation.
// Exception: Invalid viewport returns an empty rectangle after logging its values.
CODY2D::Rect pixel_avatar_damage(const PixelAvatarFrame &previous,
    const PixelAvatarFrame &current, CODY2D::Rect viewport)
{
    if (validate_viewport(viewport) != ESP_OK) {
        return {};
    }
    int left = kGridSize;
    int top = kGridSize;
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            const size_t index = static_cast<size_t>(y * kGridSize + x);
            const auto before = previous.cells[index];
            const auto after = current.cells[index];
            const bool grid = viewport.width >= 3 * kGridSize;
            if (previous.palette[before] != current.palette[after] ||
                (grid && previous.dim_palette[before] != current.dim_palette[after])) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
    }
    if (left == kGridSize) {
        return {};
    }
    const uint32_t scale = viewport.width / kGridSize;
    return {
        static_cast<uint16_t>(viewport.x + static_cast<uint32_t>(left) * scale),
        static_cast<uint16_t>(viewport.y + static_cast<uint32_t>(top) * scale),
        static_cast<uint16_t>(static_cast<uint32_t>(right - left) * scale),
        static_cast<uint16_t>(static_cast<uint32_t>(bottom - top) * scale),
    };
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
    for (uint32_t y = top; y < bottom; ++y) {
        const uint32_t row = y - viewport.y;
        const bool row_edge = grid && row % scale == scale - 1;
        const auto *source = frame.cells.data() + (row / scale) * kGridSize;
        uint16_t *destination = strip.pixels + (y - first_row) * strip.stride;
        for (uint32_t x = left; x < right; ++x) {
            const uint32_t column = x - viewport.x;
            // Same last-pixel shading rule as the upstream muse_pixel_scale.
            const bool edge = row_edge || (grid && column % scale == scale - 1);
            const auto &palette = edge ? frame.dim_palette : frame.palette;
            destination[x - first_column] = CODY2D::rgb565_native_to_stored(
                palette[source[column / scale]], strip.byte_order);
        }
    }
    return ESP_OK;
}

}  // namespace CODY
