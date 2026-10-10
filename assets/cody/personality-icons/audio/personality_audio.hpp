/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once
#include <cstddef>
#include <cstdint>
#include "CODY_personality.hpp"
namespace CODY::PersonalityAudioAssets {
struct Clip {
    PersonalityCue cue;
    const int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
    bool enabled;
};
inline constexpr size_t clip_count = 14;
extern const Clip clips[clip_count];
} // namespace CODY::PersonalityAudioAssets
