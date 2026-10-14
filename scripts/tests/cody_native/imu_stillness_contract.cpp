#include "LUNASHELL_imu_stillness.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include <cassert>
#include <limits>

namespace {
using namespace LUNASHELL;
using namespace LUNASHELL::imu_stillness;
constexpr float kRest[3] = {0.0F, 0.0F, 9.80665F};
constexpr float kQuietGyro[3] = {0.2F, -0.1F, 0.1F};

// No parameters: absent optional bias calibration or an unavailable HAR engine is not missing IMU data.
void verify_uncalibrated_measurements()
{
    ImuSnapshot measured;
    measured.calibrated = false;
    measured.data_valid = true;
    measured.last_error = ESP_OK;
    measured.gesture_acceleration_valid = true;
    measured.gesture_acceleration_mps2[2] = 9.80665F;
    ActivitySnapshot activity; // The classifier is disabled, with no measured scores.
    State detector;
    for (int tick = 0; tick <= 30; ++tick) {
        measured.gesture_sampled_at_us = 1000000 + tick * 50000;
        assert(accept(detector, measured.gesture_acceleration_mps2, kQuietGyro,
                      measured.gesture_sampled_at_us, true) == ESP_OK);
    }
    activity.stillness = detector.observation;
    assert(!measured.calibrated && activity.rejection == ActivityRejection::Disabled);
    assert(!activity.scores_valid && activity.stillness.state == ImuStillnessState::Still);
    activity.rejection = ActivityRejection::ModelError;
    activity.error = ESP_FAIL;
    assert(accept(detector, measured.gesture_acceleration_mps2, kQuietGyro, 2550000, true) == ESP_OK);
    activity.stillness = detector.observation;
    assert(activity.error == ESP_FAIL && activity.stillness.state == ImuStillnessState::Still);
    // An actual calibration operation, unlike an absent bias, retires the old hold.
    assert(accept(detector, measured.gesture_acceleration_mps2, kQuietGyro, 2600000, false) == ESP_ERR_INVALID_STATE);
    assert(!detector.observation.data_valid && detector.observation.still_since_us == 0);
    measured.calibrated = true;
    assert(accept(detector, measured.gesture_acceleration_mps2, kQuietGyro, 2650000, true) == ESP_OK);
    assert(detector.observation.state == ImuStillnessState::Collecting);
    assert(detector.observation.sample_count == 1);
}

// state: Actual portable detector receiving explicit synthetic fixture samples only.
// start_us: First strictly positive measurement timestamp.
// step_us: Real fixture cadence; no interpolation is used by the production policy.
void confirm_rest(State &state, int64_t start_us = 1000000, int64_t step_us = 100000)
{
    for (int64_t elapsed = 0; elapsed <= kStableHoldUs; elapsed += step_us) {
        assert(accept(state, kRest, kQuietGyro, start_us + elapsed, true) == ESP_OK);
        if (elapsed < kStableHoldUs) {
            assert(state.observation.state == ImuStillnessState::Collecting);
        }
    }
    assert(state.observation.state == ImuStillnessState::Still);
    assert(state.observation.data_valid && state.observation.error == ESP_OK);
    assert(state.observation.still_since_us == start_us);
}

// No parameters: measured gravity and gyro must both remain within conservative bounds.
void verify_motion_and_orientation()
{
    State state;
    confirm_rest(state);
    const float spinning[3] = {2.0F, 2.0F, 2.0F}; // Each axis is below 3, norm is above 3.
    assert(accept(state, kRest, spinning, 2600000, true) == ESP_OK);
    assert(state.observation.state == ImuStillnessState::Moving);
    confirm_rest(state, 2700000);
    const float impulse[3] = {0.0F, 0.0F, 11.0F};
    assert(accept(state, impulse, kQuietGyro, 4300000, true) == ESP_OK);
    assert(state.observation.state == ImuStillnessState::Moving);
    const float falling[3] = {0.0F, 0.0F, 0.0F};
    assert(accept(state, falling, kQuietGyro, 4400000, true) == ESP_OK);
    assert(state.observation.state == ImuStillnessState::Moving);
    State sideways;
    const float gravity_x[3] = {-9.80665F, 0.0F, 0.0F};
    for (int tick = 0; tick <= 30; ++tick) {
        assert(accept(sideways, gravity_x, kQuietGyro, 1000000 + tick * 50000, true) == ESP_OK);
    }
    assert(sideways.observation.state == ImuStillnessState::Still);
    State drift;
    for (int tick = 0; tick < 18; ++tick) {
        const float slowly_changing[3] = {tick * 0.021F, 0.0F, 9.80665F};
        assert(accept(drift, slowly_changing, kQuietGyro, 1000000 + tick * 100000, true) == ESP_OK);
    }
    assert(drift.observation.state == ImuStillnessState::Moving);
    assert(drift.observation.still_since_us == 0);
}

// No parameters: neither fast duplicate bursts nor sparse/lost samples prove stillness.
void verify_time_and_failures()
{
    State fast;
    for (int tick = 0; tick < 100; ++tick) {
        assert(accept(fast, kRest, kQuietGyro, 1000000 + tick * 1000, true) == ESP_OK);
    }
    assert(fast.observation.state == ImuStillnessState::Collecting);
    State sparse;
    for (int tick = 0; tick <= 8; ++tick) {
        assert(accept(sparse, kRest, kQuietGyro, 1000000 + tick * 200000, true) == ESP_OK);
    }
    assert(sparse.observation.state == ImuStillnessState::Collecting);
    State state;
    confirm_rest(state);
    assert(accept(state, kRest, kQuietGyro, 2500000, true) == ESP_ERR_INVALID_RESPONSE);
    assert(!state.observation.data_valid && state.observation.sample_count == 0);
    assert(state.observation.sampled_at_us == 2500000);
    assert(accept(state, kRest, kQuietGyro, 2400000, true) == ESP_ERR_INVALID_RESPONSE);
    confirm_rest(state, 2600000);
    assert(accept(state, kRest, kQuietGyro, 4300001, true) == ESP_ERR_TIMEOUT);
    assert(!state.observation.data_valid && state.observation.sampled_at_us == 4100000);
    confirm_rest(state, 4400000);
    invalidate(state, ESP_FAIL, 5950000);
    assert(state.observation.error == ESP_FAIL && state.observation.sampled_at_us == 5900000);
    assert(!state.observation.data_valid && state.observation.still_since_us == 0);
    confirm_rest(state, 6000000);
    assert(accept(state, kRest, kQuietGyro, 7600000, false) == ESP_ERR_INVALID_STATE);
    assert(!state.observation.data_valid && state.observation.sampled_at_us == 7500000);
    confirm_rest(state, 7700000);
    const float nonfinite[3] = {0.0F, std::numeric_limits<float>::quiet_NaN(), 9.80665F};
    assert(accept(state, nonfinite, kQuietGyro, 9300000, true) == ESP_ERR_INVALID_RESPONSE);
    assert(!state.observation.data_valid);
    const float overflow[3] = {std::numeric_limits<float>::max(), 0.0F, 0.0F};
    assert(accept(state, kRest, overflow, 9400000, true) == ESP_ERR_INVALID_RESPONSE);
    State aging;
    confirm_rest(aging);
    age(aging.observation, 2700000);
    assert(aging.observation.state == ImuStillnessState::Still);
    age(aging.observation, 2700001);
    assert(!aging.observation.data_valid && aging.observation.error == ESP_ERR_TIMEOUT);
    assert(aging.observation.sampled_at_us == 2500000 && aging.observation.attempted_at_us == 2500000);
    confirm_rest(aging, 2800000);
    age(aging.observation, 4200000); // Future samples cannot be made current by a reversed clock.
    assert(!aging.observation.data_valid);
}
}

// No parameters: production policy runs on explicit synthetic inputs without hardware access.
// Exception: Assertions reject loss of duration, gravity, norm, cadence or source-error boundaries.
int main()
{
    verify_uncalibrated_measurements();
    verify_motion_and_orientation();
    verify_time_and_failures();
    return 0;
}
