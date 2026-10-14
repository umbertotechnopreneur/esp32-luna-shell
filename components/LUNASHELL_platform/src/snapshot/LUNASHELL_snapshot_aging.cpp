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

#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_imu_stillness.hpp"
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
    imu_stillness::age(snapshot.activity.stillness, now_us);
    if (snapshot.activity.scores_valid &&
        (now_us < snapshot.activity.window_end_us || now_us - snapshot.activity.window_end_us > 6000000)) {
        snapshot.activity.scores_valid = false;
        snapshot.activity.activity = ActivityClass::Unknown;
        snapshot.activity.rejection = ActivityRejection::MissingSample;
        snapshot.activity.error = ESP_ERR_TIMEOUT;
        snapshot.activity.stable_windows = 0;
    }
    age_codex_technical(snapshot.codex_technical, now_us);
    codex_presence::age(snapshot.radios.codex_link.app_presence, now_us);
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

// technical: Independent source measurements; missing values remain unavailable.
// now_us: Device clock used only relative to this packet's device receipt.
void age_codex_technical(CodexTechnicalSnapshot &technical, int64_t now_us)
{
    constexpr uint64_t source_ttl_us = 120000000;
    constexpr uint64_t transport_ttl_us = 8000000;
    if (technical.received_at_us <= 0) {
        return;
    }
    const bool clock_discontinuity = now_us < technical.received_at_us;
    const uint64_t elapsed = clock_discontinuity ? transport_ttl_us :
        static_cast<uint64_t>(now_us - technical.received_at_us);
    technical.data_stale = clock_discontinuity || elapsed >= transport_ttl_us;
    technical.model_stale = technical.model_valid && (technical.data_stale ||
        technical.model_age_at_receive_us >= source_ttl_us ||
        elapsed >= source_ttl_us - technical.model_age_at_receive_us);
    technical.effort_stale = technical.effort_valid && technical.model_stale;
    // Heartbeats carry the remaining host calendar lease; they cannot renew a yesterday catalog.
    technical.cost_stale = technical.cost_band != CodexCostBand::Unknown &&
        (technical.data_stale || technical.model_stale ||
         technical.cost_source != CodexCostSource::CodexStandardCatalog ||
         elapsed >= technical.cost_valid_for_us);
    if (technical.compaction == CodexCompactionState::Active &&
        (technical.data_stale || !technical.compaction_age_valid ||
         technical.compaction_age_at_receive_us >= source_ttl_us ||
         elapsed >= source_ttl_us - technical.compaction_age_at_receive_us)) {
        // compact_age is the genuine start/last lifecycle event age. Current hooks have no
        // ongoing confirmation pulse: repeated authenticated packets do not renew this source lease.
        // A future ongoing source must carry confirmation age separately from start elapsed.
        // Loss of observation never becomes successful completion.
        technical.compaction = CodexCompactionState::Stale;
    }
    const bool usage_present = technical.context_valid || technical.used_valid || technical.capacity_valid ||
        technical.cumulative_valid || technical.input_valid || technical.output_valid || technical.threshold_valid;
    technical.context_stale = usage_present && (technical.data_stale ||
        technical.usage_age_at_receive_us >= source_ttl_us ||
        elapsed >= source_ttl_us - technical.usage_age_at_receive_us);
}

}
