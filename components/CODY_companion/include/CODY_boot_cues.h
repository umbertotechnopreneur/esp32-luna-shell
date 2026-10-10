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
#ifdef __cplusplus
extern "C" {
#endif
// No parameters: validate the flash-owned cue; playback waits for canonical quiet policy.
esp_err_t CODY_boot_cues_prepare_boot(void);
// stage: Completed startup stage; timings use the real monotonic clock.
void CODY_boot_cues_boot_checkpoint(const char *stage);
// Configure the existing logger mirror after the SD owner's startup stage.
esp_err_t CODY_boot_cues_configure_diagnostics(void);
// No parameters: play completed-boot cues, or admit one pending request for verified time.
// Exception: Returns original admission/playback errors; admission is not playback completion.
esp_err_t CODY_boot_cues_play_eyes_open(void);
// No parameters: inspect only a pending boot's canonical clock, at most once per second.
// Asynchronous admission/playback errors remain logged and published by the cue owner.
void CODY_boot_cues_observe_pending(void);
#ifdef __cplusplus
}
#endif
