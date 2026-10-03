#pragma once

#include "CODY_rig.hpp"
#include "CODY_expression_catalog.hpp"

namespace CODY {

enum class LifecyclePhase : uint8_t { BootCalm, BootHappy, BootSettling, Idle, Sleep, Wake };

struct LifecycleState {
    LifecyclePhase phase = LifecyclePhase::BootCalm;
    uint32_t phase_elapsed_us = 0;
};

// state: Current phase and active-time clock; standby time is excluded by the caller.
// active_delta_us: Nonnegative elapsed animation time, never a sensor measurement.
// next: Receives the complete phase update only on success.
esp_err_t advance_lifecycle(const LifecycleState &state, int64_t active_delta_us, LifecycleState &next);

// state: Current boot, idle or sleep/wake presentation.
// face: Receives the authored target without replacing the rhythmic channels.
// contour: Receives gentle independent body movement.
esp_err_t lifecycle_targets(const LifecycleState &state, Channels &face, ContourControls &contour);

// state: Current sleep preparation; completion includes a visible closed-eye hold.
bool lifecycle_sleep_ready(const LifecycleState &state);

} // namespace CODY
