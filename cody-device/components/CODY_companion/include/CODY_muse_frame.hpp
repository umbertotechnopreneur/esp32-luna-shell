/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Portable indexed frame composition for the Muse avatar.
 */

#pragma once

#include <array>
#include <cstdint>

namespace CODY {

enum class PixelAvatarPose : uint8_t {
    Waking = 0,
    Idle = 1,
    Listening = 2,
    Thinking = 3,
    Reply = 4,
    Error = 5,
    Sleep = 6,
};

enum class PixelAvatarComposeStatus : uint8_t {
    Ok,
    InvalidPose,
    InvalidAudioLevel,
};

inline constexpr int kPixelAvatarGridSize = 64;
inline constexpr uint32_t kPixelAvatarFrameMs = 40;
inline constexpr uint32_t kPixelAvatarReactionMs = 1600;

struct PixelAvatarFrame {
    // The display owner retains the grid and palettes until all strips retire.
    std::array<uint8_t, kPixelAvatarGridSize * kPixelAvatarGridSize> cells{};
    std::array<uint16_t, 32> palette{};
    std::array<uint16_t, 32> dim_palette{};
};

struct PixelAvatarInput {
    PixelAvatarPose pose;
    uint32_t time_ms;      // Active animation time, excluding standby.
    uint32_t pose_ms;      // Time since the semantic pose changed.
    uint32_t reaction_ms;  // A value >= kPixelAvatarReactionMs means inactive.
    float audio_level = 0.0f;
    bool working = false;
};

// frame: Caller-owned indexed output, replaced completely on success.
// input: Semantic pose, supplied clock and artistic input, with no acquisition.
// Exception: Returns a typed error and preserves frame if input is invalid.
// Calls to this single renderer and its reset must be serialized by the owner.
PixelAvatarComposeStatus compose_muse_frame(PixelAvatarFrame &frame, PixelAvatarInput input);

// seed: Artistic PRNG seed; zero selects the default, nonzero startup seed.
// Starts a fresh animation sequence. Subsequent inputs use a new elapsed clock.
// Only the animation owner may call this between render operations.
void reset_muse_animation(uint32_t seed);

}  // namespace CODY
