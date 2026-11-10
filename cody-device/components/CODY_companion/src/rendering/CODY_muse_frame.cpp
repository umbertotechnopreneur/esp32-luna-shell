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

static_assert(static_cast<unsigned>(MuseSymbol::Count) == MUSE_SYMBOL_COUNT);
static_assert(static_cast<unsigned>(MuseSymbol::Sleep) == MUSE_SYMBOL_SLEEP);
static_assert(static_cast<unsigned>(MuseEyeStyle::Automatic) == MUSE_EYE_AUTO);
static_assert(static_cast<unsigned>(MuseEyeStyle::Normal) == MUSE_EYE_NORMAL);
static_assert(static_cast<unsigned>(MuseEyeStyle::Wide) == MUSE_EYE_WIDE);
static_assert(static_cast<unsigned>(MuseEyeStyle::Happy) == MUSE_EYE_HAPPY);
static_assert(static_cast<unsigned>(MuseEyeStyle::Cross) == MUSE_EYE_CROSS);
static_assert(static_cast<unsigned>(MuseEyeStyle::Closed) == MUSE_EYE_CLOSED);
static_assert(static_cast<unsigned>(MuseEyeStyle::Count) == MUSE_EYE_COUNT);
static_assert(static_cast<unsigned>(MuseMouthStyle::Automatic) == MUSE_MOUTH_AUTO);
static_assert(static_cast<unsigned>(MuseMouthStyle::Smile) == MUSE_MOUTH_SMILE);
static_assert(static_cast<unsigned>(MuseMouthStyle::Round) == MUSE_MOUTH_ROUND);
static_assert(static_cast<unsigned>(MuseMouthStyle::Thoughtful) == MUSE_MOUTH_THOUGHTFUL);
static_assert(static_cast<unsigned>(MuseMouthStyle::Talking) == MUSE_MOUTH_TALKING);
static_assert(static_cast<unsigned>(MuseMouthStyle::Grin) == MUSE_MOUTH_GRIN);
static_assert(static_cast<unsigned>(MuseMouthStyle::Flat) == MUSE_MOUTH_FLAT);
static_assert(static_cast<unsigned>(MuseMouthStyle::Count) == MUSE_MOUTH_COUNT);

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

    const auto &particles = input.style.particles;
    const auto &pose_style = input.style.pose;
    const auto &face = input.style.face;
    const auto &symbol = input.style.symbol;
    const float transition = input.style.pose_transition_seconds;
    const auto bounded = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (!bounded(pose_style.motion, 0, 2) || !bounded(pose_style.breathing_speed, 0, 10) ||
        !bounded(pose_style.body_x, -8, 8) || !bounded(pose_style.body_y, -8, 8) ||
        !bounded(pose_style.left_hand_x, -8, 8) || !bounded(pose_style.left_hand_y, -8, 8) ||
        !bounded(pose_style.right_hand_x, -8, 8) || !bounded(pose_style.right_hand_y, -8, 8) ||
        !bounded(pose_style.gaze_x, -1, 1) || !bounded(pose_style.gaze_y, -1, 1) ||
        !bounded(pose_style.eye_openness, 0, 1) ||
        static_cast<unsigned>(face.left_eye) >= static_cast<unsigned>(MuseEyeStyle::Count) ||
        static_cast<unsigned>(face.right_eye) >= static_cast<unsigned>(MuseEyeStyle::Count) ||
        static_cast<unsigned>(face.mouth) >= static_cast<unsigned>(MuseMouthStyle::Count) ||
        !bounded(face.talking_openness, 0, 1) ||
        !bounded(symbol.bob_speed, 0, 10) ||
        particles.count > 16 || particles.size > 2 ||
        !std::isfinite(particles.speed) || particles.speed < 0 || particles.speed > 5 ||
        !std::isfinite(particles.radius) || particles.radius < 0 || particles.radius > 40 ||
        !std::isfinite(particles.vertical_scale) || particles.vertical_scale < 0 ||
        particles.vertical_scale > 1 ||
        !std::isfinite(particles.twinkle_speed) || particles.twinkle_speed < 0 ||
        particles.twinkle_speed > 20 ||
        !std::isfinite(particles.brightness) || particles.brightness < 0 ||
        particles.brightness > 1 || particles.color_rgb > 0xffffff ||
        static_cast<unsigned>(symbol.symbol) >= static_cast<unsigned>(MuseSymbol::Count) ||
        symbol.scale < 1 || symbol.scale > 2 || symbol.color_rgb > 0xffffff ||
        symbol.offset_x < -32 || symbol.offset_x > 32 ||
        symbol.offset_y < -32 || symbol.offset_y > 32 ||
        !std::isfinite(transition) || transition < 0 || transition > 3) {
        return PixelAvatarComposeStatus::InvalidVisualStyle;
    }
    const muse_art_t art{
        particles.count, particles.size, particles.speed, particles.radius,
        particles.vertical_scale, particles.twinkle_speed, particles.brightness,
        particles.custom_color, particles.color_rgb,
        pose_style.motion, pose_style.breathing_speed, pose_style.body_x, pose_style.body_y,
        pose_style.left_hand_x, pose_style.left_hand_y, pose_style.right_hand_x,
        pose_style.right_hand_y, pose_style.gaze_x, pose_style.gaze_y, pose_style.eye_openness,
        static_cast<muse_symbol_t>(symbol.symbol), symbol.offset_x, symbol.offset_y,
        symbol.scale, symbol.bob_speed, symbol.custom_color, symbol.color_rgb, transition,
        static_cast<muse_eye_style_t>(face.left_eye), static_cast<muse_eye_style_t>(face.right_eye),
        static_cast<muse_mouth_style_t>(face.mouth), face.talking_openness,
    };

    // Same hold and final 400 ms fade as muse_state_happiness().
    constexpr uint32_t fade_ms = 400;
    const uint32_t remaining_ms = kPixelAvatarReactionMs -
        std::min(input.reaction_ms, kPixelAvatarReactionMs);
    const float happy = static_cast<float>(std::min(remaining_ms, fade_ms)) / fade_ms;
    const muse_pose_t pose{
        kMuseModes[mode], input.time_ms / 1000.0f, input.pose_ms / 1000.0f,
        std::clamp(input.audio_level, 0.0f, 1.0f), happy, input.working ? 1.0f : 0.0f,
        &art,
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
