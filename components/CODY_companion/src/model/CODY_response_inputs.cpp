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
// clock: Optional caller-owned clock projection from that same locked snapshot.
// Luna services are the only samplers. This adapter never calls refresh or a driver.
// Exception: Returns the original snapshot read or ambiguous observation error.
esp_err_t observe_response_inputs(ResponseInputs &inputs, LUNASHELL::ClockSnapshot *clock)
{
    LUNASHELL::MotionPowerObservation observation;
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(observation);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Canonical snapshot observation failed (%s)", esp_err_to_name(error));
        return error;
    }
    ResponseInputs result{};
    result.rendering_rotation_degrees = observation.rendering_rotation_degrees;
    result.last_interaction_at_us = observation.touch_last_activity_at_us;
    const auto &body_touch = observation.character_touch;
    result.body_touch.stamp = published_stamp(body_touch, true);
    if (result.body_touch.stamp.health == SampleHealth::Current) {
        result.body_touch.pressed = body_touch.pressed;
        result.body_touch.displacement_px = {
            static_cast<float>(body_touch.x) - body_touch.origin_x,
            static_cast<float>(body_touch.y) - body_touch.origin_y};
        result.body_touch.rendering_rotation_degrees = body_touch.rendering_rotation_degrees;
        result.body_touch.gesture_started_at_us = body_touch.gesture_started_at_us;
    }
    // The current wake-word trial has one consumer: the standby power owner.
    // An active-device detection must not alter Cody's idle or animation inputs.
    result.eye_tap.stamp = published_stamp(observation.character_interaction, true);
    if (result.eye_tap.stamp.health == SampleHealth::Current) {
        using Kind = LUNASHELL::CharacterInteractionSnapshot::Kind;
        switch (observation.character_interaction.kind) {
        case Kind::EyeLeftTap:
            result.eye_tap.eye = EyeId::Left;
            break;
        case Kind::EyeRightTap:
            result.eye_tap.eye = EyeId::Right;
            break;
        default:
            ESP_LOGE(kTag, "Character interaction identity rejected: %s", esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    const auto &audio = observation.audio;
    result.playback.cue_id = audio.playback.cue_id;
    result.microphone.stamp = published_stamp(audio.microphone, audio.channel_count > 0);
    result.playback.stamp = published_stamp(audio.playback, true);
    if (result.microphone.stamp.health == SampleHealth::Current) {
        result.microphone.channel_count = audio.channel_count;
        for (uint8_t channel = 0; channel < 2; ++channel) {
            result.microphone.mean_level[channel] = audio.channel_mean_level[channel];
            result.microphone.peak_level[channel] = audio.channel_peak_level[channel];
        }
    }
    if (result.playback.stamp.health == SampleHealth::Current) {
        if (audio.playback.state != LUNASHELL::PlaybackState::Idle &&
            audio.playback.state != LUNASHELL::PlaybackState::Playing) {
            ESP_LOGE(kTag, "Playback snapshot contains an ambiguous current state");
            return ESP_ERR_INVALID_RESPONSE;
        }
        result.playback.active = audio.playback.state == LUNASHELL::PlaybackState::Playing;
        if (result.playback.active && !audio.playback.level_valid) {
            ESP_LOGE(kTag, "Active playback snapshot has no successfully written PCM level");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (result.playback.active) {
            result.playback.mean_level = audio.playback.mean_level;
        }
    }
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
    if (clock != nullptr) {
        *clock = observation.clock;
    }
    return ESP_OK;
}
} // namespace CODY
