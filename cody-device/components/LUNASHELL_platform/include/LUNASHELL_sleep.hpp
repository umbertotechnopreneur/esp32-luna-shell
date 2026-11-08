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

#include "esp_err.h"
#include <cstdint>

namespace LUNASHELL::sleep {

// wake_after_us: Optional explicit timer in addition to the physical IRQ wake sources.
// The power owner must confirm full transactional standby before calling this.
esp_err_t try_deep_sleep(uint64_t wake_after_us);
// No parameters: whether rejected entry released every hold and wake-only IMU state.
bool recovery_complete();

}  // namespace LUNASHELL::sleep
