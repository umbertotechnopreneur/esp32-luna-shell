#pragma once
#include "LUNASHELL_activity.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace LUNASHELL::imu_stillness {
inline constexpr int64_t kStableHoldUs = 1500000;
inline constexpr uint32_t kMinimumSamples = 16;
inline constexpr float kMaximumGyroDps = 3.0F;
inline constexpr float kGravityMps2 = 9.80665F;
inline constexpr float kGravityToleranceMps2 = 0.5F;
inline constexpr float kMaximumAxisSpanMps2 = 0.35F;
struct State {
    ImuStillnessSnapshot observation;
    int64_t candidate_since_us = 0;
    float minimum_acceleration[3]{};
    float maximum_acceleration[3]{};
};
static_assert(sizeof(State) <= 80, "Stillness detector has no sample window or heap allocation");

// destination: Canonical activity owner, including its independently acquired stillness.
// result: Complete classifier-only result; its default stillness never replaces measurements.
inline void publish_classifier_result(ActivitySnapshot &destination, const ActivitySnapshot &result)
{
    const ImuStillnessSnapshot retained = destination.stillness;
    destination = result;
    destination.stillness = retained;
}

// destination: Current canonical activity, potentially updated by the inference worker.
// staged: Acquisition stage with independently ordered classifier and stillness observations.
inline void merge_activity(ActivitySnapshot &destination, const ActivitySnapshot &staged)
{
    if (staged.inferred_at_us > destination.inferred_at_us) {
        publish_classifier_result(destination, staged);
    }
    // A stage lives for one refresh, never half the sequence space. Comparing
    // serial numbers preserves late failures without inventing newer timestamps.
    const uint32_t distance = staged.stillness.sequence - destination.stillness.sequence;
    if (staged.stillness.sequence != 0 && (destination.stillness.sequence == 0 ||
        (distance != 0 && distance < 0x80000000U))) {
        destination.stillness = staged.stillness;
    }
}

// state: Acquisition-owned detector, serialized with its canonical publication.
// error: Original read/transition failure or explicit unavailable-state cause.
// attempted_at_us: Original failed-attempt/transition timestamp, never a new measurement.
inline void invalidate(State &state, esp_err_t error, int64_t attempted_at_us)
{
    if (++state.observation.sequence == 0) {
        ++state.observation.sequence;
    }
    state.observation.state = ImuStillnessState::Unavailable;
    state.observation.data_valid = false;
    state.observation.error = error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    state.observation.attempted_at_us = attempted_at_us;
    state.observation.still_since_us = 0;
    state.observation.sample_count = 0;
    state.candidate_since_us = 0;
}

// observation: Canonical bounded projection whose source timestamps remain unchanged.
// now_us: Actual monotonic observation time; missing samples can only invalidate.
inline void age(ImuStillnessSnapshot &observation, int64_t now_us)
{
    if (observation.data_valid && (observation.sampled_at_us <= 0 ||
        now_us < observation.sampled_at_us ||
        now_us - observation.sampled_at_us > kImuStillnessMaximumGapUs)) {
        observation.state = ImuStillnessState::Unavailable;
        observation.data_valid = false;
        observation.error = ESP_ERR_TIMEOUT;
        observation.still_since_us = 0;
        observation.sample_count = 0;
    }
}

// state: Small acquisition-owned state, never shared without its owner lock.
// acceleration: Real acceleration before smoothing, in m/s2; admitted bias only when available.
// angular_velocity: Same unsmoothed gyro sample in degrees/s, with admitted bias when available.
// sampled_at_us: Unique original successful acquisition timestamp.
// eligible: Active sampling outside a calibration operation; no prior calibration is required.
// Exception: Returns invalid state, nonfinite/repeated timestamp or actual cadence failure.
inline esp_err_t accept(State &state, const float (&acceleration)[3],
                        const float (&angular_velocity)[3], int64_t sampled_at_us, bool eligible)
{
    const int64_t previous_us = std::max(state.observation.attempted_at_us, state.observation.sampled_at_us);
    if (!eligible || sampled_at_us <= 0) {
        invalidate(state, ESP_ERR_INVALID_STATE, sampled_at_us);
        return ESP_ERR_INVALID_STATE;
    }
    if (previous_us > 0 && sampled_at_us <= previous_us) {
        invalidate(state, ESP_ERR_INVALID_RESPONSE, previous_us);
        return ESP_ERR_INVALID_RESPONSE;
    }
    float acceleration_squared = 0.0F;
    float gyro_squared = 0.0F;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(acceleration[axis]) || !std::isfinite(angular_velocity[axis])) {
            invalidate(state, ESP_ERR_INVALID_RESPONSE, sampled_at_us);
            return ESP_ERR_INVALID_RESPONSE;
        }
        acceleration_squared += acceleration[axis] * acceleration[axis];
        gyro_squared += angular_velocity[axis] * angular_velocity[axis];
    }
    if (!std::isfinite(acceleration_squared) || !std::isfinite(gyro_squared)) {
        invalidate(state, ESP_ERR_INVALID_RESPONSE, sampled_at_us);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (state.observation.data_valid && sampled_at_us - previous_us > kImuStillnessMaximumGapUs) {
        invalidate(state, ESP_ERR_TIMEOUT, sampled_at_us);
        return ESP_ERR_TIMEOUT;
    }
    if (!state.observation.data_valid) {
        state.candidate_since_us = 0;
        state.observation.sample_count = 0;
    }
    state.observation.sampled_at_us = sampled_at_us;
    if (++state.observation.sequence == 0) {
        ++state.observation.sequence;
    }
    state.observation.attempted_at_us = sampled_at_us;
    state.observation.data_valid = true;
    state.observation.error = ESP_OK;
    constexpr float minimum_gravity = kGravityMps2 - kGravityToleranceMps2;
    constexpr float maximum_gravity = kGravityMps2 + kGravityToleranceMps2;
    bool moving = gyro_squared > kMaximumGyroDps * kMaximumGyroDps ||
        acceleration_squared < minimum_gravity * minimum_gravity ||
        acceleration_squared > maximum_gravity * maximum_gravity;
    if (state.candidate_since_us == 0) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            state.minimum_acceleration[axis] = acceleration[axis];
            state.maximum_acceleration[axis] = acceleration[axis];
        }
        state.candidate_since_us = sampled_at_us;
        state.observation.sample_count = 0;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        state.minimum_acceleration[axis] = std::min(state.minimum_acceleration[axis], acceleration[axis]);
        state.maximum_acceleration[axis] = std::max(state.maximum_acceleration[axis], acceleration[axis]);
        moving = moving || state.maximum_acceleration[axis] - state.minimum_acceleration[axis] >
            kMaximumAxisSpanMps2;
    }
    if (moving) {
        state.observation.state = ImuStillnessState::Moving;
        state.observation.still_since_us = 0;
        state.observation.sample_count = 0;
        state.candidate_since_us = 0;
        return ESP_OK;
    }
    if (state.observation.sample_count != std::numeric_limits<uint32_t>::max()) {
        ++state.observation.sample_count;
    }
    const bool confirmed = sampled_at_us - state.candidate_since_us >= kStableHoldUs &&
        state.observation.sample_count >= kMinimumSamples;
    state.observation.state = confirmed ? ImuStillnessState::Still : ImuStillnessState::Collecting;
    state.observation.still_since_us = confirmed ? state.candidate_since_us : 0;
    return ESP_OK;
}
} // namespace LUNASHELL::imu_stillness
