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

#include "CODY_wellbeing.hpp"

#include <limits>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_wellbeing";

// reminder: Typed identity to check against this bounded MVP catalog.
bool reminder_known(PersonalityReminder reminder)
{
    return reminder == PersonalityReminder::ScreenBreak || reminder == PersonalityReminder::MovementBreak ||
        reminder == PersonalityReminder::EyeRest || reminder == PersonalityReminder::DrinkWater;
}

// config: Complete authored rules, traversed only at the storage boundary.
// reminder: Stable domain identity whose matching rule is requested.
const WellbeingRule *find_rule(const WellbeingConfig &config, PersonalityReminder reminder)
{
    for (const auto &rule : config.rules) {
        if (rule.reminder == reminder) {
            return &rule;
        }
    }
    return nullptr;
}

// event: Rejected typed operation.
// reminder: Actual reminder identity supplied by the caller.
// session: Existing session state when the operation was rejected.
// cause: Concrete input, transition or support failure.
// error: Original structured error returned alongside the log.
esp_err_t reject_event(PersonalityEvent event, PersonalityReminder reminder, WorkSessionPhase session,
                       const char *cause, esp_err_t error)
{
    ESP_LOGE(kTag, "Event=%u reminder=%u session=%u rejected: %s (%s)",
             static_cast<unsigned>(event), static_cast<unsigned>(reminder),
             static_cast<unsigned>(session), cause, esp_err_to_name(error));
    return error;
}

// config: Previously validated authored rules.
// state: Existing identified session and active-clock reminder deadlines.
// Exception: Returns an invalid state with its actual rejected domain values logged.
esp_err_t validate_state(const WellbeingConfig &config, const WellbeingState &state)
{
    if (state.session > WorkSessionPhase::Paused || state.active_elapsed_us < 0 ||
        state.fatigue_elapsed_us < 0 || state.fatigue_elapsed_us > state.active_elapsed_us ||
        (!config.enabled && state.session != WorkSessionPhase::Inactive)) {
        ESP_LOGE(kTag, "State rejected: session=%u active_us=%lld fatigue_us=%lld enabled=%u (%s)",
                 static_cast<unsigned>(state.session), static_cast<long long>(state.active_elapsed_us),
                 static_cast<long long>(state.fatigue_elapsed_us), static_cast<unsigned>(config.enabled),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0; index < state.reminders.size(); ++index) {
        const auto &item = state.reminders[index];
        const auto *rule = find_rule(config, item.reminder);
        bool duplicate = false;
        for (size_t previous = 0; previous < index; ++previous) {
            duplicate = duplicate || state.reminders[previous].reminder == item.reminder;
        }
        const bool disabled_valid = item.phase != WellbeingReminderPhase::Disabled ||
            (item.deadline_us == 0 && (!rule || !rule->enabled || state.session == WorkSessionPhase::Inactive));
        const bool future_valid = (item.phase != WellbeingReminderPhase::Counting &&
            item.phase != WellbeingReminderPhase::Snoozed) || item.deadline_us > state.active_elapsed_us;
        const bool due_valid = item.phase != WellbeingReminderPhase::Due ||
            (item.deadline_us > 0 && item.deadline_us <= state.active_elapsed_us);
        if (!reminder_known(item.reminder) || rule == nullptr || duplicate ||
            item.phase > WellbeingReminderPhase::Snoozed || !disabled_valid || !future_valid || !due_valid ||
            (!rule->enabled && item.phase != WellbeingReminderPhase::Disabled)) {
            ESP_LOGE(kTag, "Reminder state rejected: id=%u phase=%u deadline_us=%lld active_us=%lld duplicate=%u (%s)",
                     static_cast<unsigned>(item.reminder), static_cast<unsigned>(item.phase),
                     static_cast<long long>(item.deadline_us), static_cast<long long>(state.active_elapsed_us),
                     static_cast<unsigned>(duplicate), esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

// clock_us: Existing nonnegative active-work clock.
// interval_us: Positive authored duration.
// deadline_us: Receives a representable future deadline on success only.
// Exception: Returns overflow with the actual clock and interval logged.
esp_err_t checked_deadline(int64_t clock_us, int64_t interval_us, int64_t &deadline_us)
{
    if (interval_us > std::numeric_limits<int64_t>::max() - clock_us) {
        ESP_LOGE(kTag, "Deadline overflow: clock_us=%lld interval_us=%lld (%s)",
                 static_cast<long long>(clock_us), static_cast<long long>(interval_us),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    deadline_us = clock_us + interval_us;
    return ESP_OK;
}
} // namespace

// config: Four typed authored rules plus the global enable flag and active-clock snooze duration.
// Exception: Returns invalid identity, duplicate rule or duration outside 1..1440 whole minutes.
esp_err_t validate_wellbeing_config(const WellbeingConfig &config)
{
    constexpr int64_t maximum_interval_us = 1440 * kWellbeingMinuteUs;
    if (config.snooze_us <= 0 || config.snooze_us > maximum_interval_us ||
        config.snooze_us % kWellbeingMinuteUs != 0) {
        ESP_LOGE(kTag, "Snooze_us=%lld rejected (%s)", static_cast<long long>(config.snooze_us),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < kWellbeingReminderCount; ++index) {
        const auto &rule = config.rules[index];
        if (!reminder_known(rule.reminder) || rule.interval_us <= 0 ||
            rule.interval_us > maximum_interval_us || rule.interval_us % kWellbeingMinuteUs != 0) {
            ESP_LOGE(kTag, "Rule rejected: reminder=%u interval_us=%lld (%s)",
                     static_cast<unsigned>(rule.reminder), static_cast<long long>(rule.interval_us),
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (config.rules[previous].reminder == rule.reminder) {
                ESP_LOGE(kTag, "Duplicate reminder=%u rejected (%s)", static_cast<unsigned>(rule.reminder),
                         esp_err_to_name(ESP_ERR_INVALID_ARG));
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return ESP_OK;
}

// config: Authored settings, including intentionally disabled operation.
// state: Receives an inactive session and identified disabled reminders after successful validation.
// Exception: Returns the original configuration error without changing caller state.
esp_err_t initialize_wellbeing(const WellbeingConfig &config, WellbeingState &state)
{
    const esp_err_t error = validate_wellbeing_config(config);
    if (error != ESP_OK) {
        return error;
    }
    WellbeingState initial;
    for (size_t index = 0; index < kWellbeingReminderCount; ++index) {
        initial.reminders[index].reminder = config.rules[index].reminder;
    }
    state = initial;
    return ESP_OK;
}

// config: Authored settings for this pure session.
// elapsed_us: Nonnegative interval supplied by a future session owner, never sampled here.
// state: Receives active-clock advancement, fatigue and newly due statuses on success only.
// Exception: Returns invalid input/state or overflow without partial mutation.
esp_err_t advance_wellbeing(const WellbeingConfig &config, int64_t elapsed_us, WellbeingState &state)
{
    esp_err_t error = validate_wellbeing_config(config);
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
    if (!config.enabled || state.session != WorkSessionPhase::Active) {
        return ESP_OK;
    }
    constexpr int64_t maximum_time = std::numeric_limits<int64_t>::max();
    if (elapsed_us > maximum_time - state.active_elapsed_us ||
        elapsed_us > maximum_time - state.fatigue_elapsed_us) {
        ESP_LOGE(kTag, "Advance overflow: elapsed_us=%lld active_us=%lld fatigue_us=%lld (%s)",
                 static_cast<long long>(elapsed_us), static_cast<long long>(state.active_elapsed_us),
                 static_cast<long long>(state.fatigue_elapsed_us), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    WellbeingState next = state;
    next.active_elapsed_us += elapsed_us;
    next.fatigue_elapsed_us += elapsed_us;
    for (auto &item : next.reminders) {
        if ((item.phase == WellbeingReminderPhase::Counting || item.phase == WellbeingReminderPhase::Snoozed) &&
            next.active_elapsed_us >= item.deadline_us) {
            // A due status remains latched until an explicit event; no repeated event is queued.
            item.phase = WellbeingReminderPhase::Due;
        }
    }
    state = next;
    return ESP_OK;
}

// config: Complete authored model settings.
// event: Typed session lifecycle, acknowledgment or snooze operation.
// reminder: None for lifecycle operations, otherwise a known due reminder.
// state: Receives a complete successful transition only.
// Exception: Returns invalid identity/transition, disabled operation or overflow unchanged.
esp_err_t apply_wellbeing_event(const WellbeingConfig &config, PersonalityEvent event,
                               PersonalityReminder reminder, WellbeingState &state)
{
    esp_err_t error = validate_wellbeing_config(config);
    if (error != ESP_OK) {
        return error;
    }
    error = validate_state(config, state);
    if (error != ESP_OK) {
        return error;
    }
    const bool lifecycle_event = event == PersonalityEvent::WorkSessionStart ||
        event == PersonalityEvent::WorkSessionPause || event == PersonalityEvent::WorkSessionResume ||
        event == PersonalityEvent::WorkSessionEnd;
    const bool reminder_event = event == PersonalityEvent::ReminderAcknowledge ||
        event == PersonalityEvent::ReminderSnooze;
    if ((!lifecycle_event && !reminder_event) ||
        (lifecycle_event && reminder != PersonalityReminder::None) ||
        (reminder_event && !reminder_known(reminder))) {
        return reject_event(event, reminder, state.session, "Unknown or mismatched operation identity", ESP_ERR_INVALID_ARG);
    }
    if (!config.enabled) {
        return reject_event(event, reminder, state.session, "Authored model is disabled", ESP_ERR_NOT_SUPPORTED);
    }
    WellbeingState next = state;
    if (event == PersonalityEvent::WorkSessionStart) {
        if (state.session != WorkSessionPhase::Inactive) {
            return reject_event(event, reminder, state.session, "Start requires an inactive session", ESP_ERR_INVALID_STATE);
        }
        error = initialize_wellbeing(config, next);
        if (error != ESP_OK) {
            return error;
        }
        next.session = WorkSessionPhase::Active;
        for (auto &item : next.reminders) {
            const auto *rule = find_rule(config, item.reminder);
            if (rule == nullptr) {
                return reject_event(event, item.reminder, state.session,
                                    "Initialized reminder has no authored rule", ESP_ERR_INVALID_STATE);
            }
            if (rule->enabled) {
                item.phase = WellbeingReminderPhase::Counting;
                item.deadline_us = rule->interval_us;
            }
        }
    } else if (event == PersonalityEvent::WorkSessionPause) {
        if (state.session != WorkSessionPhase::Active) {
            return reject_event(event, reminder, state.session, "Pause requires an active session", ESP_ERR_INVALID_STATE);
        }
        next.session = WorkSessionPhase::Paused;
    } else if (event == PersonalityEvent::WorkSessionResume) {
        if (state.session != WorkSessionPhase::Paused) {
            return reject_event(event, reminder, state.session, "Resume requires a paused session", ESP_ERR_INVALID_STATE);
        }
        next.session = WorkSessionPhase::Active;
    } else if (event == PersonalityEvent::WorkSessionEnd) {
        if (state.session == WorkSessionPhase::Inactive) {
            return reject_event(event, reminder, state.session, "End requires an active or paused session", ESP_ERR_INVALID_STATE);
        }
        next.session = WorkSessionPhase::Inactive;
    } else {
        WellbeingReminderState *selected = nullptr;
        for (auto &item : next.reminders) {
            if (item.reminder == reminder) {
                selected = &item;
                break;
            }
        }
        if (selected == nullptr || selected->phase != WellbeingReminderPhase::Due) {
            return reject_event(event, reminder, state.session, "Reminder operation requires a due reminder", ESP_ERR_INVALID_STATE);
        }
        const auto *rule = find_rule(config, reminder);
        if (rule == nullptr) {
            return reject_event(event, reminder, state.session,
                                "Due reminder has no authored rule", ESP_ERR_INVALID_STATE);
        }
        const bool acknowledged = event == PersonalityEvent::ReminderAcknowledge;
        error = checked_deadline(next.active_elapsed_us, acknowledged ? rule->interval_us : config.snooze_us,
                                 selected->deadline_us);
        if (error != ESP_OK) {
            return error;
        }
        selected->phase = acknowledged ? WellbeingReminderPhase::Counting : WellbeingReminderPhase::Snoozed;
        if (acknowledged && reminder == PersonalityReminder::ScreenBreak) {
            next.fatigue_elapsed_us = 0;
        }
    }
    state = next;
    return ESP_OK;
}

// config: Authored settings used to validate the snapshot's source state.
// state: Pure state; this read never advances an elapsed clock.
// snapshot: Receives derived fatigue minutes/remainder and identified reminder statuses.
// Exception: Returns the original configuration/state error without replacing output.
esp_err_t read_wellbeing_snapshot(const WellbeingConfig &config, const WellbeingState &state,
                                WellbeingSnapshot &snapshot)
{
    esp_err_t error = validate_wellbeing_config(config);
    if (error != ESP_OK) {
        return error;
    }
    error = validate_state(config, state);
    if (error != ESP_OK) {
        return error;
    }
    WellbeingSnapshot projected;
    projected.session = state.session;
    projected.active_elapsed_us = state.active_elapsed_us;
    projected.fatigue_minutes = static_cast<uint64_t>(state.fatigue_elapsed_us / kWellbeingMinuteUs);
    projected.fatigue_remainder_us = state.fatigue_elapsed_us % kWellbeingMinuteUs;
    projected.reminders = state.reminders;
    snapshot = projected;
    return ESP_OK;
}

} // namespace CODY
