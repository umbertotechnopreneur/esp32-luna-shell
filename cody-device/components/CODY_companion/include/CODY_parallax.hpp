#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "CODY_pixel_demo.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class ParallaxLevel : uint8_t { Off, Low, Medium, High };

// Display-owner state only: no acquisition, extra polling or hardware cache.
struct ParallaxState {
    PixelDemoMotionState motion{};
    int8_t x = 0;
    int8_t y = 0;
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
        !imu.data_valid || imu.data_stale || imu.last_error != ESP_OK) {
        state = {};
        return old_x != 0 || old_y != 0;
    }
    PixelDemoMotion sample{};
    sample.valid = true;
    sample.acceleration_x = imu.acceleration_mps2[0];
    sample.acceleration_y = imu.acceleration_mps2[1];
    sample.sampled_at_us = imu.last_success_at_us;
    sample.rendering_rotation_degrees = rotation;
    sample.angular_velocity_x = imu.angular_velocity_dps[0];
    sample.angular_velocity_y = imu.angular_velocity_dps[1];
    sample.angular_velocity_z = imu.angular_velocity_dps[2];
    sample.angular_valid = std::isfinite(sample.angular_velocity_x) &&
        std::isfinite(sample.angular_velocity_y) && std::isfinite(sample.angular_velocity_z);
    advance_pixel_demo_motion(state.motion, sample, now_us);
    float x = state.motion.offset_x;
    float y = state.motion.offset_y;
    switch (state.motion.rendering_rotation_degrees) {
    case 90: x = state.motion.offset_y; y = -state.motion.offset_x; break;
    case 180: x = -state.motion.offset_x; y = -state.motion.offset_y; break;
    case 270: x = -state.motion.offset_y; y = state.motion.offset_x; break;
    default: break;
    }
    const int limit = static_cast<int>(level) * 2;
    const float scale = static_cast<float>(limit) / 4.0f;
    state.x = static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(x * scale)), -limit, limit));
    state.y = static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(y * scale)), -limit, limit));
    return old_x != state.x || old_y != state.y;
}

} // namespace CODY
