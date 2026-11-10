/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>

namespace CODY {

// Discrete pixel shapes remain legible on the original 64 x 64 artwork grid.
enum class MuseSymbol : uint8_t {
    None, Question, Exclamation, Idea, Thinking, Heart, Sweat, Tension, Sleep, Count,
};

enum class MuseEyeStyle : uint8_t {
    Automatic, Normal, Wide, Happy, Cross, Closed, Count,
};

enum class MuseMouthStyle : uint8_t {
    Automatic, Smile, Round, Thoughtful, Talking, Grin, Flat, Count,
};

// Explicit face selections override pose and symbol expression independently.
// Automatic retains the authored pose, blink, gaze and reaction behavior.
struct MuseFaceStyle {
    MuseEyeStyle left_eye = MuseEyeStyle::Automatic;
    MuseEyeStyle right_eye = MuseEyeStyle::Automatic;
    MuseMouthStyle mouth = MuseMouthStyle::Automatic;
    float talking_openness = 0.5f; // 0..1, used only for an explicit Talking mouth.
};

// Defaults reproduce Muse's state-dependent orbit; count is scaled by each mode.
// Valid ranges: count 0..16, size 0..2, speed 0..5, radius 0..40,
// vertical_scale and brightness 0..1, twinkle_speed 0..20, RGB 0..0xffffff.
struct MuseParticleStyle {
    uint8_t count = 6;
    uint8_t size = 2;
    float speed = 1.0f;
    float radius = 25.0f;
    float vertical_scale = 0.42f;
    float twinkle_speed = 5.0f;
    float brightness = 1.0f;
    bool custom_color = false;
    uint32_t color_rgb = 0xffffff;
};

// Motion 0..2, breathing speed 0..10; body and hand offsets -8..8 pixels.
// Gaze offsets are -1..1 and eye openness is 0..1.
struct MusePoseStyle {
    float motion = 1.0f;
    float breathing_speed = 2.0f;
    float body_x = 0.0f;
    float body_y = 0.0f;
    float left_hand_x = 0.0f;
    float left_hand_y = 0.0f;
    float right_hand_x = 0.0f;
    float right_hand_y = 0.0f;
    float gaze_x = 0.0f;
    float gaze_y = 0.0f;
    float eye_openness = 1.0f;
};

// Offsets are -32..32 logical pixels relative to Muse; scale is 1 or 2.
// Bob speed is 0..10 and RGB is 0..0xffffff.
struct MuseSymbolStyle {
    MuseSymbol symbol = MuseSymbol::None;
    int8_t offset_x = 17;
    int8_t offset_y = 0;
    uint8_t scale = 1;
    float bob_speed = 2.0f;
    bool custom_color = false;
    uint32_t color_rgb = 0xffffff;
};

struct MuseVisualStyle {
    MusePoseStyle pose{};
    MuseFaceStyle face{};
    MuseParticleStyle particles{};
    MuseSymbolStyle symbol{};
    float pose_transition_seconds = 0.28f; // Valid range 0..3 seconds.
};

} // namespace CODY
