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

#include <array>
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "personality_catalog.hpp"

namespace CODY {

inline constexpr std::size_t kWellbeingReminderCount = 4;
inline constexpr int64_t kWellbeingMinuteUs = 60000000;

struct WellbeingRule {
    PersonalityReminder reminder;
    bool enabled;
    int64_t interval_us;
};

struct WellbeingConfig {
    bool enabled;
    int64_t snooze_us;
    WellbeingRule rules[kWellbeingReminderCount];
};

enum class WorkSessionPhase : uint8_t { Inactive = 0, Active = 1, Paused = 2 };
enum class WellbeingReminderPhase : uint8_t { Disabled = 0, Counting = 1, Due = 2, Snoozed = 3 };

struct WellbeingReminderState {
    PersonalityReminder reminder = PersonalityReminder::None;
    WellbeingReminderPhase phase = WellbeingReminderPhase::Disabled;
    int64_t deadline_us = 0;
};

// Pure session state, with four independent deadlines on the active-work clock.
struct WellbeingState {
    WorkSessionPhase session = WorkSessionPhase::Inactive;
    int64_t active_elapsed_us = 0;
    int64_t fatigue_elapsed_us = 0;
    std::array<WellbeingReminderState, kWellbeingReminderCount> reminders{};
};

struct WellbeingSnapshot {
    WorkSessionPhase session = WorkSessionPhase::Inactive;
    int64_t active_elapsed_us = 0;
    uint64_t fatigue_minutes = 0;
    int64_t fatigue_remainder_us = 0;
    std::array<WellbeingReminderState, kWellbeingReminderCount> reminders{};
};

// config: Complete authored configuration with four unique typed reminder identities.
// Exception: Returns invalid identities/duplicates or durations outside 1..1440 whole minutes.
esp_err_t validate_wellbeing_config(const WellbeingConfig &config);

// config: Authored settings; an intentionally disabled model is valid.
// state: Receives an inactive session with identified disabled reminder slots on success only.
// Exception: Returns the original authored configuration failure without replacing caller state.
esp_err_t initialize_wellbeing(const WellbeingConfig &config, WellbeingState &state);

// config: Authored settings for this session.
// elapsed_us: Nonnegative active-work elapsed duration; paused/inactive sessions ignore it.
// state: Receives elapsed fatigue and newly due statuses transactionally.
// Exception: Returns invalid input/state or overflow without partial mutation.
esp_err_t advance_wellbeing(const WellbeingConfig &config, int64_t elapsed_us, WellbeingState &state);

// config: Authored settings; disabled event operations are explicitly not supported.
// event: Stable session lifecycle, reminder acknowledgment or snooze identity.
// reminder: None for lifecycle events; a known reminder for acknowledgment/snooze.
// state: Receives a valid transition transactionally; only ScreenBreak acknowledgment resets fatigue.
// Due reminders may be explicitly acknowledged/snoozed after Pause or End; their work clock stays frozen.
// Exception: Returns invalid identity/transition, disabled operation or deadline overflow unchanged.
esp_err_t apply_wellbeing_event(const WellbeingConfig &config, PersonalityEvent event,
                               PersonalityReminder reminder, WellbeingState &state);

// config: Settings used to validate the identified session state.
// state: Pure state to project without advancing any clock.
// snapshot: Receives fatigue minutes/remainder and reminder statuses on success only.
// Exception: Returns the original configuration or state failure without replacing output.
esp_err_t read_wellbeing_snapshot(const WellbeingConfig &config, const WellbeingState &state,
                                WellbeingSnapshot &snapshot);

} // namespace CODY
