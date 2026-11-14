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
// Host compilation selects Debug presentation without importing an IDF build tree.
#define CONFIG_LUNASHELL_BUILD_MODE_DEBUG 1
#ifndef CONFIG_ESP_TASK_WDT_TIMEOUT_S
// Matches the current 1.75C wake-word firmware's settings timeout contract.
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 12
#endif
