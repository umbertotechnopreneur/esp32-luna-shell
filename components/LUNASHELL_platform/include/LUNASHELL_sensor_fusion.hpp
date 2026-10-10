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
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


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
