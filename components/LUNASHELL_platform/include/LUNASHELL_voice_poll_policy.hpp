#pragma once
#include <cstdint>

namespace LUNASHELL::voice_poll {
inline constexpr uint32_t kIdleMs = 100;
inline constexpr uint32_t kActiveMs = 5;
inline constexpr int64_t kAckTimeoutUs = 10000000;
inline constexpr int64_t kSessionTimeoutUs = 90000000;

// now_us: Actual monotonic time used by the transport's timeout checks.
// started_us: Current utterance start, never reset during a connectivity gap.
// acknowledged_us: Last authenticated ACK, never extended by a failed send.
// A disconnected transport has no socket work; retain the original fast cadence
// near either deadline so a coarse wait cannot add a full idle-period overshoot.
inline bool disconnected_needs_fast_poll(int64_t now_us, int64_t started_us, int64_t acknowledged_us)
{
    constexpr int64_t idle_us = static_cast<int64_t>(kIdleMs) * 1000;
    return now_us - started_us >= kSessionTimeoutUs - idle_us ||
           now_us - acknowledged_us >= kAckTimeoutUs - idle_us;
}
} // namespace LUNASHELL::voice_poll
