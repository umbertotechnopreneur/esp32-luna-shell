/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Portable indexed frame composition for the Muse avatar.
 */

#include "CODY_muse_frame.hpp"
#include "CODY_muse_themes.hpp"

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

// a, b: Opaque RGB888 colors. amount: Integer proportion of b in 0..255.
// Exception: None; each channel remains bounded to one byte.
uint32_t mix_rgb(uint32_t a, uint32_t b, unsigned amount)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift <= 16; shift += 8) {
        const unsigned av = (a >> shift) & 255U, bv = (b >> shift) & 255U;
        out |= ((av * (255U - amount) + bv * amount + 127U) / 255U) << shift;
    }
    return out;
}

// color: Native RGB565 value produced by Muse's state palette.
// Exception: None; expands the existing quantized state color to RGB888.
uint32_t rgb888(uint16_t color)
{
    const unsigned r = (color >> 11U) & 31U, g = (color >> 5U) & 63U, b = color & 31U;
    return ((r * 255U / 31U) << 16U) | ((g * 255U / 63U) << 8U) | (b * 255U / 31U);
}

// color: Opaque RGB888 theme color. percent: Brightness in 0..100.
// Exception: None; produces the renderer's native RGB565 representation.
uint16_t theme_rgb565(uint32_t color, unsigned percent = 100)
{
    const auto r = ((color >> 16U) & 255U) * percent / 100U;
    const auto g = ((color >> 8U) & 255U) * percent / 100U;
    const auto b = (color & 255U) * percent / 100U;
    return static_cast<uint16_t>(((r >> 3U) << 11U) | ((g >> 2U) << 5U) | (b >> 3U));
}

// frame: Newly copied indexed avatar and its original state-dependent palettes.
// theme_id: Stable character theme identity; Classic and unknown IDs preserve every pixel.
// Exception: None; state glows, symbols, eyeshine and geometry are left intact.
void apply_character_theme(PixelAvatarFrame &frame, uint8_t theme_id)
{
    if (theme_id == 0 || theme_id >= kMuseCharacterThemes.size()) return;
    const auto &theme = muse_character_theme(theme_id);
    const uint32_t state_accent = rgb888(frame.palette[MUSE_PX_STATE_ACCENT]);
    const auto apply = [&](unsigned index, uint32_t color) {
        frame.palette[index] = theme_rgb565(color);
        frame.dim_palette[index] = theme_rgb565(color, 72);
    };
    apply(MUSE_PX_OUTLINE, theme.outline_rgb);
    apply(MUSE_PX_OUTLINE_SOFT, mix_rgb(theme.outline_rgb, theme.face_rgb, 125));
    apply(MUSE_PX_FUR_DARK, mix_rgb(theme.outline_rgb, theme.fur_rgb, 175));
    apply(MUSE_PX_FUR_MID, mix_rgb(theme.outline_rgb, theme.fur_rgb, 220));
    apply(MUSE_PX_FUR_LIGHT, theme.fur_rgb);
    apply(MUSE_PX_FUR_HIGHLIGHT, mix_rgb(theme.fur_rgb, 0xffffff, 75));
    apply(MUSE_PX_RIM, mix_rgb(theme.fur_rgb, state_accent, 115));
    apply(MUSE_PX_FACE_DARK, mix_rgb(theme.outline_rgb, theme.face_rgb, 220));
    apply(MUSE_PX_FACE, theme.face_rgb);
    apply(MUSE_PX_FACE_LIGHT, mix_rgb(theme.face_rgb, 0xffffff, 65));
    apply(MUSE_PX_IRIS, theme.outline_rgb);
    apply(MUSE_PX_BROW, mix_rgb(theme.outline_rgb, theme.fur_rgb, 35));
    apply(MUSE_PX_MOUTH, theme.outline_rgb);
    apply(MUSE_PX_BLUSH, mix_rgb(theme.face_rgb, theme.accent_rgb, 165));
    apply(MUSE_PX_BLUSH_DARK, theme.accent_rgb);
    apply(MUSE_PX_HEART, theme.accent_rgb);
}

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
    apply_character_theme(frame, input.style.character_theme);
    return PixelAvatarComposeStatus::Ok;
}

// seed: Artistic PRNG seed for one new serialized animation sequence.
void reset_muse_animation(uint32_t seed)
{
    muse_pixel_reset_animation(seed);
}

}  // namespace CODY
