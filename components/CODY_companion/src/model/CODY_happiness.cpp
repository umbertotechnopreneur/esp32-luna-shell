#include "CODY_happiness.hpp"

#include <algorithm>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_happiness";

// config: Previously validated authored bounds and decay interval.
// state: Existing artistic state; invalid values cannot be silently substituted.
// Exception: Returns invalid state with the actual rejected value and remainder logged.
esp_err_t validate_state(const HappinessConfig &config, const HappinessState &state)
{
    if (state.happiness < config.minimum || state.happiness > config.maximum ||
        state.decay_remainder_us < 0 || state.decay_remainder_us >= config.decay_interval_us) {
        ESP_LOGE(kTag, "State rejected: happiness=%u remainder_us=%lld (%s)",
                 static_cast<unsigned>(state.happiness),
                 static_cast<long long>(state.decay_remainder_us), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
} // namespace

// config: Complete authored bounds, initial state, gain and decay cadence.
// Exception: Returns invalid configuration without defaulting any rejected value.
esp_err_t validate_happiness_config(const HappinessConfig &config)
{
    if (config.minimum != 0 || config.maximum != 100 ||
        config.initial < config.minimum || config.initial > config.maximum ||
        config.caress_gain == 0 || config.caress_gain > config.maximum - config.minimum ||
        config.decay_points == 0 || config.decay_points > config.maximum - config.minimum ||
        config.decay_interval_us < 1000000 || config.decay_interval_us > 86400000000LL ||
        config.decay_interval_us % 1000000 != 0) {
        ESP_LOGE(kTag, "Config rejected: min=%u max=%u initial=%u gain=%u decay=%u interval_us=%lld (%s)",
                 static_cast<unsigned>(config.minimum), static_cast<unsigned>(config.maximum),
                 static_cast<unsigned>(config.initial), static_cast<unsigned>(config.caress_gain),
                 static_cast<unsigned>(config.decay_points), static_cast<long long>(config.decay_interval_us),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// config: Authored session initialization, explicitly validated first.
// state: Receives the initial value and an empty decay remainder on success only.
// Exception: Returns the original configuration failure.
esp_err_t initialize_happiness(const HappinessConfig &config, HappinessState &state)
{
    const esp_err_t error = validate_happiness_config(config);
    if (error != ESP_OK) {
        return error;
    }
    state = {config.initial, 0};
    return ESP_OK;
}

// config: Authored domain and decay rule.
// elapsed_us: Nonnegative duration spent awake; standby is excluded by the owner.
// state: Receives the new happiness and remainder only after validation.
// Exception: Returns the actual invalid config, elapsed duration or state error.
esp_err_t advance_happiness(const HappinessConfig &config, int64_t elapsed_us, HappinessState &state)
{
    esp_err_t error = validate_happiness_config(config);
    if (error != ESP_OK) {
        return error;
    }
    error = validate_state(config, state);
    if (error != ESP_OK) {
        return error;
    }
    if (elapsed_us < 0) {
        ESP_LOGE(kTag, "Elapsed_us=%lld rejected (%s)", static_cast<long long>(elapsed_us),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    int64_t full_intervals = elapsed_us / config.decay_interval_us;
    const int64_t partial_interval = elapsed_us % config.decay_interval_us;
    const int64_t until_next_interval = config.decay_interval_us - state.decay_remainder_us;
    int64_t remainder = 0;
    if (partial_interval >= until_next_interval) {
        ++full_intervals;
        remainder = partial_interval - until_next_interval;
    } else {
        remainder = state.decay_remainder_us + partial_interval;
    }
    const uint32_t above_floor = state.happiness - config.minimum;
    const uint32_t intervals_to_floor = (above_floor + config.decay_points - 1U) / config.decay_points;
    if (full_intervals >= static_cast<int64_t>(intervals_to_floor)) {
        state.happiness = config.minimum;
        // Full unpaid decay is discarded at the floor; only the bounded minute phase survives.
        state.decay_remainder_us = remainder;
    } else {
        // This product is bounded by the distance to the floor, even for a huge elapsed interval.
        state.happiness = static_cast<uint16_t>(state.happiness - full_intervals * config.decay_points);
        state.decay_remainder_us = remainder;
    }
    return ESP_OK;
}

// config: Authored domain and caress gain.
// state: Receives saturated arithmetic without resetting its existing minute remainder.
// Exception: Returns original configuration or state validation error.
esp_err_t apply_happiness_caress(const HappinessConfig &config, HappinessState &state)
{
    esp_err_t error = validate_happiness_config(config);
    if (error != ESP_OK) {
        return error;
    }
    error = validate_state(config, state);
    if (error != ESP_OK) {
        return error;
    }
    const uint32_t increased = static_cast<uint32_t>(state.happiness) + config.caress_gain;
    state.happiness = static_cast<uint16_t>(std::min(increased, static_cast<uint32_t>(config.maximum)));
    return ESP_OK;
}

} // namespace CODY
