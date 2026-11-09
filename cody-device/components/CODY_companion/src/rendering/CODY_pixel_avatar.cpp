/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Cody display adapter for the Muse Gadget SDK avatar.
 */

#include "CODY_pixel_avatar.hpp"

#include <algorithm>
#include <cstddef>

#include "esp_log.h"

extern "C" {
#include "muse_pixel.h"
}

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_muse_avatar";
constexpr int kGridSize = 64;
constexpr uint32_t kCoordinateEnd = 65536;
static_assert(kGridSize == MUSE_PX_W && kGridSize == MUSE_PX_H);
static_assert(PixelAvatarFrame{}.palette.size() == MUSE_PX_FRAME_PALETTE);

constexpr std::array<muse_mode_t, 7> kMuseModes{
    MUSE_MODE_BOOT, MUSE_MODE_IDLE, MUSE_MODE_LISTENING, MUSE_MODE_THINKING,
    MUSE_MODE_SPEAKING, MUSE_MODE_ERROR, MUSE_MODE_OFF,
};

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
// Exception: Logs an invalid pose and clears the frame.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms, float audio_level, bool working)
{
    const auto mode = static_cast<size_t>(pose);
    if (mode >= kMuseModes.size()) {
        ESP_LOGE(kTag, "Compose rejected pose=%u (%s)",
            static_cast<unsigned>(pose), esp_err_to_name(ESP_ERR_INVALID_ARG));
        frame.cells.fill(0);
        frame.palette.fill(0);
        frame.dim_palette.fill(0);
        return;
    }

    // Same hold and final 400 ms fade as muse_state_happiness().
    constexpr uint32_t fade_ms = 400;
    const uint32_t remaining_ms = kPixelAvatarReactionMs -
        std::min(reaction_ms, kPixelAvatarReactionMs);
    const float happy = static_cast<float>(std::min(remaining_ms, fade_ms)) / fade_ms;
    const muse_pose_t muse_pose{
        kMuseModes[mode], time_ms / 1000.0f, pose_ms / 1000.0f,
        std::clamp(audio_level, 0.0f, 1.0f), happy, working ? 1.0f : 0.0f,
    };
    muse_pixel_render(&muse_pose);
    muse_pixel_copy_frame(frame.cells.data(), frame.palette.data(), frame.dim_palette.data());
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
        uint32_t cell = (left - viewport.x) / scale;
        uint32_t cell_end = viewport.x + (cell + 1) * scale;
        for (uint32_t x = left; x < right; ++cell, cell_end += scale) {
            const uint32_t run_end = std::min(right, cell_end);
            const auto index = source[cell];
            const auto &palette = row_edge ? frame.dim_palette : frame.palette;
            const uint16_t color = CODY2D::rgb565_native_to_stored(palette[index], strip.byte_order);
            std::fill_n(destination + (x - first_column), run_end - x, color);
            // Muse shades the last pixel of each large cell, even on clipped transfers.
            if (grid && !row_edge && run_end == cell_end) {
                destination[run_end - first_column - 1] = CODY2D::rgb565_native_to_stored(
                    frame.dim_palette[index], strip.byte_order);
            }
            x = run_end;
        }
    }
    return ESP_OK;
}

}  // namespace CODY
