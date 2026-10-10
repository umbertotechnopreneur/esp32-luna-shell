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
#include "codex_audio_assets.hpp"

namespace CODY {

enum class CodexAudioState : uint8_t {
    Idle = 0, Dispatched = 1, Playing = 2, Completed = 3, DroppedBusy = 4,
    DroppedCooldown = 5, Suppressed = 6, Expired = 7, Failed = 8, Unavailable = 9,
};

struct CodexAudioStatus {
    CodexAudioState state = CodexAudioState::Idle;
    esp_err_t last_error = ESP_OK;
    uint32_t request_sequence = 0;
    CodexAudioCue cue = CodexAudioCue::None;
};

// cue: One newly observed semantic transition, never a polling/repaint event.
// status: Receives admitted/dropped/suppressed disposition; Dispatched is not completed playback.
// Exception: Returns rejected identities or the original shared audio owner error.
esp_err_t notify_codex_audio(CodexAudioCue cue, CodexAudioStatus &status);
// suspended: True when the companion presentation is hidden; cancels pending audio without replay.
// Exception: Returns the original retained owner error.
esp_err_t set_codex_audio_suspended(bool suspended);
// enabled: Explicit session preference; volume, quiet hours and immutable build policy still apply.
// Exception: Returns the original retained owner error.
esp_err_t set_codex_audio_enabled(bool enabled);
// status: Receives current shared-owner outcome; cue is None if the owner is handling another channel.
// Exception: Returns the original retained asynchronous owner failure.
esp_err_t get_codex_audio_status(CodexAudioStatus &status);

} // namespace CODY
