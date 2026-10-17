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
#include "brookesia/system_core.hpp"
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_TIMEOUT = 0x107;
// error: Exact injected SDK transport error used by the real shell.
inline const char *esp_err_to_name(int error) {
    return error == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : "HOST_ERROR";
}
inline void LUNASHELL_power_manager_note_ui_standby_result(uint32_t sequence, int result) {
    shell_test::fixture.acknowledgements.emplace_back(sequence, result);
}
