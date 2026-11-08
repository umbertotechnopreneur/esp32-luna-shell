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


#include "LUNASHELL_sensor_fusion.hpp"

#include <cmath>

namespace LUNASHELL {
namespace {

constexpr float kGravityLowerBoundMps2 = 8.0F;
constexpr float kGravityUpperBoundMps2 = 12.0F;
constexpr float kStationaryAngularSpeedDps = 8.0F;

bool component_has_current_data(const ComponentSnapshot &component)
{
    return component.health == DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}

}  // namespace

SensorFusionService &SensorFusionService::instance()
{
    static SensorFusionService service;
    return service;
}

esp_err_t SensorFusionService::process(
    const ImuSnapshot &imu,
    SensorFusionResult *result
) const
{
    if (result == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = {};
    if (!component_has_current_data(imu)) {
        return imu.last_error != ESP_OK
            ? imu.last_error
            : ESP_ERR_INVALID_STATE;
    }

    float acceleration_squared = 0.0F;
    float angular_speed_squared = 0.0F;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float acceleration = imu.acceleration_mps2[axis];
        const float angular_speed = imu.angular_velocity_dps[axis];
        if (!std::isfinite(acceleration) || !std::isfinite(angular_speed)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        acceleration_squared += acceleration * acceleration;
        angular_speed_squared += angular_speed * angular_speed;
    }

    result->acceleration_magnitude_mps2 = std::sqrt(acceleration_squared);
    result->angular_speed_dps = std::sqrt(angular_speed_squared);
    if (!std::isfinite(result->acceleration_magnitude_mps2) ||
        !std::isfinite(result->angular_speed_dps)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    result->gravity_dominant =
        result->acceleration_magnitude_mps2 >= kGravityLowerBoundMps2 &&
        result->acceleration_magnitude_mps2 <= kGravityUpperBoundMps2;
    result->stationary =
        result->gravity_dominant &&
        result->angular_speed_dps <= kStationaryAngularSpeedDps;
    return ESP_OK;
}

}  // namespace LUNASHELL
