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
