#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe and initialize hardware attached to the Waveshare BSP I2C bus.
 *
 * Every peripheral is fail-open: a missing optional sensor is logged but does
 * not prevent the UI from starting.
 */
esp_err_t qsee_hardware_init(void);

/**
 * Refresh the canonical hardware snapshot once. No background polling occurs.
 */
esp_err_t qsee_hardware_refresh(const char *reason);

/**
 * Report the result of an actual speaker playback attempt.
 */
void qsee_hardware_report_speaker(bool ready, esp_err_t error);

/**
 * Start Wi-Fi STA without credentials and BLE advertising as QSEE-xxxx.
 */
esp_err_t qsee_radios_init(void);

#ifdef __cplusplus
}
#endif
