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

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace LUNASHELL::wifi_provisioning {

// These owner-to-owner calls do not write credentials or issue another connect.
esp_err_t begin();
// ssid: Confirmed credential-event SSID bytes, possibly all 32 bytes occupied.
// ssid_capacity: Size of the SDK event's SSID array.
esp_err_t credentialsReceived(const uint8_t *ssid, size_t ssid_capacity);
// keep_connection: Preserve intent only after confirmed credential success.
esp_err_t end(bool keep_connection);

}  // namespace LUNASHELL::wifi_provisioning
