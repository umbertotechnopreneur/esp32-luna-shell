#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class ParallaxLevel : uint8_t { Off, Low, Medium, High };

// Display-owner state only: no acquisition, extra polling or hardware cache.
struct ParallaxState {
    float filtered_x = 0;
    float filtered_y = 0;
    int64_t sampled_at_us = 0;
    int8_t x = 0;
    int8_t y = 0;
    bool primed = false;
};
static_assert(sizeof(ParallaxState) <= 64, "Parallax state must remain bounded");

// state: Display-owner filter and last quantized decoration offset.
// imu: Already acquired canonical IMU observation, never read by this helper.
// observation_error: Original result from the owner's snapshot read.
// rotation: Current admitted display orientation in degrees.
// active: True only on explicitly permitted awake visual surfaces.
// level: Off, Low, Medium or High; maximum offsets are 0, 2, 4 and 6 pixels.
// now_us: Current monotonic time, used by the existing freshness gate.
// Returns true only if the quantized output changed and damage is required.
// Exception: Invalid levels or measurements disable the effect without inventing motion.
inline bool update_parallax(ParallaxState &state, const LUNASHELL::ImuSnapshot &imu,
    esp_err_t observation_error, uint16_t rotation, bool active, ParallaxLevel level, int64_t now_us)
{
    const int8_t old_x = state.x;
    const int8_t old_y = state.y;
    if (!active || level == ParallaxLevel::Off || static_cast<uint8_t>(level) > 3U ||
        observation_error != ESP_OK || imu.health != LUNASHELL::DeviceHealth::Ready ||
        !imu.data_valid || imu.data_stale || imu.last_error != ESP_OK ||
        imu.last_success_at_us <= 0 || now_us < imu.last_success_at_us ||
        now_us - imu.last_success_at_us > 500000 ||
        !std::isfinite(imu.acceleration_mps2[0]) || !std::isfinite(imu.acceleration_mps2[1]) ||
        std::fabs(imu.acceleration_mps2[0]) > 256.0f || std::fabs(imu.acceleration_mps2[1]) > 256.0f ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)) {
        state = {};
        return old_x != 0 || old_y != 0;
    }
    // Gravity gives a stable tilt instead of a temporary deviation from a
    // moving baseline. Smooth only fresh samples; do not invent IMU data.
    constexpr float kGravity = 9.80665f;
    const float target_x = std::clamp(-imu.acceleration_mps2[1] / kGravity, -1.0f, 1.0f);
    const float target_y = std::clamp(-imu.acceleration_mps2[0] / kGravity, -1.0f, 1.0f);
    if (!state.primed || imu.last_success_at_us < state.sampled_at_us ||
        imu.last_success_at_us - state.sampled_at_us > 500000) {
        state.filtered_x = target_x;
        state.filtered_y = target_y;
        state.primed = true;
    } else if (imu.last_success_at_us > state.sampled_at_us) {
        const float seconds = static_cast<float>(imu.last_success_at_us - state.sampled_at_us) / 1000000.0f;
        const float blend = seconds / (seconds + 0.15f);
        state.filtered_x += (target_x - state.filtered_x) * blend;
        state.filtered_y += (target_y - state.filtered_y) * blend;
    }
    state.sampled_at_us = imu.last_success_at_us;
    float x = state.filtered_x;
    float y = state.filtered_y;
    switch (rotation) {
    case 90: x = state.filtered_y; y = -state.filtered_x; break;
    case 180: x = -state.filtered_x; y = -state.filtered_y; break;
    case 270: x = -state.filtered_y; y = state.filtered_x; break;
    default: break;
    }
    const int limit = static_cast<int>(level) * 2;
    state.x = static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(x * limit)), -limit, limit));
    state.y = static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(y * limit)), -limit, limit));
    return old_x != state.x || old_y != state.y;
}

} // namespace CODY
