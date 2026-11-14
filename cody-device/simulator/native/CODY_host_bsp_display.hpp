#pragma once
#include "esp_err.h"

#if defined(_WIN32)
#define CODY_HOST_BSP_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_BSP_API extern "C" __attribute__((visibility("default")))
#endif

// initial_brightness: Explicit synthetic panel level, from 0 to 100 percent.
// panel_enabled: Explicit synthetic hardware panel state.
// reset_reason: One ESP reset identity selected by the host scenario.
// Exception: Rejects invalid values or replacing an already initialized panel.
CODY_HOST_BSP_API esp_err_t cody_host_bsp_display_initialize(
    int initial_brightness, bool panel_enabled, int reset_reason);

// brightness: Receives the virtual hardware level, not a physical measurement.
// panel_enabled: Receives the explicit virtual panel gate.
// Exception: Returns the original unavailable-state or invalid-output error.
CODY_HOST_BSP_API esp_err_t cody_host_bsp_display_read(int *brightness, bool *panel_enabled);
