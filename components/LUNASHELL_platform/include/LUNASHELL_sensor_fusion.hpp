/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#pragma once

#include "esp_err.h"
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {

struct SensorFusionResult {
    bool gravity_dominant = false;
    bool stationary = false;
    float acceleration_magnitude_mps2 = 0.0F;
    float angular_speed_dps = 0.0F;
};

/**
 * Derive a bounded motion state from one valid, filtered IMU sample.
 *
 * This service deliberately does not claim pitch, roll, or yaw. The device
 * has no magnetometer and the current contract does not provide a validated
 * reference frame for orientation output.
 */
class SensorFusionService final {
public:
    static SensorFusionService &instance();

    esp_err_t process(
        const ImuSnapshot &imu,
        SensorFusionResult *result
    ) const;

private:
    SensorFusionService() = default;
};

}  // namespace LUNASHELL
