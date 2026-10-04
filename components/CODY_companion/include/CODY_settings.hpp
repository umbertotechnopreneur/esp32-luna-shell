#pragma once
#include "esp_err.h"

namespace CODY {
struct OnboardingSettings {
    bool persisted_first_boot = false;
    bool force_first_boot = false;
    bool effective_first_boot = false;
};

// settings: Receives persisted/default/explicit-development decisions on success only.
esp_err_t load_onboarding_settings(OnboardingSettings &settings);

// first_boot: Explicit user preference; greeting completion never writes this key.
// Exception: Returns the original write/readback error or ESP_ERR_INVALID_RESPONSE for a mismatched value.
esp_err_t persist_first_boot(bool first_boot);
} // namespace CODY
