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

#include <cstdint>

namespace LUNASHELL::power_policy {

struct SceneStandbyOverride {
    uint32_t seconds = 0;
    int64_t entered_at_us = 0;
};

// configured_seconds: Current saved setting, including zero when globally disabled.
// scene: Transient policy selected by the active scene.
constexpr uint32_t scene_standby_timeout_seconds(
    uint32_t configured_seconds, const SceneStandbyOverride &scene)
{
    return scene.seconds == 0 ? configured_seconds : scene.seconds;
}

// now_us: Current ESP timer observation at the power-owner boundary.
// last_activity_at_us: Timestamp of real activity; entering a scene never changes it.
// scene: Positive scene timeout with the independent entry timestamp.
constexpr bool scene_standby_expired(
    int64_t now_us, int64_t last_activity_at_us, const SceneStandbyOverride &scene)
{
    const int64_t anchor_us = last_activity_at_us > scene.entered_at_us
        ? last_activity_at_us : scene.entered_at_us;
    // Convert only at the ESP timer boundary. Even UINT32_MAX seconds fits int64_t.
    return scene.seconds != 0 && now_us >= anchor_us &&
        now_us - anchor_us >= static_cast<int64_t>(scene.seconds) * 1000000LL;
}

} // namespace LUNASHELL::power_policy
