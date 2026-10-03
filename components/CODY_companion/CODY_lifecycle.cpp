#include "CODY_lifecycle.hpp"

#include <algorithm>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_lifecycle";
constexpr uint32_t kBootCalmUs = 900000;
constexpr uint32_t kBootHappyUs = 1200000;
constexpr uint32_t kBootSettlingUs = 900000;
constexpr uint32_t kSleepTransitionUs = 400000;
constexpr uint32_t kSleepClosedHoldUs = 300000;
constexpr uint32_t kWakeTransitionUs = 900000;
}

// state: Current phase and active-time clock; standby time is excluded by the caller.
// active_delta_us: Nonnegative elapsed animation time, never a sensor measurement.
// next: Receives the complete phase update only on success.
esp_err_t advance_lifecycle(const LifecycleState &state, int64_t active_delta_us, LifecycleState &next)
{
    if (active_delta_us < 0 || active_delta_us > UINT32_MAX) {
        ESP_LOGE(kTag, "Lifecycle delta outside active-frame range (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    LifecycleState result = state;
    int64_t remaining_us = active_delta_us;
    for (;;) {
        uint32_t duration_us = 0;
        LifecyclePhase following = LifecyclePhase::Idle;
        switch (result.phase) {
        case LifecyclePhase::BootCalm:
            duration_us = kBootCalmUs;
            following = LifecyclePhase::BootHappy;
            break;
        case LifecyclePhase::BootHappy:
            duration_us = kBootHappyUs;
            following = LifecyclePhase::BootSettling;
            break;
        case LifecyclePhase::BootSettling:
            duration_us = kBootSettlingUs;
            break;
        case LifecyclePhase::Wake:
            duration_us = kWakeTransitionUs;
            break;
        case LifecyclePhase::Idle:
            if (result.phase_elapsed_us != 0) {
                ESP_LOGE(kTag, "Idle lifecycle clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
                return ESP_ERR_INVALID_STATE;
            }
            next = result;
            return ESP_OK;
        case LifecyclePhase::Sleep:
            if (result.phase_elapsed_us > kSleepTransitionUs + kSleepClosedHoldUs) {
                ESP_LOGE(kTag, "Sleep lifecycle clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
                return ESP_ERR_INVALID_STATE;
            }
            // The visible hold is bounded; a long standby cannot overflow its clock.
            result.phase_elapsed_us = static_cast<uint32_t>(std::min<int64_t>(
                kSleepTransitionUs + kSleepClosedHoldUs, result.phase_elapsed_us + remaining_us));
            next = result;
            return ESP_OK;
        default:
            ESP_LOGE(kTag, "Unknown lifecycle phase rejected: %u (%s)", to_index(result.phase),
                esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        if (result.phase_elapsed_us >= duration_us) {
            ESP_LOGE(kTag, "Lifecycle phase clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        const uint32_t consumed_us = static_cast<uint32_t>(std::min<int64_t>(
            remaining_us, duration_us - result.phase_elapsed_us));
        result.phase_elapsed_us += consumed_us;
        remaining_us -= consumed_us;
        if (result.phase_elapsed_us < duration_us) {
            next = result;
            return ESP_OK;
        }
        result.phase = following;
        result.phase_elapsed_us = 0;
    }
}

// state: Current boot, idle or sleep/wake presentation.
// face: Receives the authored target without replacing the rhythmic channels.
// contour: Receives gentle independent body movement.
esp_err_t lifecycle_targets(const LifecycleState &state, Channels &face, ContourControls &contour)
{
    ExpressionId expression = ExpressionId::Calm;
    const AtlasScreenDefinition *screen = nullptr;
    esp_err_t mapping_error = ESP_OK;
    switch (state.phase) {
    case LifecyclePhase::BootCalm:
    case LifecyclePhase::BootSettling:
        break;
    case LifecyclePhase::Idle:
        mapping_error = find_atlas_screen(AtlasScreenId::Idle16, screen);
        break;
    case LifecyclePhase::BootHappy:
        expression = ExpressionId::Happy;
        break;
    case LifecyclePhase::Sleep:
        mapping_error = find_atlas_screen(AtlasScreenId::Sleep35A, screen);
        break;
    case LifecyclePhase::Wake:
        // Begin with the atlas' drowsy eyes, then recover the calm idle combination.
        mapping_error = find_atlas_screen(state.phase_elapsed_us < 300000
            ? AtlasScreenId::Wake35B : AtlasScreenId::Idle16, screen);
        break;
    default:
        ESP_LOGE(kTag, "Unknown lifecycle target rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (mapping_error != ESP_OK) {
        return mapping_error;
    }
    if (screen != nullptr) {
        expression = screen->expression;
    }
    const esp_err_t error = expression_channels(expression, face);
    if (error != ESP_OK) {
        return error;
    }
    contour = contour_defaults();
    contour[ContourControl::Amplitude] = .12f;
    contour[ContourControl::Speed] = state.phase == LifecyclePhase::Sleep ? .15f : .25f;
    return ESP_OK;
}

// state: Current sleep preparation; completion includes a visible closed-eye hold.
bool lifecycle_sleep_ready(const LifecycleState &state)
{
    return state.phase == LifecyclePhase::Sleep &&
        state.phase_elapsed_us >= kSleepTransitionUs + kSleepClosedHoldUs;
}

} // namespace CODY
