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

#include <cstdint>
#include <limits>
#include "esp_err.h"

namespace LUNASHELL {

inline constexpr char kAutoTimePreferenceKey[] = "cody_autotime";
inline constexpr int kClockOffsetUnchanged = std::numeric_limits<int>::min();

// Stable setting identities, not positions in a timezone menu. RTC storage stays UTC.
enum class ClockTimezone : uint8_t { Utc = 0, Rome = 1, HoChiMinh = 2, FixedOffset = 3 };
enum class RoutinePeriod : uint8_t { Unavailable, Morning, Afternoon, Evening, Night };
enum class ClockCuePolicy : uint8_t { Allowed, QuietHours, TimeUnavailable, SettingsUnavailable };
struct ClockSnapshot;

struct ClockPreferences {
    ClockTimezone timezone = ClockTimezone::Utc;
    bool routines_enabled = true;
    bool quiet_hours_enabled = false;
    uint16_t quiet_start_minute = 22 * 60;
    uint16_t quiet_end_minute = 7 * 60;
    bool pc_offset_active = false;
    int16_t pc_offset_minutes = 0;
};

// preferences: Configuration. Legacy named values are applied as fixed standard offsets.
// offset: Receives a supported configured offset only on success.
inline bool configured_clock_offset(const ClockPreferences &preferences, int16_t &offset)
{
    if (preferences.pc_offset_active) {
        if (preferences.pc_offset_minutes < -840 || preferences.pc_offset_minutes > 840) return false;
        offset = preferences.pc_offset_minutes;
        return true;
    }
    switch (preferences.timezone) {
    case ClockTimezone::Utc: offset = 0; return true;
    case ClockTimezone::Rome: offset = 60; return true;
    case ClockTimezone::HoChiMinh: offset = 420; return true;
    case ClockTimezone::FixedOffset: break;
    }
    return false;
}

// One supplied UTC instant projected through the time owner's configured zone.
struct ClockLocalCalendar {
    int64_t utc_epoch_seconds = 0;
    int16_t utc_offset_minutes = 0;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    ClockTimezone timezone = ClockTimezone::Utc;
};

// utc_epoch_seconds: Actual instant supplied by the caller, independent of the current wall clock.
// calendar: Receives the projected date and its exact offset on success only.
// Exception: Returns an error if the active timezone or requested date is unavailable.
esp_err_t project_clock_local_calendar(int64_t utc_epoch_seconds, ClockLocalCalendar &calendar);

// timezone: Explicit reviewed identity; unsupported values return nullptr, never UTC.
inline const char *clock_timezone_spec(ClockTimezone timezone)
{
    switch (timezone) {
    case ClockTimezone::Utc:
        return "UTC0";
    case ClockTimezone::Rome:
        return "UTC-1"; // Retained legacy preference means its standard fixed UTC offset.
    case ClockTimezone::HoChiMinh:
        return "UTC-7";
    case ClockTimezone::FixedOffset:
        return nullptr; // Runtime projection only; the fixed offset is stored independently.
    }
    return nullptr;
}

// preferences: Receives the validated active configuration; no storage or clock reads.
esp_err_t read_clock_preferences(ClockPreferences &preferences);
// preferences: Explicit runtime request, persisted atomically through the NVS owner.
esp_err_t configure_clock_preferences(const ClockPreferences &preferences);
enum class ClockRemoteOperation : uint8_t { Read, Configure };

// preferences: Receives the existing validated configuration only on a successful read.
// initialized: Receives the durable first-companion initialization marker.
// Exception: Returns original preference/marker read errors; invalid marker values are rejected.
esp_err_t read_remote_clock_preferences(ClockPreferences &preferences, bool &initialized);

// operation: Authenticated read, first-companion initialization or explicit user configuration.
// offset_minutes: Fixed UTC offset -840..840, or INT_MIN to preserve the existing choice.
// enabled: Quiet-hours switch 0/1, or -1 to preserve its existing choice.
// start_minute: Quiet window start 0..1439, or -1 to preserve it.
// end_minute: Quiet window end 0..1439, or -1 to preserve it.
// preferences: Receives the actual validated preferences after the operation when available.
// initialized: Receives the actual durable initialization marker when available.
// Exception: Returns original validation/storage/publication failures; no default repairs occur.
esp_err_t apply_remote_clock_preferences(ClockRemoteOperation operation, int offset_minutes,
    int enabled, int start_minute, int end_minute, ClockPreferences &preferences, bool &initialized);
// clock: Canonical, aged observation; an invalid/stale value cannot select a routine.
bool clock_observation_current(const ClockSnapshot &clock);
// clock: User opt-out is explicit; otherwise unknown time conservatively suppresses cues.
ClockCuePolicy clock_cue_policy(const ClockSnapshot &clock);

} // namespace LUNASHELL
