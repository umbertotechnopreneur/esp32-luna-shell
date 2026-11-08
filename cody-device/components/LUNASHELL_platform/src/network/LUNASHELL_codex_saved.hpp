#pragma once

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL::codex_saved {

inline constexpr char kCheckpointKey[] = "cody_codex_v1";

// No parameters: the CCP worker opens its optional exact-version checkpoint once.
// Storage errors remain in the historical observation and are logged by its owner.
void load();

// peer: Authenticated PC identity owning this weekly sample.
// sample: Successfully published weekly observation with its original receipt and source age.
void remember_weekly(const char *peer, const CodexWeeklyLimitSnapshot &sample);

// peer: Authenticated PC identity owning this account sample.
// sample: Successfully published account observation with its original receipt and source age.
void remember_account(const char *peer, const CodexAccountSnapshot &sample);

// now_us: Monotonic worker time for checkpoint coalescing and flash write pacing.
void checkpoint(int64_t now_us);

// No parameters: clear RAM only after the settings owner confirms the checkpoint was erased.
void reset_after_erase();

// peer: Exact current retained pairing identity, or an empty string when absent.
// saved: Receives dated historical fields or an explicit unavailable projection.
void read(const char *peer, CodexSavedSnapshot &saved);

} // namespace LUNASHELL::codex_saved
