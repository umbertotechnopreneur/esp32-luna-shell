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


// Read-only HTTP API entry point for device diagnostics.
// Owns server startup and shutdown around the station lifecycle.

#pragma once

#include <cstddef>

#include "esp_err.h"

namespace LUNASHELL {

constexpr unsigned short kHttpServerPort = 8080;
constexpr std::size_t kHttpApiKeyMaxLength = 63;

/** Start the read-only device HTTP API and bind it to the station lifecycle. */
esp_err_t httpServerInit();
/** Persist the HTTP preference and apply it immediately when Wi-Fi is usable. */
esp_err_t httpServerSetEnabled(bool enabled);
/** Persist a validated API key and update active authentication without changing lifecycle. */
esp_err_t httpServerSetApiKey(const char *api_key);

}  // namespace LUNASHELL
