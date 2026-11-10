/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Cody display adapter for the Muse Gadget SDK avatar.
 */

#pragma once

#include <cstddef>

#include "CODY2D.hpp"
#include "CODY_muse_frame.hpp"

namespace CODY {

// frame: Caller-owned 64 by 64 indexed output, replaced completely.
// pose: Current semantic state, retained during a touch reaction.
// time_ms: Active animation time, used only for artistic motion.
// pose_ms: Elapsed time in the current semantic state.
// reaction_ms: Elapsed touch-reaction time; 1600 or greater means inactive.
// audio_level: Normalized energy supplied by the audio owner when available.
// working: Whether Thinking represents active work.
// style: Validated pose, particle, symbol and transition parameters.
// Exception: Invalid pose, audio or visual settings log the cause and clear the frame.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms,
    float audio_level = 0.0f, bool working = false,
    MuseVisualStyle style = {});

// One scratch span per grid row, reduced to a bounded transfer plan.
struct PixelAvatarDamage {
    std::array<CODY2D::Rect, kPixelAvatarGridSize> rectangles{};
    std::size_t count = 0;
};

// previous: Indexed frame from the last completed display transaction.
// current: Newly composed indexed frame.
// viewport: Square logical bounds at least 64 pixels wide.
// damage: Display-owned scratch and the resulting changed rectangles.
// Exception: Logs invalid viewport geometry and leaves the plan empty.
void plan_pixel_avatar_damage(const PixelAvatarFrame &previous,
    const PixelAvatarFrame &current, CODY2D::Rect viewport, PixelAvatarDamage &damage);

// frame: Caller-owned indexed Muse artwork and both RGB565 palettes.
// viewport: Square logical bounds at least 64 pixels wide; fractional scale uses nearest-neighbour.
// strip: Borrowed writable tile with its original capacity, stride and byte order.
// first_row: Logical canvas row corresponding to strip row zero.
// first_column: Logical canvas column corresponding to strip column zero.
// Exception: Returns the original surface error, or invalid size before writes.
esp_err_t draw_pixel_avatar_rows(const PixelAvatarFrame &frame,
    CODY2D::Rect viewport, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column);

}  // namespace CODY
