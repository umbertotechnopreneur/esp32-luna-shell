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
#include <cstdint>
#include "CODY_rig.hpp"

namespace CODY {
enum class DemoPhase : uint8_t { Breathe, ContourSlow, ContourFast, Sleepy, Awake };
struct DemoStep {
    DemoPhase identity;
    uint32_t duration_ms;
    Channels face;
    float amplitude;
    float speed;
};
// elapsed_us: Active animation time; parked time has already been excluded.
// face: Receives authored channel targets for smooth interpolation.
// contour: Receives authored contour targets; real stimuli compose afterwards.
// phase: Receives the current semantic demo identity only on success.
esp_err_t demo_targets(int64_t elapsed_us, Channels &face, ContourControls &contour, DemoPhase &phase);
} // namespace CODY
