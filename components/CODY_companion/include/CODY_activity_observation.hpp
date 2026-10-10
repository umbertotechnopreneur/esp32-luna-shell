#pragma once

#include <algorithm>
#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {

struct ActivityObservationState {
    char chat[65]{};
    LUNASHELL::CodexActivityState pending_phase = LUNASHELL::CodexActivityState::Working;
    LUNASHELL::CodexActivityState presented_phase = LUNASHELL::CodexActivityState::Working;
    int64_t pending_since_us = -1;
    int64_t observed_at_us = -1;
    int64_t operation_elapsed_us = -1;
    bool was_working = false;
};

// state: Serialized local observer while a genuine interaction owns the screen.
inline void pause_activity_observation(ActivityObservationState &state)
{
    state.was_working = false;
    state.pending_since_us = -1;
}

// state: Serialized presentation owner; genuine hook metadata remains in the canonical snapshot.
// phase: Actual typed hook phase, never inferred from an animation or elapsed clock.
// current: Genuine coherent link/activity admission, or explicitly labelled SIMULATED metadata.
// chat: Exact bounded SHA-256 chat identity; changing it retires all local dwell and elapsed state.
// active_time_us: Monotonic pausable UI clock, capturing first observed work before subtype admission.
// Exception: Returns invalid identity or clock errors without guessing a phase or elapsed time.
inline esp_err_t advance_activity_observation(ActivityObservationState &state,
    LUNASHELL::CodexActivityState phase, bool current, const char *chat, int64_t active_time_us)
{
    // Before the first authenticated activity, an empty invalid snapshot has no selected chat.
    constexpr char no_chat[] = "0000000000000000000000000000000000000000000000000000000000000000";
    const char *identity = !current && chat != nullptr && chat[0] == '\0' ? no_chat : chat;
    if (identity == nullptr || std::strlen(identity) != 64 || std::strspn(identity, "0123456789abcdef") != 64 ||
        active_time_us < 0 || (state.observed_at_us >= 0 && active_time_us < state.observed_at_us)) {
        ESP_LOGE("CODY_activity", "Observation identity or clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    using Phase = LUNASHELL::CodexActivityState;
    const bool working = current && (phase == Phase::Working || phase == Phase::Editing || phase == Phase::Running);
    const bool attention = current && phase == Phase::Paused;
    const bool same_chat = std::strcmp(identity, state.chat) == 0;
    if (!same_chat) {
        std::memcpy(state.chat, identity, sizeof(state.chat));
        state.pending_since_us = -1;
        state.operation_elapsed_us = -1;
        state.was_working = false;
    }
    if (working) {
        if (state.operation_elapsed_us < 0) {
            state.operation_elapsed_us = 0;
        } else if (state.was_working && state.observed_at_us >= 0) {
            state.operation_elapsed_us += active_time_us - state.observed_at_us;
        }
    } else if (!attention) {
        state.operation_elapsed_us = -1;
    }
    state.observed_at_us = active_time_us;
    state.was_working = working;
    state.presented_phase = working ? Phase::Working : phase;
    if (working && phase != Phase::Working) {
        if (state.pending_since_us < 0 || state.pending_phase != phase) {
            state.pending_phase = phase;
            state.pending_since_us = active_time_us;
        }
        if (active_time_us - state.pending_since_us >= 2500000) {
            state.presented_phase = phase;
        }
    } else {
        // Generic work covers short tool bursts. Completion and attention bypass subtype dwell.
        state.pending_since_us = -1;
    }
    return ESP_OK;
}

} // namespace CODY
