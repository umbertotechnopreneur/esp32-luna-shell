#pragma once

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

#ifdef __cplusplus
}
#endif
