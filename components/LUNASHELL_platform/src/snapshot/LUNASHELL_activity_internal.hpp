#pragma once
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL::snapshot_detail::activity {
inline constexpr uint32_t kPeriodMs = 50;
// No parameters: requested observation stops after a terminal model initialization error.
bool requested();
// sample: Canonical acquisition result, borrowed before any consumer-side smoothing.
// error: Original acquisition/publication error; failed attempts invalidate the window.
// sampled_at_us: Original acquisition timestamp, never a repeated snapshot timestamp.
// stillness: Same acquisition's published projection, or null for a failed acquisition.
void accept(const ImuSnapshot &sample, esp_err_t error, int64_t sampled_at_us,
            const ImuStillnessSnapshot *stillness = nullptr);
// enabled: Whether active-mode acquisition is admitted by the existing power barrier.
void set_active(bool enabled);
} // namespace LUNASHELL::snapshot_detail::activity
