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
#include "esp_err.h"

namespace LUNASHELL {

// Stable setting identities, not positions in a timezone menu. RTC storage stays UTC.
enum class ClockTimezone : uint8_t { Utc = 0, Rome = 1, HoChiMinh = 2 };
enum class RoutinePeriod : uint8_t { Unavailable, Morning, Afternoon, Evening, Night };
enum class ClockCuePolicy : uint8_t { Allowed, QuietHours, TimeUnavailable, SettingsUnavailable };
struct ClockSnapshot;

struct ClockPreferences {
    ClockTimezone timezone = ClockTimezone::Utc;
    bool routines_enabled = true;
    bool quiet_hours_enabled = false;
    uint16_t quiet_start_minute = 22 * 60;
    uint16_t quiet_end_minute = 7 * 60;
};

// timezone: Explicit reviewed identity; unsupported values return nullptr, never UTC.
inline const char *clock_timezone_spec(ClockTimezone timezone)
{
    switch (timezone) {
    case ClockTimezone::Utc:
        return "UTC0";
    case ClockTimezone::Rome:
        return "CET-1CEST,M3.5.0/2,M10.5.0/3";
    case ClockTimezone::HoChiMinh:
        return "ICT-7";
    }
    return nullptr;
}

// preferences: Receives the validated active configuration; no storage or clock reads.
esp_err_t read_clock_preferences(ClockPreferences &preferences);
// preferences: Explicit runtime request, persisted atomically through the NVS owner.
esp_err_t configure_clock_preferences(const ClockPreferences &preferences);
enum class ClockRemoteOperation : uint8_t { Read, Initialize, Configure };

// preferences: Receives the existing validated configuration only on a successful read.
// initialized: Receives the durable first-companion initialization marker.
// Exception: Returns original preference/marker read errors; invalid marker values are rejected.
esp_err_t read_remote_clock_preferences(ClockPreferences &preferences, bool &initialized);

// operation: Authenticated read, first-companion initialization or explicit user configuration.
// timezone: Typed timezone value 0..2, or -1 to preserve the existing choice.
// enabled: Quiet-hours switch 0/1, or -1 to preserve its existing choice.
// start_minute: Quiet window start 0..1439, or -1 to preserve it.
// end_minute: Quiet window end 0..1439, or -1 to preserve it.
// preferences: Receives the actual validated preferences after the operation when available.
// initialized: Receives the actual durable initialization marker when available.
// Exception: Returns original validation/storage/publication failures; no default repairs occur.
esp_err_t apply_remote_clock_preferences(ClockRemoteOperation operation, int timezone,
    int enabled, int start_minute, int end_minute, ClockPreferences &preferences, bool &initialized);
// clock: Canonical, aged observation; an invalid/stale value cannot select a routine.
bool clock_observation_current(const ClockSnapshot &clock);
// clock: User opt-out is explicit; otherwise unknown time conservatively suppresses cues.
ClockCuePolicy clock_cue_policy(const ClockSnapshot &clock);

} // namespace LUNASHELL
