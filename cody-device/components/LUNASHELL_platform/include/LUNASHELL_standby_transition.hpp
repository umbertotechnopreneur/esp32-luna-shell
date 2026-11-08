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
#include <cstddef>
#include <cstdint>

namespace LUNASHELL::power_policy {

enum class StandbyStep : uint8_t {
    FullSnapshots = 0, UiSampling = 1, Gps = 2, Distance = 3, Storage = 4,
    Imu = 5, MotionWake = 6, VoiceAfe = 7, BaseAudio = 8, Wireless = 9,
    StandaloneDisplay = 10, Display = 11, Lvgl = 12,
    PrepareDarkFrame = 13, PrepareWakeFrame = 14, Count = 15
};

struct StandbyStepResult {
    esp_err_t mandatory = ESP_OK;
    esp_err_t optional = ESP_OK;
};

struct StandbySequenceResult {
    esp_err_t error = ESP_OK;
    esp_err_t storage = ESP_OK;
    esp_err_t wireless = ESP_OK;
};

using StandbyStepCallback = StandbyStepResult (*)(void *, StandbyStep, bool);
using StandbyFinalizeCallback = esp_err_t (*)(void *, bool, const StandbySequenceResult &);

// target: Whether workloads enter standby or restore their active state.
// best_effort: Continue mandatory recovery after preserving its first error.
// owner: Borrowed resource owner; hardware and synthetic owners remain separate.
// step: Executes only the named operation and reports real optional/mandatory errors.
// finalize: Checks every selected owner against the requested terminal state.
// Exception: Returns the first mandatory error; optional errors remain independently visible.
inline StandbySequenceResult execute_standby_sequence(bool target, bool best_effort,
    void *owner, StandbyStepCallback step, StandbyFinalizeCallback finalize)
{
    static constexpr StandbyStep suspend[] = {
        StandbyStep::FullSnapshots, StandbyStep::UiSampling, StandbyStep::Gps,
        StandbyStep::Distance, StandbyStep::Storage, StandbyStep::Imu,
        StandbyStep::MotionWake, StandbyStep::VoiceAfe, StandbyStep::BaseAudio,
        StandbyStep::Wireless, StandbyStep::StandaloneDisplay, StandbyStep::Display,
        StandbyStep::Lvgl
    };
    static constexpr StandbyStep resume[] = {
        StandbyStep::Lvgl, StandbyStep::MotionWake, StandbyStep::Imu,
        StandbyStep::Distance, StandbyStep::Storage, StandbyStep::BaseAudio,
        StandbyStep::VoiceAfe, StandbyStep::UiSampling, StandbyStep::Gps,
        StandbyStep::FullSnapshots, StandbyStep::Wireless, StandbyStep::PrepareDarkFrame,
        StandbyStep::StandaloneDisplay, StandbyStep::PrepareWakeFrame, StandbyStep::Display
    };
    StandbySequenceResult result;
    if (owner == nullptr || step == nullptr || finalize == nullptr) {
        result.error = ESP_ERR_INVALID_ARG;
        return result;
    }
    const StandbyStep *steps = target ? suspend : resume;
    const size_t count = target ? sizeof(suspend) / sizeof(*suspend) : sizeof(resume) / sizeof(*resume);
    for (size_t index = 0; index < count; ++index) {
        const StandbyStep operation = steps[index];
        const bool exposes_display = !target && (operation == StandbyStep::PrepareDarkFrame ||
            operation == StandbyStep::StandaloneDisplay || operation == StandbyStep::PrepareWakeFrame ||
            operation == StandbyStep::Display);
        // Producer failures keep the display dark, including best-effort rollback.
        if (result.error != ESP_OK && (!best_effort || exposes_display)) {
            continue;
        }
        const StandbyStepResult outcome = step(owner, operation, target);
        if (operation == StandbyStep::Storage && result.storage == ESP_OK) {
            result.storage = outcome.optional;
        }
        if (operation == StandbyStep::Wireless && result.wireless == ESP_OK) {
            result.wireless = outcome.optional;
        }
        if (result.error == ESP_OK && outcome.mandatory != ESP_OK) {
            result.error = outcome.mandatory;
        }
    }
    const esp_err_t final_error = finalize(owner, target, result);
    if (result.error == ESP_OK) {
        result.error = final_error;
    }
    return result;
}

enum class StandbyRecoveryState : uint8_t { Previous, Requested, Incomplete };
struct StandbyRecoveryResult {
    StandbyRecoveryState state = StandbyRecoveryState::Incomplete;
    esp_err_t rollback_error = ESP_OK;
    esp_err_t recovery_error = ESP_OK;
};
using StandbySequenceCallback = esp_err_t (*)(void *, bool, bool);

// requested: Failed transaction's target; recovery first restores its opposite.
// owner: Borrowed serialized resource owner.
// run: Executes the same real resource plan in best-effort recovery mode.
// Exception: Retains rollback and fallback errors; neither proves a coherent state on failure.
inline StandbyRecoveryResult recover_standby_sequence(bool requested, void *owner,
    StandbySequenceCallback run)
{
    StandbyRecoveryResult result;
    if (owner == nullptr || run == nullptr) {
        result.rollback_error = ESP_ERR_INVALID_ARG;
        return result;
    }
    result.rollback_error = run(owner, !requested, true);
    if (result.rollback_error == ESP_OK) {
        result.state = StandbyRecoveryState::Previous;
        return result;
    }
    result.recovery_error = run(owner, requested, true);
    if (result.recovery_error == ESP_OK) {
        result.state = StandbyRecoveryState::Requested;
    }
    return result;
}

} // namespace LUNASHELL::power_policy
