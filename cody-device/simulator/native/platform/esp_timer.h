#pragma once

#include <chrono>
#include <cstdint>
#include <atomic>

// Process-wide host scenario clock. Set once before starting any firmware worker;
// zero keeps the interactive runtime on steady time. Timeline replay is separate.
inline std::atomic<int64_t> cody_host_manual_time_us{0};
// A live Studio child transitions forward from its initial one-second origin.
inline std::atomic<int64_t> cody_host_live_origin_steady_us{0};

// No parameters: monotonic microseconds since the first host timer use.
// This models elapsed time only; it is not an observation from a physical Cody.
inline int64_t esp_timer_get_time()
{
    const auto manual = cody_host_manual_time_us.load(std::memory_order_acquire);
    if (manual > 0) return manual;
    using clock = std::chrono::steady_clock;
    const auto live_origin = cody_host_live_origin_steady_us.load(std::memory_order_acquire);
    if (live_origin > 0) {
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
        return 1000000 + now - live_origin;
    }
    static const auto origin = clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - origin).count();
}
