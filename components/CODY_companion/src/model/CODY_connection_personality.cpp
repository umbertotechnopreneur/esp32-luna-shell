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

#include "CODY_connection_personality.hpp"

#include <algorithm>
#include "esp_log.h"

namespace CODY {

// state: Active-time narrative, independent of real connection success or failure.
ConnectionSearchPresentation connection_search_presentation(const ConnectionPersonalityState &state)
{
    const int64_t seconds = state.search_elapsed_us / 1000000;
    if (seconds < 6) {
        return {PersonalityCue::Curiosity, ExpressionId::Curious};
    }
    if (seconds < 14) {
        return {PersonalityCue::None, ExpressionId::Calm};
    }
    if (seconds < 20) {
        return {PersonalityCue::Curiosity, ExpressionId::Curious};
    }
    if (seconds < 32) {
        return {PersonalityCue::Search, ExpressionId::Focused};
    }
    if (seconds < 42) {
        return {PersonalityCue::None, ExpressionId::Worried};
    }
    if (seconds < 48) {
        return {PersonalityCue::Fear, ExpressionId::Afraid};
    }
    if (seconds < 62) {
        return {PersonalityCue::None, ExpressionId::Happy};
    }
    if (seconds < 74) {
        return {PersonalityCue::Search, ExpressionId::Focused};
    }
    return {PersonalityCue::None, ExpressionId::Calm};
}

// inputs: Classified canonical connectivity, never an inferred result of an animation.
// active_delta_us: Elapsed active time since the last evaluated frame.
// state: Retains a real connection edge so repeated frames cannot retrigger its sound.
// cue: Updated only after a successful input contract check.
// Exception: Returns invalid argument without mutating state or cue on rejection.
esp_err_t advance_connection_personality(const ConnectionPersonalityInputs &inputs,
    int64_t active_delta_us, ConnectionPersonalityState &state, PersonalityCue &cue)
{
    if (active_delta_us < 0 ||
        (inputs.pc_connected && (!inputs.pc_current || inputs.pc_searching)) ||
        (inputs.pc_searching && !inputs.pc_current) ||
        (inputs.wifi_searching && !inputs.wifi_current) ||
        (inputs.wifi_connected && (!inputs.wifi_current || inputs.wifi_searching))) {
        ESP_LOGE("CODY_connection", "Connection reaction rejected: dt=%lld wifi=%u/%u/%u pc=%u/%u/%u (%s)",
            static_cast<long long>(active_delta_us), static_cast<unsigned>(inputs.wifi_current),
            static_cast<unsigned>(inputs.wifi_searching), static_cast<unsigned>(inputs.wifi_connected),
            static_cast<unsigned>(inputs.pc_current),
            static_cast<unsigned>(inputs.pc_connected), static_cast<unsigned>(inputs.pc_searching),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    ConnectionPersonalityState next = state;
    next.wifi_connected_remaining_us -= std::min(next.wifi_connected_remaining_us, active_delta_us);
    if (!inputs.wifi_current) {
        next.wifi_observation_continuous = false;
        next.wifi_connected_remaining_us = 0;
    } else {
        if (inputs.wifi_connected && (!next.wifi_observed ||
            (next.wifi_observation_continuous && !next.wifi_was_connected))) {
            next.wifi_connected_remaining_us = 4000000;
        }
        if (!inputs.wifi_connected) {
            next.wifi_connected_remaining_us = 0;
        }
        next.wifi_observed = true;
        next.wifi_observation_continuous = true;
        next.wifi_was_connected = inputs.wifi_connected;
    }
    next.event_remaining_us -= std::min(next.event_remaining_us, active_delta_us);
    if (next.event_remaining_us == 0 || !inputs.pc_current) {
        next.event = PersonalityCue::None;
        next.event_remaining_us = 0;
    }
    if (!inputs.pc_current) {
        // An unavailable interval breaks edge continuity; it is not a disconnect.
        next.pc_observation_continuous = false;
    } else if (inputs.pc_current) {
        if (inputs.pc_connected && (!next.pc_observed ||
            (next.pc_observation_continuous && !next.pc_was_connected))) {
            next.event = PersonalityCue::Joy;
            next.event_remaining_us = 4000000;
        } else if (next.pc_observation_continuous && next.pc_was_connected && !inputs.pc_connected) {
            next.event = PersonalityCue::Fear;
            next.event_remaining_us = 4000000;
        }
        next.pc_observed = true;
        next.pc_observation_continuous = true;
        next.pc_was_connected = inputs.pc_connected;
    }
    // The narrative runs only on the actual discovery view with current LAN facts.
    next.search_active = inputs.search_view_visible && inputs.pc_searching && inputs.wifi_connected &&
        next.wifi_connected_remaining_us == 0 && next.event_remaining_us == 0;
    next.search_elapsed_us = next.search_active
        ? static_cast<int64_t>((static_cast<uint64_t>(next.search_elapsed_us) +
            static_cast<uint64_t>(active_delta_us)) % 96000000ULL) : 0;
    // Loss of network truth cancels the PC celebration; it never waits to play later.
    if (inputs.wifi_searching) {
        next.event = PersonalityCue::None;
        next.event_remaining_us = 0;
        cue = PersonalityCue::NoWifi;
    } else if (next.wifi_connected_remaining_us > 0) {
        cue = PersonalityCue::WifiConnected;
    } else if (next.event_remaining_us > 0 && inputs.pc_current) {
        cue = next.event;
    } else if (next.search_active) {
        cue = connection_search_presentation(next).cue;
    } else {
        cue = inputs.pc_searching ? PersonalityCue::Curiosity : PersonalityCue::None;
    }
    state = next;
    return ESP_OK;
}

// scene: Stable identity, independent of declaration order, title or storage index.
// No exceptions: Routes without an authored reaction return None.
PersonalityCue scene_personality_cue(SceneId scene)
{
    switch (scene) {
    case SceneId::PairingFailed:
    case SceneId::ConnectionLost:
    case SceneId::ChecksFailed:
        return PersonalityCue::Fear;
    case SceneId::CheckFailures:
        return PersonalityCue::Frustration;
    case SceneId::Pairing:
    case SceneId::LookingForPc:
    case SceneId::ChecksRunning:
    case SceneId::DecisionWaiting:
    case SceneId::ApprovalOnPc:
    case SceneId::PrPending:
        return PersonalityCue::Curiosity;
    case SceneId::NetworkWait:
        return PersonalityCue::NoWifi;
    case SceneId::Online:
    case SceneId::Connected:
    case SceneId::SetupComplete:
    case SceneId::ResponseReady:
    case SceneId::ReplySent:
        return PersonalityCue::Joy;
    case SceneId::DecisionNotice:
    case SceneId::DecisionChoices:
    case SceneId::DecisionConfirm:
        return PersonalityCue::Curiosity;
    case SceneId::ActivityUnavailable:
    case SceneId::EventUnavailable:
        return PersonalityCue::Confusion;
    default:
        return PersonalityCue::None;
    }
}

// scene: Stable identity of the scene being rendered, never a requested future route.
// connection: Reaction already admitted by advance_connection_personality.
// No exceptions: Pure arbitration owns neither time nor observations.
PersonalityCue resolve_scene_personality_cue(SceneId scene, PersonalityCue connection)
{
    const PersonalityCue authored = scene_personality_cue(scene);
    if (scene == SceneId::LookingForPc) {
        // Quiet beats intentionally request no symbol; the route cannot restore a question mark.
        return connection;
    }
    if (authored == PersonalityCue::Fear || authored == PersonalityCue::Frustration) {
        return authored;
    }
    if (connection == PersonalityCue::NoWifi || connection == PersonalityCue::WifiConnected ||
        authored == PersonalityCue::None) {
        return connection;
    }
    if ((scene == SceneId::LookingForPc || scene == SceneId::Connected) &&
        connection != PersonalityCue::None) {
        return connection;
    }
    return authored;
}

} // namespace CODY
