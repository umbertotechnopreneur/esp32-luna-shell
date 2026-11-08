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

namespace LUNASHELL::power_policy {
struct StandbyResourceStatus {
    bool applied = false;
    bool consistent = true;
    bool unsupported = false;
};

// status: Last confirmed owner lifecycle state; genuine build-off owners are optional.
// target: Requested resource policy state.
[[nodiscard]] constexpr bool standby_resource_matches(const StandbyResourceStatus &status, bool target)
{
    return status.unsupported || (status.consistent && status.applied == target);
}

// status: Last confirmed radio policy state.
// target: Requested resource policy state.
// allow_degraded: An explicitly recorded restore failure with verified radio cleanup.
[[nodiscard]] constexpr bool standby_wireless_matches(const StandbyResourceStatus &status,
    bool target, bool allow_degraded)
{
    return standby_resource_matches(status, target) ||
        (!target && allow_degraded && status.consistent && status.applied && !status.unsupported);
}

using StandbyTransition = esp_err_t (*)(bool);
using StandbyOwnerTransition = esp_err_t (*)(void *, bool);

// component: Diagnostic identity of the hardware or explicitly synthetic owner.
// target: Requested resource state.
// status: Owner's last confirmed state; uncertainty remains visible after failure.
// owner: Borrowed context whose lifetime covers the operation.
// transition: Non-null context-aware resource operation returning its real result.
// reassert: Reapply policy even when the confirmed state already matches.
// Exception: Returns original failure; only an unused build-off owner is optional.
esp_err_t transition_standby_resource(const char *component, bool target,
    StandbyResourceStatus &status, void *owner, StandbyOwnerTransition transition,
    bool reassert = false);

// component: Non-secret diagnostic identity of the resource owner.
// target: Requested standby state.
// status: Borrowed power-owner state, preserved while the operation evaluates its policy.
// transition: Non-null owner operation returning its real result.
// reassert: Reapply policy even when the last confirmed state already matches.
// Exception: Returns original transition failure; only an unused build-off owner is optional.
esp_err_t transition_standby_resource(const char *component, bool target,
    StandbyResourceStatus &status, StandbyTransition transition, bool reassert = false);

// target: Requested panel-off lifecycle state.
// applied: Last confirmed panel-off state.
// consistent: Whether that panel state is known after the last operation.
// display_enabled: Whether this profile owns an actual display.
// owner: Borrowed synchronous transition context.
// transition: Actual panel operation, or explicit synthetic fault transport on the host.
// rollback_error: Receives secondary local recovery failure independently.
// Exception: Returns original transition failure even when local rollback succeeds.
esp_err_t transition_standby_display(bool target, bool &applied, bool &consistent,
    bool display_enabled, void *owner, StandbyOwnerTransition transition,
    esp_err_t &rollback_error);
}
