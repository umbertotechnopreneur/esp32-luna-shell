#pragma once
#include "esp_err.h"

namespace LUNASHELL::power_policy {
struct StandbyResourceStatus {
    bool applied = false;
    bool consistent = true;
    bool unsupported = false;
};

using StandbyTransition = esp_err_t (*)(bool);

// component: Non-secret diagnostic identity of the resource owner.
// target: Requested standby state.
// status: Borrowed power-owner state, preserved while the operation evaluates its policy.
// transition: Non-null owner operation returning its real result.
// reassert: Reapply policy even when the last confirmed state already matches.
// Exception: Returns original transition failure; only an unused build-off owner is optional.
esp_err_t transition_standby_resource(const char *component, bool target,
    StandbyResourceStatus &status, StandbyTransition transition, bool reassert = false);
}
