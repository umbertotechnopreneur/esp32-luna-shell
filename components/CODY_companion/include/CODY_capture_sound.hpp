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

#include "esp_err.h"

namespace CODY {

// No parameters: prepare one dormant audio worker for the original shutter cue.
// Exception: Returns the real worker allocation error.
esp_err_t prepare_capture_sound();

// No parameters: queue one shutter after a successfully retired capture frame.
// Exception: Returns the real notification or unavailable-route error.
esp_err_t notify_capture_sound();

} // namespace CODY
