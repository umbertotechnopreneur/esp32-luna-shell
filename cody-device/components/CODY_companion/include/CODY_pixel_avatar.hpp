/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Cody display adapter for the Muse Gadget SDK avatar.
 */

#pragma once

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
// Exception: An invalid pose or non-finite audio level logs the cause and clears the frame.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms,
    float audio_level = 0.0f, bool working = false);

// previous: Caller-owned previous indexed frame; never copied or modified.
// current: Caller-owned newly composed indexed frame.
// viewport: Square logical bounds, scaled by an integer multiple of 64.
// Exception: Invalid bounds are logged and return an empty rectangle.
// An unchanged frame also returns an empty rectangle; removed cells count as damage.
CODY2D::Rect pixel_avatar_damage(const PixelAvatarFrame &previous,
    const PixelAvatarFrame &current, CODY2D::Rect viewport);

// frame: Caller-owned indexed Muse artwork and both RGB565 palettes.
// viewport: Square logical bounds, scaled by an integer multiple of 64.
// strip: Borrowed writable tile with its original capacity, stride and byte order.
// first_row: Logical canvas row corresponding to strip row zero.
// first_column: Logical canvas column corresponding to strip column zero.
// Exception: Returns the original surface error, or invalid size before writes.
esp_err_t draw_pixel_avatar_rows(const PixelAvatarFrame &frame,
    CODY2D::Rect viewport, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column);

}  // namespace CODY
