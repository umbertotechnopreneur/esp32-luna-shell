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

#include <algorithm>
#include <array>
#include <cstdint>

namespace LUNASHELL {

inline constexpr std::array<uint32_t, 7> kFullSnapshotPeriodsMs = {
    300,
    500,
    1000,
    2000,
    3000,
    5000,
    10000,
};

constexpr bool isFullSnapshotPeriodSupported(uint32_t period_ms)
{
    return std::find(
               kFullSnapshotPeriodsMs.begin(),
               kFullSnapshotPeriodsMs.end(),
               period_ms
           ) != kFullSnapshotPeriodsMs.end();
}

}  // namespace LUNASHELL
