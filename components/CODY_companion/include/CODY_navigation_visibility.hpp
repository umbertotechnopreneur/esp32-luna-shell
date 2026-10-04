#pragma once

#include <algorithm>
#include <cstdint>
#include "navigation_data.hpp"

namespace CODY {

// Small wall-clock presentation state; it owns no observations or hardware reads.
struct NavigationVisibility {
    static constexpr int64_t kHideAfterUs = NavigationData::auto_hide_ms * 1000LL;
    static constexpr int64_t kFadeUs = NavigationData::fade_ms * 1000LL;
    bool primed = false;
    int64_t last_touch_us = 0;
    int64_t fade_started_us = 0;
    uint8_t start_opacity = 0;
    uint8_t target_opacity = 255;

    // now_us: Monotonic renderer timestamp, independent of animation pause.
    uint8_t opacity(int64_t now_us) const
    {
        if (!primed) {
            return 0;
        }
        const int64_t elapsed = std::clamp(now_us - fade_started_us, int64_t{0}, kFadeUs);
        return static_cast<uint8_t>((start_opacity * (kFadeUs - elapsed) +
            target_opacity * elapsed + kFadeUs / 2) / kFadeUs);
    }

    // now_us: Original successful touch-report time for shared status/debug chrome.
    void interact(int64_t now_us)
    {
        const uint8_t current = opacity(now_us);
        if (!primed || target_opacity != 255) {
            start_opacity = current;
            target_opacity = 255;
            fade_started_us = now_us;
        }
        primed = true;
        last_touch_us = now_us;
    }

    // now_us: Wall clock; hidden/paused characters do not freeze the toolbar timeout.
    void update(int64_t now_us)
    {
        if (primed && target_opacity != 0 && now_us - last_touch_us >= kHideAfterUs) {
            const int64_t deadline = last_touch_us + kHideAfterUs;
            start_opacity = opacity(deadline);
            fade_started_us = deadline;
            target_opacity = 0;
        }
    }
};

} // namespace CODY
