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
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


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
