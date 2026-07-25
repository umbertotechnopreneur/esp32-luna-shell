/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

// Boot screen and boot audio entry points for the firmware.
// Exposes the minimal API used by app_main and startup tasks.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t qsee_boot_ui_show(void);
void qsee_boot_ui_mark_ready(void);
void qsee_boot_ui_wait_for_continue(void);
void qsee_boot_ui_hide(void);
esp_err_t qsee_boot_audio_play(void);

#ifdef __cplusplus
}
#endif
