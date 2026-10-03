#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Complete the mandatory first frame and touch sample before starting the worker.
// Real touch polling uses 50 ms while active and 200 ms while rendering is parked.
// Startup or runtime faults are terminal; another call does not restart failed hardware.
esp_err_t CODY_companion_start(void);

// Draw the current animated face after local time, power, and storage stages finish.
// This reports a real boot checkpoint; it is not a measured percentage of startup.
esp_err_t CODY_companion_boot_midpoint(void);

// Complete the real startup checkpoint without replacing the animated facial lifecycle.
// Every ordinary boot uses Calm/Happy/Calm; Deep-sleep reset uses Wake/Idle instead.
// Effective first boot schedules greeting text two seconds after this frame, excluding wake.
esp_err_t CODY_companion_boot_completed(void);
// Preserve the existing startup request without replacing the active facial lifecycle.
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

// enabled: Follow the pearl body's row deformation when true; keep the independent face when false.
// Session-scoped preference: queues geometry for the worker, without advancing paused animation.
// Returns the real lock/state error, or ESP_ERR_NOT_SUPPORTED when pearl jelly is not compiled in.
esp_err_t CODY_companion_set_face_deformation(bool enabled);

// enabled: Non-null output receiving the requested session preference on success only.
// Returns ESP_ERR_INVALID_ARG for null, the real lock/state error, or ESP_ERR_NOT_SUPPORTED.
esp_err_t CODY_companion_get_face_deformation(bool *enabled);

#ifdef __cplusplus
}
#endif
