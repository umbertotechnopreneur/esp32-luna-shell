/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Portable indexed frame composition for the Muse avatar.
 */

#include "CODY_muse_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

extern "C" {
#include "muse_pixel.h"
}

namespace CODY {
namespace {

static_assert(kPixelAvatarGridSize == MUSE_PX_W && kPixelAvatarGridSize == MUSE_PX_H);
static_assert(PixelAvatarFrame{}.palette.size() == MUSE_PX_FRAME_PALETTE);

constexpr std::array<muse_mode_t, 7> kMuseModes{
    MUSE_MODE_BOOT, MUSE_MODE_IDLE, MUSE_MODE_LISTENING, MUSE_MODE_THINKING,
    MUSE_MODE_SPEAKING, MUSE_MODE_ERROR, MUSE_MODE_OFF,
};

}  // namespace

// frame: Caller-owned output, independent of the display transfer buffer.
// input: Pose, active/pose/reaction clocks, and optional audio energy.
// Exception: Returns the specific rejected input without advancing the renderer.
PixelAvatarComposeStatus compose_muse_frame(PixelAvatarFrame &frame, PixelAvatarInput input)
{
    const auto mode = static_cast<std::size_t>(input.pose);
    if (mode >= kMuseModes.size()) {
        return PixelAvatarComposeStatus::InvalidPose;
    }
    if (!std::isfinite(input.audio_level)) {
        return PixelAvatarComposeStatus::InvalidAudioLevel;
    }

    // Same hold and final 400 ms fade as muse_state_happiness().
    constexpr uint32_t fade_ms = 400;
    const uint32_t remaining_ms = kPixelAvatarReactionMs -
        std::min(input.reaction_ms, kPixelAvatarReactionMs);
    const float happy = static_cast<float>(std::min(remaining_ms, fade_ms)) / fade_ms;
    const muse_pose_t pose{
        kMuseModes[mode], input.time_ms / 1000.0f, input.pose_ms / 1000.0f,
        std::clamp(input.audio_level, 0.0f, 1.0f), happy, input.working ? 1.0f : 0.0f,
    };
    muse_pixel_render(&pose);
    muse_pixel_copy_frame(frame.cells.data(), frame.palette.data(), frame.dim_palette.data());
    return PixelAvatarComposeStatus::Ok;
}

// seed: Artistic PRNG seed for one new serialized animation sequence.
void reset_muse_animation(uint32_t seed)
{
    muse_pixel_reset_animation(seed);
}

}  // namespace CODY
