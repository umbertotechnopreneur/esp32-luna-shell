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

// Render the waking face after local time, power, and storage stages finish.
// This reports a real boot checkpoint; it is not a measured percentage of startup.
esp_err_t CODY_companion_boot_midpoint(void);

// Render the ready face synchronously after every mandatory boot stage and watchdog cleanup.
// Subsequent rotation/wake redraws retain the completed phase; failures remain terminal.
esp_err_t CODY_companion_boot_completed(void);
// Enter the authored animation demo after the ready frame if autostart is enabled.
esp_err_t CODY_companion_demo_start(void);

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
