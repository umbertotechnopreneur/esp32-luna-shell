#pragma once
#include "esp_err.h"
#include <cstdint>

namespace CODY {
enum class PreferenceId : uint8_t { ShakeWake = 0, AutoRotate = 1, Sounds = 2, Orientation = 3 };

// No parameters: loads exact persisted values, creating only genuinely absent keys.
// Exception: Returns the real storage, owner-policy or publication error.
esp_err_t load_companion_preferences();

// preference: Stable identity of the explicit user choice.
// value: Boolean 0/1, or an exact manual angle 0/180 for Orientation.
// Exception: Returns the real validation, persistence, application or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value);

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
