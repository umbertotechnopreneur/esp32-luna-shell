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

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// No parameters: prepares the first native frame before starting the display worker.
// Exception: Returns the original initialization or frame retirement failure.
esp_err_t CODY_companion_start(void);

// No parameters: admits setup after the services are ready and startup art has retired.
// Exception: Returns the original route or display failure, or a bounded timeout.
esp_err_t CODY_companion_boot_completed(void);

// No parameters: requires an awake panel and the power owner's serialized transition.
// Exception: Returns the original shutdown frame error or a bounded timeout.
esp_err_t CODY_companion_prepare_shutdown(void);

// error: Original shutdown failure; restore display writes and brightness before calling.
// Returns only after a visible error frame retires; the caller retains the operation error.
// A subsequent wake lifecycle clears the retained failure presentation.
// Exception: Returns the real presentation error or timeout; rejects ESP_OK.
esp_err_t CODY_companion_report_shutdown_error(esp_err_t error);

#if !defined(CODY_DEVICE_UI_ONLY)
esp_err_t CODY_companion_boot_midpoint(void);
esp_err_t CODY_companion_demo_start(void);

// expression_key: Exact implemented catalog key, such as "calm", "happy", or "embarrassed".
// Select a base combination during Idle without resetting breathing or blinking.
// Sleep/Wake transitions and unknown keys are rejected with their real error.
esp_err_t CODY_companion_set_expression(const char *expression_key);

// enabled: Session preference for the existing measured FPS/CPU layer on eligible scenes.
// Queues old/new HUD damage; it never samples hardware or allocates a framebuffer.
esp_err_t CODY_companion_set_debug_readout_enabled(bool enabled);

// enabled: Non-null output receiving the current session preference on success only.
esp_err_t CODY_companion_get_debug_readout_enabled(bool *enabled);

// enabled: Removed PWR screenshot preference; Power exclusively owns standby/shutdown.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
esp_err_t CODY_companion_set_power_screenshot_enabled(bool enabled);

// enabled: Receives false; Power never admits screenshots.
esp_err_t CODY_companion_get_power_screenshot_enabled(bool *enabled);

// enabled: Follow the pearl body's row deformation when true; keep the independent face when false.
// Session-scoped preference: queues geometry for the worker, without advancing paused animation.
// Returns the real lock/state error, or ESP_ERR_NOT_SUPPORTED when pearl jelly is not compiled in.
esp_err_t CODY_companion_set_face_deformation(bool enabled);

// enabled: Non-null output receiving the requested session preference on success only.
// Returns ESP_ERR_INVALID_ARG for null, the real lock/state error, or ESP_ERR_NOT_SUPPORTED.
esp_err_t CODY_companion_get_face_deformation(bool *enabled);
#endif

#ifdef __cplusplus
}
#endif
