#include "LUNASHELL_snapshot_aging.hpp"
#include <algorithm>

namespace LUNASHELL {
namespace {
constexpr int64_t kMinimumMeasuredSampleTtlUs = 5LL * 1000LL * 1000LL;
constexpr int64_t kFullSnapshotTtlNumerator = 6;
constexpr int64_t kFullSnapshotTtlDenominator = 5;
}

// component: Owner-provided measurement whose timestamp is preserved.
// now_us: Explicit monotonic observation time.
// ttl_us: Positive maximum sample age.
void age_measured_sample(
    LUNASHELL::ComponentSnapshot &component,
    int64_t now_us,
    int64_t ttl_us
)
{
    if (!component.data_valid || component.data_stale ||
        component.last_success_at_us <= 0) {
        return;
    }
    if (now_us < component.last_success_at_us ||
        now_us - component.last_success_at_us >= ttl_us) {
        component.data_valid = false;
        component.data_stale = true;
    }
}

// snapshot: Owner cadence and validity used to select a conservative TTL.
int64_t measured_sample_ttl_us(const LUNASHELL::DeviceSnapshot &snapshot)
{
    if (!snapshot.diagnostics.full_snapshot_monitor_active ||
        snapshot.diagnostics.full_snapshot_period.last_error != ESP_OK ||
        !snapshot.diagnostics.full_snapshot_period.data_valid) {
        return kMinimumMeasuredSampleTtlUs;
    }

    const int64_t cadence_ttl_us =
        static_cast<int64_t>(snapshot.diagnostics.full_snapshot_period_ms) *
        1000LL * kFullSnapshotTtlNumerator /
        kFullSnapshotTtlDenominator;
    return std::max(kMinimumMeasuredSampleTtlUs, cadence_ttl_us);
}

// snapshot: Canonical observations whose original timestamps must remain untouched.
// now_us: Actual monotonic observation time used to mark, never refresh, stale data.
void apply_measured_sample_aging(
    LUNASHELL::DeviceSnapshot &snapshot,
    int64_t now_us
)
{
    const int64_t ttl_us = measured_sample_ttl_us(snapshot);
    age_measured_sample(snapshot.battery, now_us, ttl_us);
    age_measured_sample(snapshot.rtc, now_us, ttl_us);
    age_measured_sample(snapshot.clock, now_us, ttl_us);
    age_measured_sample(snapshot.imu, now_us, ttl_us);
    age_measured_sample(snapshot.sensor_fusion, now_us, ttl_us);
    age_measured_sample(snapshot.distance, now_us, ttl_us);
    for (LUNASHELL::TofSensorSnapshot &sensor : snapshot.distance.sensors) {
        age_measured_sample(sensor, now_us, ttl_us);
    }
    age_measured_sample(snapshot.gps, now_us, ttl_us);
    if (snapshot.voice.state == LUNASHELL::VoiceState::Listening) {
        age_measured_sample(snapshot.voice, now_us, 1000000);
    }
    age_measured_sample(snapshot.audio.microphone, now_us, ttl_us);
    age_measured_sample(snapshot.audio.playback, now_us, ttl_us);
    age_measured_sample(snapshot.character_interaction, now_us, ttl_us);
    age_measured_sample(snapshot.character_touch, now_us, ttl_us);
    age_measured_sample(snapshot.audio.spectrum, now_us, ttl_us);
    if (snapshot.ui.orientation.auto_rotation_enabled) {
        age_measured_sample(snapshot.ui.orientation, now_us, ttl_us);
    }
    age_measured_sample(snapshot.memory, now_us, ttl_us);
    age_measured_sample(snapshot.processor, now_us, ttl_us);
    age_measured_sample(snapshot.storage.logger_capture, now_us, ttl_us);
}

}
