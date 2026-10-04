#pragma once

#include <cstdint>
#include "esp_err.h"

namespace CODY {

struct HappinessConfig {
    uint16_t minimum;
    uint16_t maximum;
    uint16_t initial;
    uint16_t caress_gain;
    uint16_t decay_points;
    int64_t decay_interval_us;
};

// Session-only artistic state. Sadness is derived and never independently stored.
struct HappinessState {
    uint16_t happiness = 0;
    int64_t decay_remainder_us = 0;
};

struct HappinessSnapshot {
    uint16_t happiness = 0;
    uint16_t sadness = 0;
    int64_t decay_remainder_us = 0;
};

// config: Complete authored bounds, gains and monotonic decay interval.
// Exception: Returns invalid configuration with the rejected values logged.
esp_err_t validate_happiness_config(const HappinessConfig &config);

// config: Authored configuration; absence or invalid values are never repaired.
// state: Receives the initial session value only after successful validation.
// Exception: Returns the original authored configuration error.
esp_err_t initialize_happiness(const HappinessConfig &config, HappinessState &state);

// config: Valid authored arithmetic domain and decay rule.
// elapsed_us: Awake monotonic duration supplied by the single character owner.
// state: Receives bounded decay and retained sub-interval remainder on success only.
// Exception: Returns invalid config, elapsed duration or existing state without mutation.
esp_err_t advance_happiness(const HappinessConfig &config, int64_t elapsed_us, HappinessState &state);

// config: Valid authored bounds and gain per recognized caress.
// state: Receives the bounded gain while retaining its partial decay interval.
// Exception: Returns invalid config or existing state without mutation.
esp_err_t apply_happiness_caress(const HappinessConfig &config, HappinessState &state);

// snapshot: Receives the owner state and derived sadness under the display/state lock.
// Exception: Returns the actual lock/consumer-state error; excluded profiles return not-supported.
esp_err_t get_happiness(HappinessSnapshot &snapshot);

} // namespace CODY
