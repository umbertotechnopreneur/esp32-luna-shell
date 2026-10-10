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
 *
 * VBWR E */

#pragma once
#include "esp_err.h"
#include <cstdint>

namespace LUNASHELL { enum class CodyUsageMode : uint8_t; }

namespace CODY {
enum class PreferenceId : uint8_t { ShakeWake = 0, AutoRotate = 1, Sounds = 2, Orientation = 3, UsageMode = 4 };

// No parameters: loads exact persisted values, creating only genuinely absent keys.
// Exception: Returns the real storage, owner-policy or publication error.
esp_err_t load_companion_preferences();

// preference: Stable identity of the explicit user choice.
// value: Boolean/mode 0/1, or an exact manual angle 0/180 for Orientation.
// Exception: Returns the real validation, persistence, application or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value);

// mode: Explicit Desk or Away choice; changes only this durable presentation preference.
// Exception: Returns original validation, projection, persistence, readback or publication errors.
esp_err_t persist_companion_usage_mode(LUNASHELL::CodyUsageMode mode);

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
