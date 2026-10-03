#pragma once

#include "CODY_rig.hpp"
#include "CODY_expression_catalog.hpp"
#include "LUNASHELL_display_timing.hpp"

namespace CODY {

enum class BootCheckpoint : uint32_t { Sleeping, Waking, Ready };
enum class LifecyclePhase : uint8_t { BootSleeping, BootWaking, BootReady, Idle, Sleep, Wake };
enum class StartupPresentation : uint8_t { CharacterFade, PanelFade };

inline constexpr uint32_t kBootSleepingUs = 2000000;
inline constexpr uint32_t kBootWakingUs = 4000000;
inline constexpr uint32_t kBootReadyUs = 4000000;
inline constexpr uint32_t kPanelWakeSleepingUs = LUNASHELL::display_timing::kTransitionFadeMs * 1000U;

struct LifecycleState {
    LifecyclePhase phase = LifecyclePhase::BootSleeping;
    uint32_t phase_elapsed_us = 0;
    StartupPresentation startup_presentation = StartupPresentation::CharacterFade;
};

// state: Settled animation permits interaction; timed startup/Sleep/Wake do not.
constexpr bool lifecycle_is_resting(const LifecycleState &state)
{
    return state.phase == LifecyclePhase::Idle;
}

// state: Current phase and active-time clock; standby time is excluded by the caller.
// active_delta_us: Nonnegative elapsed animation time, never a sensor measurement.
// checkpoint: Last successful startup checkpoint; animation cannot imply hardware readiness.
// next: Receives the complete phase update only on success.
esp_err_t advance_lifecycle(const LifecycleState &state, int64_t active_delta_us,
                            BootCheckpoint checkpoint, LifecycleState &next);

// state: Cold boot fades Sleeping01; software wake delegates visibility to the panel.
uint8_t lifecycle_opacity(const LifecycleState &state);

// state: Current boot, idle or sleep/wake presentation.
// face: Receives the authored target without replacing the rhythmic channels.
// contour: Receives gentle independent body movement.
esp_err_t lifecycle_targets(const LifecycleState &state, Channels &face, ContourControls &contour);

// state: Current sleep preparation; completion includes a visible closed-eye hold.
bool lifecycle_sleep_ready(const LifecycleState &state);

} // namespace CODY
