/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <array>
#include <cstdint>
#include "esp_err.h"

namespace CODY {

// Storage identities remain stable when the presentation catalog changes order.
enum class OnboardingTourStep : uint8_t {
    Buttons = 0, Touch = 1, Sofia = 2, Offline = 3, Project = 4, Completed = 5, Thanks = 6,
};

// stored: Raw persistent identity, never a presentation array index.
// step: Receives a recognized storage identity only on success.
constexpr bool decode_onboarding_tour_step(uint32_t stored, OnboardingTourStep &step)
{
    switch (stored) {
    case static_cast<uint32_t>(OnboardingTourStep::Buttons):
    case static_cast<uint32_t>(OnboardingTourStep::Touch):
    case static_cast<uint32_t>(OnboardingTourStep::Sofia):
    case static_cast<uint32_t>(OnboardingTourStep::Offline):
    case static_cast<uint32_t>(OnboardingTourStep::Project):
    case static_cast<uint32_t>(OnboardingTourStep::Completed):
    case static_cast<uint32_t>(OnboardingTourStep::Thanks):
        step = static_cast<OnboardingTourStep>(stored);
        return true;
    default: return false;
    }
}

// Confirmed storage values, distinct from the current boot's presentation state.
struct OnboardingSettings {
    bool persisted_first_boot = false;
    bool force_first_boot = false;
    bool effective_first_boot = false;
    bool birth_intro_pending = false;
    bool first_boot_music_pending = false;
    uint8_t onboarding_tour_step = static_cast<uint8_t>(OnboardingTourStep::Completed);
    bool setup_progress_pending = false;
};

enum class SettingsLifecycleOperation : uint8_t {
    None = 0,
    ConsumeFirstBootMusic = 1,
    CompleteBirthIntro = 2,
    CompleteSetup = 3,
};

enum class SettingsCommandState : uint8_t { Unavailable, Queued, Running, Succeeded, Failed, Superseded };

// A receipt belongs to one admitted command, never merely its current UI route.
struct SettingsLifecycleReceipt {
    uint32_t sequence = 0;
    SettingsLifecycleOperation operation = SettingsLifecycleOperation::None;
    SettingsCommandState state = SettingsCommandState::Unavailable;
    esp_err_t error = ESP_ERR_NOT_FINISHED;
    uint32_t expected_revision = 0;
};

struct SettingsStorageStatus {
    bool valid = false;
    esp_err_t error = ESP_ERR_NOT_FINISHED;
    // A newer explicit intent also supersedes old automatic completion retries.
    uint32_t revision = 0;
};

struct OnboardingStorageStatus {
    SettingsStorageStatus first_boot{};
    SettingsStorageStatus birth_intro{};
    SettingsStorageStatus first_boot_music{};
    SettingsStorageStatus tour_step{};
    SettingsStorageStatus setup_progress{};
};

// The settings service alone mutates this model. Consumers retain read-only
// projections; ordinary preferences keep their existing DeviceSnapshot publisher.
struct SettingsModel {
    OnboardingSettings onboarding{};
    OnboardingStorageStatus storage{};
    // Initial load completed; individual write/readback validity is kept above.
    bool valid = false;
    uint32_t revision = 0;
    esp_err_t last_error = ESP_ERR_NOT_FINISHED;
    std::array<SettingsLifecycleReceipt, 3> lifecycle{};
};
static_assert(sizeof(SettingsModel) <= 128U, "Settings reads must stay bounded on display stacks");

} // namespace CODY
