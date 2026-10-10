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
#include "CODY_personality.hpp"
#include "CODY_scene.hpp"

namespace CODY {

// These are classified canonical facts on firmware, visibly synthetic inputs on PC.
struct ConnectionPersonalityInputs {
    bool wifi_current = false;
    bool wifi_searching = false;
    bool pc_current = false;
    bool pc_connected = false;
    bool pc_searching = false;
};

struct ConnectionPersonalityState {
    bool pc_observed = false;
    bool pc_observation_continuous = false;
    bool pc_was_connected = false;
    PersonalityCue event = PersonalityCue::None;
    int64_t event_remaining_us = 0;
};

// inputs: Classified canonical connectivity, with validity independent of animation.
// active_delta_us: Nonnegative active time; standby and pause contribute zero.
// state: Previous transition state; updated only after input validation.
// cue: Receives the currently supported network or transient connection reaction.
// Exception: Returns invalid argument for negative timing or contradictory facts.
esp_err_t advance_connection_personality(const ConnectionPersonalityInputs &inputs,
    int64_t active_delta_us, ConnectionPersonalityState &state, PersonalityCue &cue);

// scene: Semantic identity of a route already admitted from canonical runtime state.
// No exceptions: Unsupported routes deliberately request no manga reaction.
PersonalityCue scene_personality_cue(SceneId scene);

// scene: Current admitted route, or CompanionIdle for the ordinary character view.
// connection: Current canonical-supported connection reaction.
// No exceptions: Failure reactions retain priority over connection status.
PersonalityCue resolve_scene_personality_cue(SceneId scene, PersonalityCue connection);

} // namespace CODY
