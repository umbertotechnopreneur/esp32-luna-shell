#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// clock: Original owner timestamps, validity and health already aged by the snapshot.
bool clock_observation_current(const ClockSnapshot &clock)
{
    return clock.preferences_valid && clock.health == DeviceHealth::Ready &&
        clock.last_error == ESP_OK && clock.data_valid && !clock.data_stale &&
        clock.last_success_at_us > 0;
}

// clock: Explicitly disabled routines permit cues without guessing a local hour.
ClockCuePolicy clock_cue_policy(const ClockSnapshot &clock)
{
    if (!clock.preferences_valid) {
        return ClockCuePolicy::SettingsUnavailable;
    }
    if (!clock.routines_enabled || !clock.quiet_hours_enabled) {
        return ClockCuePolicy::Allowed;
    }
    if (!clock_observation_current(clock)) {
        return ClockCuePolicy::TimeUnavailable;
    }
    return clock.quiet ? ClockCuePolicy::QuietHours : ClockCuePolicy::Allowed;
}

}
