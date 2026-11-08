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

namespace LUNASHELL {

// A failed or stale observation invalidates the whole gesture, not just one
// sample. A fresh release is required before another hold can be accepted.
struct ButtonHold {
    bool blocked_until_release = false;
    bool fired = false;

    // pressed: Acquired level derived by the hardware owner from physical edges.
    // pressed_at_us: Time of this press edge, zero when its start is unknown.
    // current: Whether the canonical sample is valid and fresh.
    // now_us: Current monotonic policy time.
    // duration_us: Continuous hold required before firing once.
    bool update(bool pressed, int64_t pressed_at_us, bool current,
                int64_t now_us, int64_t duration_us)
    {
        if (!current) {
            blocked_until_release = true;
            return false;
        }
        if (!pressed) {
            blocked_until_release = false;
            fired = false;
            return false;
        }
        if (blocked_until_release || fired || pressed_at_us <= 0 ||
            now_us < pressed_at_us || duration_us <= 0 ||
            now_us - pressed_at_us < duration_us) {
            return false;
        }
        fired = true;
        return true;
    }
};

} // namespace LUNASHELL
