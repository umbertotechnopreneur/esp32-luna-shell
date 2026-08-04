/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Boot screen and boot audio entry points for the firmware.
// Exposes the minimal API used by app_main and startup tasks.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t LUNASHELL_boot_ui_show(void);
esp_err_t LUNASHELL_boot_ui_mark_ready(void);
esp_err_t LUNASHELL_boot_ui_hide(void);
esp_err_t LUNASHELL_boot_audio_play(void);

#ifdef __cplusplus
}
#endif
