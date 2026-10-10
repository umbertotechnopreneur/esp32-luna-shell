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

/*
 * Canonical inactivity policy shared by the power service and LUNASHELL UI.
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "LUNASHELL_platform.h"
#include "LUNASHELL_display_timing.hpp"

namespace LUNASHELL::power_policy {

inline constexpr std::array<uint32_t, 7> kChromeTimeoutOptions = {
    10, 30, 60, 120, 300, 600, 0,
};
inline constexpr std::array<uint32_t, 6> kDisplayDimTimeoutOptions = {
    60, 120, 300, 600, 900, 0,
};
inline constexpr std::array<uint32_t, 7> kScreenSleepDelayOptions = {
    30, 60, 120, 300, 600, 900, 0,
};
inline constexpr std::array<uint32_t, 7> kScreenIdleTimeoutOptions = {
    30, 60, 120, 300, 600, 900, 0,
};
inline constexpr uint32_t kDefaultScreenIdleTimeoutSeconds = 60;
inline constexpr uint32_t kDisplayFadeDurationMs = display_timing::kTransitionFadeMs;
inline constexpr uint8_t kMotionWakeThresholdMg = 200;

// Hide the command buttons quickly; any touch or motion wakes them again.
inline constexpr uint32_t kDefaultCommandArcTimeoutSeconds = 10;
inline constexpr uint32_t kDefaultStatusArcTimeoutSeconds = 120;
inline constexpr uint32_t kDefaultDisplayDimTimeoutSeconds = 300;
inline constexpr uint32_t kDefaultScreenSleepDelaySeconds = 300;

// timeout: Typed setting whose supported runtime values are requested.
[[nodiscard]] constexpr std::span<const uint32_t> timeout_options(
    LUNASHELL_power_timeout_t timeout
)
{
    switch (timeout) {
    case LUNASHELL_POWER_TIMEOUT_COMMAND_ARC:
    case LUNASHELL_POWER_TIMEOUT_STATUS_ARC:
        return kChromeTimeoutOptions;
    case LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM:
        return kDisplayDimTimeoutOptions;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP:
        return kScreenSleepDelayOptions;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE:
        return kScreenIdleTimeoutOptions;
    }
    return {};
}

// timeout: Typed setting whose first-boot default is requested.
[[nodiscard]] constexpr uint32_t default_timeout_seconds(
    LUNASHELL_power_timeout_t timeout
)
{
    switch (timeout) {
    case LUNASHELL_POWER_TIMEOUT_COMMAND_ARC:
        return kDefaultCommandArcTimeoutSeconds;
    case LUNASHELL_POWER_TIMEOUT_STATUS_ARC:
        return kDefaultStatusArcTimeoutSeconds;
    case LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM:
        return kDefaultDisplayDimTimeoutSeconds;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP:
        return kDefaultScreenSleepDelaySeconds;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE:
        return kDefaultScreenIdleTimeoutSeconds;
    }
    return 0;
}

// timeout: Typed setting whose NVS key is requested.
[[nodiscard]] constexpr const char *settings_key(
    LUNASHELL_power_timeout_t timeout
)
{
    switch (timeout) {
    case LUNASHELL_POWER_TIMEOUT_COMMAND_ARC:
        return "cmd_idle_s";
    case LUNASHELL_POWER_TIMEOUT_STATUS_ARC:
        return "status_idle_s";
    case LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM:
        return "dim_idle_s";
    case LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP:
        return "sleep_delay_s";
    case LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE:
        return "idle_sleep_s";
    }
    return "";
}

// timeout: Typed setting whose diagnostic name is requested.
[[nodiscard]] constexpr const char *display_name(
    LUNASHELL_power_timeout_t timeout
)
{
    switch (timeout) {
    case LUNASHELL_POWER_TIMEOUT_COMMAND_ARC:
        return "Command Arc";
    case LUNASHELL_POWER_TIMEOUT_STATUS_ARC:
        return "Status Arc";
    case LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM:
        return "display dim";
    case LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP:
        return "screen sleep after dim";
    case LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE:
        return "screen inactivity";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool timeout_supported(
    LUNASHELL_power_timeout_t timeout,
    uint32_t seconds
)
{
    for (const uint32_t option : timeout_options(timeout)) {
        if (option == seconds) {
            return true;
        }
    }
    return false;
}

static_assert(timeout_supported(
    LUNASHELL_POWER_TIMEOUT_COMMAND_ARC,
    kDefaultCommandArcTimeoutSeconds
));
static_assert(timeout_supported(
    LUNASHELL_POWER_TIMEOUT_STATUS_ARC,
    kDefaultStatusArcTimeoutSeconds
));
static_assert(timeout_supported(
    LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM,
    kDefaultDisplayDimTimeoutSeconds
));
static_assert(timeout_supported(
    LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP,
    kDefaultScreenSleepDelaySeconds
));
static_assert(timeout_supported(
    LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE,
    kDefaultScreenIdleTimeoutSeconds
));

}  // namespace LUNASHELL::power_policy
