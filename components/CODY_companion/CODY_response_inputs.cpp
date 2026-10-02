#include "CODY_response.hpp"

#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_inputs";

// component: Published component health and its real error/timestamp.
// value_available: Whether this specific measurement exists in a healthy component.
SampleStamp published_stamp(const LUNASHELL::ComponentSnapshot &component, bool value_available)
{
    SampleStamp stamp{};
    stamp.error = component.last_error;
    stamp.sampled_at_us = component.last_success_at_us;
    if (component.health == LUNASHELL::DeviceHealth::Missing || component.last_attempt_at_us == 0) {
        stamp.health = SampleHealth::Unavailable;
    } else if (component.last_error != ESP_OK) {
        stamp.health = SampleHealth::Error;
    } else if (component.data_stale) {
        stamp.health = SampleHealth::Stale;
    } else if (component.health == LUNASHELL::DeviceHealth::Ready && component.data_valid && value_available) {
        stamp.health = SampleHealth::Current;
    } else {
        stamp.health = SampleHealth::Unavailable;
    }
    return stamp;
}
} // namespace

// inputs: Receives a hardware-only projection; failures preserve the caller's state.
// Luna services are the only samplers. This adapter never calls refresh or a driver.
esp_err_t observe_response_inputs(ResponseInputs &inputs)
{
    LUNASHELL::MotionPowerObservation observation;
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(observation);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Canonical snapshot observation failed (%s)", esp_err_to_name(error));
        return error;
    }
    ResponseInputs result{};
    result.rendering_rotation_degrees = observation.rendering_rotation_degrees;
    result.motion.stamp = published_stamp(observation.imu, true);
    result.power.stamp = published_stamp(observation.battery,
                                         observation.battery.battery_present && observation.battery.level_percent_valid);
    if (result.motion.stamp.health == SampleHealth::Current) {
        // This array is the existing driver API boundary, not an authored identity catalog.
        result.motion.acceleration_mps2[Axis::X] = observation.imu.acceleration_mps2[0];
        result.motion.acceleration_mps2[Axis::Y] = observation.imu.acceleration_mps2[1];
        result.motion.acceleration_mps2[Axis::Z] = observation.imu.acceleration_mps2[2];
    }
    if (result.power.stamp.health == SampleHealth::Current) {
        result.power.level_percent = observation.battery.level_percent;
        const char *power_state = observation.battery.power_state;
        if (std::strcmp(power_state, "charging") == 0) {
            result.power.charging = true;
        } else if (std::strcmp(power_state, "discharging") != 0 && std::strcmp(power_state, "standby") != 0) {
            ESP_LOGE(kTag, "power.charge: rejected published power_state '%s' (%s)",
                     power_state, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    inputs = result;
    return ESP_OK;
}
} // namespace CODY
