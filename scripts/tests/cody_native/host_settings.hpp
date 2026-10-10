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
#include <string>
#include "nvs.h"

namespace HostSettings {

// Explicit fault injection stays in the host executable, never firmware snapshots.
struct State {
    bool initialized = false;
    esp_err_t initialize_error = ESP_OK;
    esp_err_t read_error = ESP_ERR_NVS_NOT_FOUND;
    esp_err_t write_error = ESP_OK;
    unsigned write_attempts = 0;
    std::string saved_value;
};

// No parameters: expose the host fixture for deterministic storage outcomes.
State &state();

// No parameters: discard the preceding test's storage state and injected failures.
void reset();

}  // namespace HostSettings
