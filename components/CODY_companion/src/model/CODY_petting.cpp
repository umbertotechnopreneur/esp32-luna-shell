#include "CODY_petting.hpp"

#include <algorithm>
#include <cmath>
#include "CODY_response.hpp"
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_petting";

// scene: Fully displayed rig whose body polygon and face anchors define the zones.
// point: Logical acquired coordinate.
// zone: Receives the body-contained region, excluding the eye and mouth margins.
// body_hit: Receives the actual evaluated body hit used to distinguish anatomical exclusions.
// Exception: Returns the original hit-test error or invalid evaluated anchors.
esp_err_t classify_zone(const RigScene &scene, CODY2D::Vec2 point, PettingZone &zone, bool &body_hit)
{
    const esp_err_t error = hit_test_body(scene, point, body_hit);
    if (error != ESP_OK) {
        return error;
    }
    const float left_eye_y = scene.world[to_index(PointId::EyeLeft)].y;
    const float right_eye_y = scene.world[to_index(PointId::EyeRight)].y;
    const float upper_y = std::min(left_eye_y, right_eye_y);
    const float lower_y = scene.world[to_index(PointId::Mouth)].y;
    if (!std::isfinite(scene.scale) || scene.scale <= 0 ||
        !std::isfinite(left_eye_y) || !std::isfinite(right_eye_y) || !std::isfinite(lower_y)) {
        ESP_LOGE(kTag, "Face anchors rejected: left_y=%g right_y=%g mouth_y=%g scale=%g (%s)",
                 static_cast<double>(left_eye_y), static_cast<double>(right_eye_y),
                 static_cast<double>(lower_y), static_cast<double>(scene.scale),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    zone = PettingZone::None;
    if (body_hit && point.y <= upper_y - scene.scale * PettingConfig::upper_margin_per_scale) {
        zone = PettingZone::AboveEyes;
    } else if (body_hit && point.y >= lower_y + scene.scale * PettingConfig::lower_margin_per_scale) {
        zone = PettingZone::BelowMouth;
    }
    return ESP_OK;
}

// first: First logical position.
// second: Second logical position.
float distance(CODY2D::Vec2 first, CODY2D::Vec2 second)
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

// state: Caller-owned contact whose first rejection remains frozen until release or cancellation.
// rejection: First actual violated rule, in the recognizer's original evaluation order.
// point: Original acquired logical position at that rule violation.
// sampled_at_us: Original acquisition timestamp, including a rejected nonmonotonic value.
// sample_gap_us: Actual interval from the previous processed report; zero on initial press.
// zone: Actual classification of the rejected report, distinct from the contact's original zone.
// body_hit: Actual evaluated body membership of the rejected report.
// speed_valid: Whether the recognizer evaluated segment speed before this rejection.
// speed: Measured accumulated-segment speed when valid; never synthesized for earlier rules.
void reject_contact(PettingState &state, PettingRejection rejection, CODY2D::Vec2 point,
                    int64_t sampled_at_us, int64_t sample_gap_us, PettingZone zone,
                    bool body_hit, bool speed_valid = false, float speed = 0)
{
    if (state.rejected) {
        return;
    }
    state.rejected = true;
    state.rejection = rejection;
    state.rejected_at_us = sampled_at_us;
    state.rejected_point = point;
    state.rejected_sample_gap_us = sample_gap_us;
    state.rejected_zone = zone;
    state.rejected_body_hit = body_hit;
    state.rejected_speed_valid = speed_valid;
    state.rejected_speed_pixels_per_second = speed;
}
} // namespace

// rejection: Typed diagnostic identity; invalid enum values are distinguishable from no rejection.
const char *petting_rejection_name(PettingRejection rejection)
{
    switch (rejection) {
    case PettingRejection::None: return "None";
    case PettingRejection::OutsideBody: return "OutsideBody";
    case PettingRejection::FaceBand: return "FaceBand";
    case PettingRejection::Cooldown: return "Cooldown";
    case PettingRejection::NonMonotonic: return "NonMonotonic";
    case PettingRejection::SampleGap: return "SampleGap";
    case PettingRejection::MaxDuration: return "MaxDuration";
    case PettingRejection::ZoneChanged: return "ZoneChanged";
    case PettingRejection::TooFast: return "TooFast";
    }
    return "Invalid";
}

// state: Gesture state to cancel while retaining the last accepted cooldown.
void cancel_petting(PettingState &state)
{
    const int64_t last_triggered_at_us = state.last_triggered_at_us;
    state = {};
    state.last_triggered_at_us = last_triggered_at_us;
}

// scene: Last fully displayed geometry in logical touch coordinates.
// point: Successfully acquired logical coordinate.
// pressed: Real acquired contact state.
// pressed_edge: First acquired report of this contact.
// sampled_at_us: Original hardware acquisition timestamp.
// state: Bounded caller-owned contact and cooldown state.
// triggered_zone: Receives at most one caress per contact.
// Exception: Returns invalid acquisition time or the real body/anchor validation error.
esp_err_t observe_petting(const RigScene &scene, CODY2D::Vec2 point, bool pressed,
                         bool pressed_edge, int64_t sampled_at_us, PettingState &state,
                         PettingZone &triggered_zone)
{
    triggered_zone = PettingZone::None;
    if (sampled_at_us <= 0) {
        ESP_LOGE(kTag, "Acquisition timestamp=%lld rejected (%s)",
                 static_cast<long long>(sampled_at_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!pressed) {
        cancel_petting(state);
        return ESP_OK;
    }
    PettingZone zone{};
    bool body_hit = false;
    const esp_err_t error = classify_zone(scene, point, zone, body_hit);
    if (error != ESP_OK) {
        return error;
    }
    if (pressed_edge) {
        cancel_petting(state);
        state.contact_active = true;
        state.zone = zone;
        state.origin = point;
        state.accepted = point;
        state.contact_scale = scene.scale;
        state.started_at_us = sampled_at_us;
        state.sampled_at_us = sampled_at_us;
        state.accepted_at_us = sampled_at_us;
        if (zone == PettingZone::None) {
            reject_contact(state, body_hit ? PettingRejection::FaceBand : PettingRejection::OutsideBody,
                           point, sampled_at_us, 0, zone, body_hit);
        } else if (state.last_triggered_at_us != 0 &&
                   sampled_at_us - state.last_triggered_at_us < PettingConfig::cooldown_us) {
            reject_contact(state, PettingRejection::Cooldown, point, sampled_at_us, 0, zone, body_hit);
        }
        return ESP_OK;
    }
    if (!state.contact_active || state.rejected || state.triggered) {
        return ESP_OK;
    }
    const int64_t elapsed_us = sampled_at_us - state.started_at_us;
    const int64_t interval_us = sampled_at_us - state.sampled_at_us;
    // Keep the original rule order: diagnostics explain the first cause without
    // changing gesture admission or evaluating a speed with an invalid interval.
    PettingRejection rejection = PettingRejection::None;
    if (interval_us <= 0) {
        rejection = PettingRejection::NonMonotonic;
    } else if (interval_us > PettingConfig::maximum_sample_gap_us) {
        rejection = PettingRejection::SampleGap;
    } else if (elapsed_us > PettingConfig::maximum_contact_us) {
        rejection = PettingRejection::MaxDuration;
    } else if (zone != state.zone) {
        if (!body_hit) {
            rejection = PettingRejection::OutsideBody;
        } else if (zone == PettingZone::None) {
            rejection = PettingRejection::FaceBand;
        } else {
            rejection = PettingRejection::ZoneChanged;
        }
    }
    if (rejection != PettingRejection::None) {
        reject_contact(state, rejection, point, sampled_at_us, interval_us, zone, body_hit);
        return ESP_OK;
    }
    const float accepted_segment = distance(point, state.accepted);
    // Measure speed across the same accumulated displacement used by the jitter filter.
    // Otherwise many subpixel reports could disguise a rapid swipe as a slow stroke.
    const int64_t accepted_interval_us = sampled_at_us - state.accepted_at_us;
    const float speed = accepted_segment * 1000000.f / static_cast<float>(accepted_interval_us);
    if (accepted_segment > PettingConfig::jitter_pixels &&
        speed > state.contact_scale * PettingConfig::maximum_speed_per_scale) {
        reject_contact(state, PettingRejection::TooFast, point, sampled_at_us,
                       interval_us, zone, body_hit, true, speed);
        return ESP_OK;
    }
    state.sampled_at_us = sampled_at_us;
    if (accepted_segment > PettingConfig::jitter_pixels) {
        state.path_pixels += accepted_segment;
        state.accepted = point;
        state.accepted_at_us = sampled_at_us;
    }
    const float displacement = distance(point, state.origin);
    if (elapsed_us >= PettingConfig::minimum_contact_us &&
        state.path_pixels >= state.contact_scale * PettingConfig::minimum_path_per_scale &&
        displacement >= state.contact_scale * PettingConfig::minimum_displacement_per_scale) {
        state.triggered = true;
        state.last_triggered_at_us = sampled_at_us;
        triggered_zone = state.zone;
    }
    return ESP_OK;
}

} // namespace CODY
