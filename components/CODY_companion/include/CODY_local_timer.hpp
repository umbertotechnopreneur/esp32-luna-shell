#pragma once

#include <cstdint>

#include "esp_err.h"

namespace CODY {

inline constexpr uint16_t kLocalTimerMinMinutes = 1;
inline constexpr uint16_t kLocalTimerMaxMinutes = 120;

enum class LocalTimerPhase : uint8_t { Idle, Running, Finished };
enum class LocalTimerSound : uint8_t { Bell, Chime };
enum class LocalTimerAlarm : uint8_t { None, Pending, Playing, Complete, Silent, Failed };

// The Cody worker owns this state across navigation and software standby.
struct LocalTimerModel {
    LocalTimerPhase phase = LocalTimerPhase::Idle;
    LocalTimerSound sound = LocalTimerSound::Bell;
    LocalTimerAlarm alarm = LocalTimerAlarm::None;
    uint16_t duration_minutes = 5;
    int64_t deadline_us = 0;
    int64_t next_alarm_attempt_at_us = 0;
    uint32_t alarm_request_sequence = 0;
    esp_err_t wake_error = ESP_OK;
    esp_err_t alarm_error = ESP_OK;
};

// model: Retains the chosen duration and sound while idle.
// step: One minute forward (+1) or backward (-1), bounded at the endpoints.
void adjust_local_timer(LocalTimerModel &model, int step);

// model: Changes the built-in sound only while idle.
void cycle_local_timer_sound(LocalTimerModel &model);

// model: Starts the single timer from its idle settings.
// now_us: Device monotonic time, independent of network and wall-clock sync.
// Exception: Returns invalid phase or an invalid duration/clock value without starting.
esp_err_t start_local_timer(LocalTimerModel &model, int64_t now_us);

// model: Cancels only a currently running timer and preserves its settings.
void cancel_local_timer(LocalTimerModel &model);

// model: Dismisses the finished state and any pending admission of the alarm sound.
void dismiss_local_timer(LocalTimerModel &model);

// model: Current timer phase, deadline and selected idle duration.
// now_us: Device monotonic time, in the same domain as the deadline.
uint32_t local_timer_seconds_remaining(const LocalTimerModel &model, int64_t now_us);

// model: Worker-owned countdown and one-shot wake request.
// now_us: Current device monotonic time, sampled even with the panel off.
// Returns true only on the transition to Finished; preserves optional wake errors.
bool update_local_timer(LocalTimerModel &model, int64_t now_us);

// model: Tracks the single alarm through real shared playback admission and result.
// now_us: Device monotonic time for bounded retry and terminal deadlines.
// voice_busy: True while the recording flow owns audio, including preparation.
// Exception: Retains and logs original power observation and audio errors in alarm_error.
void update_local_timer_alarm(LocalTimerModel &model, int64_t now_us, bool voice_busy);

} // namespace CODY
