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
#include "esp_err.h"

namespace CODY {

// No parameters: registers the original flash-owned tap without starting playback.
// Exception: Returns the audio owner's exact preparation error.
esp_err_t prepare_typewriter_audio();

// newly_visible_characters: Positive teletype progress in one successfully retired frame.
// Exception: Returns request admission or retained asynchronous playback errors.
esp_err_t notify_typewriter_audio(uint32_t newly_visible_characters);

// suspended: Clears pending taps when hiding, leaving, or parking the current scene.
// Exception: Returns the existing owner's exact notification error.
esp_err_t set_typewriter_audio_suspended(bool suspended);

} // namespace CODY
