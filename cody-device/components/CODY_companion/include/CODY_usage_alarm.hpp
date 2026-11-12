#pragma once

#include <cstdint>
#include <limits>

#include "CODY_pet_audio.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

// The settings owner selects the alarm and threshold bits. No preference is
// persisted or inferred by this projection.
struct UsageAlarmOptions {
    bool enabled = false;
    uint8_t thresholds = 0x1f; // Bits 0..4 correspond to 70, 50, 20, 10 and 5 percent.
};

struct UsageAlarmChannel {
    int64_t weekly_reset = 0;
    uint64_t pending_sequence = 0;
    uint8_t observed_level = 0;
    uint8_t pending_level = 0;
    bool initialized = false;
};

// Retained by the caller, never by a source acquisition worker or an audio task.
struct UsageAlarmState {
    UsageAlarmChannel weekly{};
    uint64_t next_sequence = 0;
};
static_assert(sizeof(UsageAlarmState) <= 32, "Usage alarm state must remain bounded");

struct UsageAlarmNotice {
    uint64_t sequence = 0;
    uint8_t threshold_percent = 0;
    uint8_t intensity = 0; // 0 means none; 1..5 increase as remaining allowance decreases.
    PetCue cue = PetCue::Jump;
};

namespace UsageAlarmDetail {

inline constexpr uint16_t kBasisPoints[] = {7000, 5000, 2000, 1000, 500};
inline constexpr uint8_t kPercent[] = {70, 50, 20, 10, 5};

// remaining: Actual remaining amount in the same units as capacity.
// capacity: Positive actual capacity.
// Exception: None; callers validate the range before invoking this helper.
inline uint8_t level(uint64_t remaining, uint64_t capacity)
{
    uint8_t result = 0;
    for (uint8_t i = 0; i < 5; ++i) {
        // Exact floor(capacity * threshold / 10000), without overflowing uint64_t.
        const uint64_t limit = (capacity / 10000U) * kBasisPoints[i] +
            ((capacity % 10000U) * kBasisPoints[i]) / 10000U;
        if (remaining <= limit) result = static_cast<uint8_t>(i + 1U);
    }
    return result;
}

// channel: One source's last current observation and any not-yet-consumed notice.
// next: Level from the newly admitted canonical observation.
// enabled: Explicit settings mask for the five thresholds.
// Exception: None; a recovery rearms only thresholds above the new level.
inline bool observe(UsageAlarmChannel &channel, uint8_t next, uint8_t enabled)
{
    const uint8_t previous = channel.initialized ? channel.observed_level : 0;
    bool queued = false;
    if (next < previous) {
        if (channel.pending_level > next) channel.pending_level = 0;
    } else if (next > previous) {
        for (uint8_t level = next; level > previous; --level) {
            if ((enabled & (1U << (level - 1U))) != 0) {
                channel.pending_level = level;
                queued = true;
                break;
            }
        }
    }
    channel.observed_level = next;
    channel.initialized = true;
    return queued;
}

// sample: Canonical component observation already aged by its snapshot owner.
// Exception: None; stale, failed and unavailable data never trigger an alarm.
inline bool current(const LUNASHELL::ComponentSnapshot &sample)
{
    return sample.health == LUNASHELL::DeviceHealth::Ready && sample.data_valid &&
        !sample.data_stale && sample.last_error == ESP_OK;
}

} // namespace UsageAlarmDetail

// state: Retained weekly threshold observations.
// options: Explicit setting for the alarm and thresholds; unsupported bits are rejected.
// weekly: Canonical current weekly allowance, never a saved historical value.
// clock: Canonical clock, required to confirm that the weekly reset is still in the future.
// notice: Receives a pending notice, or intensity zero when no notice is available.
// A notice remains pending until acknowledge_usage_alarm confirms its presented identity.
// Exception: Rejects unsupported threshold bits or exhausted notice identities; no data are invented.
inline esp_err_t observe_usage_alarms(UsageAlarmState &state, const UsageAlarmOptions &options,
    const LUNASHELL::CodexWeeklyLimitSnapshot &weekly, const LUNASHELL::ClockSnapshot &clock,
    UsageAlarmNotice &notice)
{
    notice = {};
    if ((options.thresholds & ~0x1fU) != 0) return ESP_ERR_INVALID_ARG;
    const bool weekly_current = options.enabled && UsageAlarmDetail::current(weekly) &&
        UsageAlarmDetail::current(clock) && clock.utc_epoch_seconds > 0 &&
        weekly.reset_unix_seconds > clock.utc_epoch_seconds && weekly.remaining_basis_points <= 10000;
    if (!options.enabled) state.weekly = {};
    else if (weekly_current) {
        if (state.weekly.weekly_reset != weekly.reset_unix_seconds) {
            state.weekly = {};
            state.weekly.weekly_reset = weekly.reset_unix_seconds;
        }
        if (UsageAlarmDetail::observe(state.weekly,
                UsageAlarmDetail::level(weekly.remaining_basis_points, 10000), options.thresholds)) {
            if (state.next_sequence == std::numeric_limits<uint64_t>::max()) {
                state.weekly.pending_level = 0;
                return ESP_ERR_INVALID_STATE;
            }
            state.weekly.pending_sequence = ++state.next_sequence;
        }
        if (state.weekly.pending_level != 0 &&
            (options.thresholds & (1U << (state.weekly.pending_level - 1U))) == 0)
            state.weekly.pending_level = 0;
    }
    // A stale source cannot deliver a previously pending alert as if it were current.
    if (!weekly_current || state.weekly.pending_level == 0) return ESP_OK;
    auto &channel = state.weekly;
    notice.sequence = channel.pending_sequence;
    notice.intensity = channel.pending_level;
    notice.threshold_percent = UsageAlarmDetail::kPercent[channel.pending_level - 1U];
    notice.cue = channel.pending_level >= 4U ? PetCue::Surprise : PetCue::Jump;
    return ESP_OK;
}

// state: Same owner that observed this alert.
// notice: Exact notice whose first animation frame retired successfully.
// Exception: None; a stale or superseded notice never consumes a newer threshold.
inline void acknowledge_usage_alarm(UsageAlarmState &state, const UsageAlarmNotice &notice)
{
    if (notice.sequence != 0 && notice.sequence == state.weekly.pending_sequence &&
        notice.intensity > 0 && notice.intensity <= 5 && state.weekly.pending_level == notice.intensity &&
        notice.threshold_percent == UsageAlarmDetail::kPercent[notice.intensity - 1U]) {
        state.weekly.pending_level = 0;
        state.weekly.pending_sequence = 0;
    }
}

} // namespace CODY
