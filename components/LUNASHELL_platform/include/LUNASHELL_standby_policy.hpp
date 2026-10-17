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

enum class StandbySleep : uint8_t { Software, Deep };

inline constexpr int64_t kCodexOfflineTimeoutUs = 120000000;

struct CodexOfflineTimer {
    int64_t disconnected_at_us = 0;
};

// timer: Power-owner clock; reconnect, unknown evidence and explicit pairing clear it.
// now_us: Current monotonic owner time.
// link_known: A valid canonical link observation, including a real offline failure.
// connected: Confirmed authenticated PC transport, independent of chat activity.
// pairing_active: Explicit setup work must finish before the offline interval starts.
// manual_activity_at_us: Last real touch/button or completed wake, never background motion.
constexpr bool codex_offline_expired(CodexOfflineTimer &timer, int64_t now_us,
    bool link_known, bool connected, bool pairing_active, int64_t manual_activity_at_us)
{
    if (!link_known || connected || pairing_active) {
        timer.disconnected_at_us = 0;
        return false;
    }
    if (timer.disconnected_at_us == 0) {
        timer.disconnected_at_us = now_us;
    }
    const int64_t anchor_us = timer.disconnected_at_us > manual_activity_at_us
        ? timer.disconnected_at_us : manual_activity_at_us;
    return now_us >= anchor_us && now_us - anchor_us >= kCodexOfflineTimeoutUs;
}

// standby: Requested overall software standby state.
// voice_listening: Whether speech acquisition still owns the audio transport.
[[nodiscard]] constexpr bool select_standby_audio(bool standby, bool voice_listening)
{
    return standby && !voice_listening;
}

// deep_sleep_enabled: Whether the selected build permits loss of runtime state.
// retained_connection: Confirmed radio retention needed for incoming Codex events.
// voice_listening: Whether the audio owner must keep acquiring standby speech.
// touch_wake_available: Whether this profile can enter the existing deep-sleep path.
[[nodiscard]] constexpr StandbySleep select_standby_sleep(
    bool deep_sleep_enabled, bool retained_connection, bool voice_listening,
    bool touch_wake_available)
{
    // A live service requires software standby even in a Release build.
    return deep_sleep_enabled && touch_wake_available &&
        !retained_connection && !voice_listening ? StandbySleep::Deep : StandbySleep::Software;
}

} // namespace LUNASHELL::power_policy
