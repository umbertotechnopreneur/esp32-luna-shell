#pragma once

#include <cmath>
#include <cstdint>
#include "CODY_response.hpp"
#include "esp_log.h"

namespace CODY {

// Serialized presentation state; recognition never performs navigation itself.
struct CompanionTapState {
    int64_t started_at_us = 0;
    int64_t last_sample_at_us = 0;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    uint16_t rotation_degrees = 0;
    bool candidate = false;
    bool contact_active = false;
};

// state: Contact to invalidate without manufacturing an acquired release.
inline void cancel_companion_tap(CompanionTapState &state)
{
    state.candidate = false;
}

// state: Presentation-owned recognizer retained throughout a physical contact.
// x: Successfully acquired logical horizontal coordinate.
// y: Successfully acquired logical vertical coordinate.
// pressed: Successfully acquired contact state, including an actual release.
// pressed_edge: First sample of a new acquired contact.
// sampled_at_us: Original touch acquisition timestamp.
// rotation_degrees: Orientation of the fully displayed view.
// eligible: Whether this contact started on visible passive companion content.
// consumed: Whether an explicit control or another gesture owns this contact.
inline bool observe_companion_tap(CompanionTapState &state, int16_t x, int16_t y,
    bool pressed, bool pressed_edge, int64_t sampled_at_us, uint16_t rotation_degrees,
    bool eligible, bool consumed)
{
    if (pressed_edge) {
        state = {sampled_at_us, sampled_at_us, x, y, rotation_degrees,
                 eligible && !consumed && sampled_at_us > 0, true};
        return false;
    }
    if (!state.contact_active) {
        return false;
    }
    const int dx = static_cast<int>(x) - state.origin_x;
    const int dy = static_cast<int>(y) - state.origin_y;
    const int64_t elapsed_us = sampled_at_us - state.started_at_us;
    const int64_t gap_us = sampled_at_us - state.last_sample_at_us;
    // A genuine release has no contact coordinates in the BSP contract. Only
    // acquired pressed samples can measure travel; release still validates time and ownership.
    // IRQ-driven stationary contacts may have no intermediate report; their release
    // uses the complete short-contact limit rather than inventing held samples.
    if (consumed || !eligible || rotation_degrees != state.rotation_degrees ||
        gap_us <= 0 || (pressed && gap_us > 150000) || elapsed_us > 450000 ||
        (pressed && dx * dx + dy * dy > 144)) {
        state.candidate = false;
    }
    state.last_sample_at_us = sampled_at_us;
    if (pressed) {
        return false;
    }
    const bool accepted = state.candidate && elapsed_us >= 30000;
    state.contact_active = false;
    state.candidate = false;
    return accepted;
}

struct CompanionShakeState {
    float baseline[3]{};
    float previous_peak[3]{};
    int64_t sampled_at_us = 0;
    int64_t calm_since_us = 0;
    int64_t burst_started_us = 0;
    int64_t blocked_until_us = 0;
    uint8_t alternating_peaks = 0;
    bool baseline_valid = false;
    bool armed = false;
    bool above_threshold = false;
};

// state: Navigation recognizer; sensor ownership remains in the canonical IMU owner.
// now_us: Device monotonic time, never a host clock.
inline void suspend_companion_shake(CompanionShakeState &state, int64_t now_us)
{
    state = {};
    // A wake-causing movement must settle before it can become navigation.
    state.blocked_until_us = now_us + 2000000;
}

// state: Presentation-owned recognizer consuming distinct canonical measurements.
// sample: Existing response projection of the canonical IMU sample, never a driver read.
// now_us: Device observation time used only for freshness and gesture debounce.
// active: Whether navigation can operate on an awake, visible companion view.
// Exception: Rejects invalid, stale, replayed or non-finite measurements without navigation.
inline bool observe_companion_shake(CompanionShakeState &state,
    const MotionSample &sample, int64_t now_us, bool active)
{
    const int64_t timestamp = sample.stamp.sampled_at_us;
    if (!active || sample.stamp.health != SampleHealth::Current || timestamp <= 0 ||
        now_us < timestamp || now_us - timestamp > 250000) {
        suspend_companion_shake(state, now_us);
        return false;
    }
    if (timestamp == state.sampled_at_us) {
        return false;
    }
    if (timestamp < state.sampled_at_us ||
        (state.sampled_at_us > 0 && timestamp - state.sampled_at_us > 250000)) {
        suspend_companion_shake(state, now_us);
    }
    float acceleration[3] = {sample.acceleration_mps2[Axis::X],
        sample.acceleration_mps2[Axis::Y], sample.acceleration_mps2[Axis::Z]};
    for (float value : acceleration) {
        if (!std::isfinite(value)) {
            ESP_LOGE("CODY_gesture", "Navigation rejected non-finite canonical IMU sample");
            suspend_companion_shake(state, now_us);
            return false;
        }
    }
    state.sampled_at_us = timestamp;
    if (!state.baseline_valid) {
        for (uint8_t axis = 0; axis < 3; ++axis) {
            state.baseline[axis] = acceleration[axis];
        }
        state.baseline_valid = true;
        state.calm_since_us = timestamp;
        return false;
    }
    float dynamic[3]{};
    float magnitude_squared = 0;
    for (uint8_t axis = 0; axis < 3; ++axis) {
        dynamic[axis] = acceleration[axis] - state.baseline[axis];
        magnitude_squared += dynamic[axis] * dynamic[axis];
        state.baseline[axis] += .08f * dynamic[axis];
    }
    // Thresholds are deliberate initial parameters, not physically calibrated claims.
    if (magnitude_squared < 4.f) {
        if (state.calm_since_us == 0) {
            state.calm_since_us = timestamp;
        }
        if (timestamp - state.calm_since_us >= 500000 && now_us >= state.blocked_until_us) {
            state.armed = true;
            state.alternating_peaks = 0;
        }
    } else {
        state.calm_since_us = 0;
    }
    if (magnitude_squared < 16.f) {
        state.above_threshold = false;
    }
    if (!state.armed || magnitude_squared < 49.f || now_us < state.blocked_until_us) {
        return false;
    }
    bool opposing_peak = false;
    if (state.alternating_peaks != 0) {
        float dot = 0;
        float previous_squared = 0;
        for (uint8_t axis = 0; axis < 3; ++axis) {
            dot += dynamic[axis] * state.previous_peak[axis];
            previous_squared += state.previous_peak[axis] * state.previous_peak[axis];
        }
        opposing_peak = dot < -.35f * std::sqrt(previous_squared * magnitude_squared);
    }
    // Discrete canonical samples can measure opposite strong vectors without
    // ever measuring their intervening zero crossing.
    // Preserve same-direction hysteresis, but admit a genuinely measured reversal.
    if (state.above_threshold && !opposing_peak) {
        return false;
    }
    state.above_threshold = true;
    if (state.alternating_peaks == 0 || timestamp - state.burst_started_us > 900000) {
        state.burst_started_us = timestamp;
        state.alternating_peaks = 1;
    } else if (opposing_peak) {
        ++state.alternating_peaks;
    } else {
        return false;
    }
    for (uint8_t axis = 0; axis < 3; ++axis) {
        state.previous_peak[axis] = dynamic[axis];
    }
    if (state.alternating_peaks < 3) {
        return false;
    }
    state.armed = false;
    state.alternating_peaks = 0;
    state.calm_since_us = 0;
    state.blocked_until_us = now_us + 1500000;
    return true;
}

} // namespace CODY
