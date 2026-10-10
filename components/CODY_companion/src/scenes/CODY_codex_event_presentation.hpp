#pragma once

#include <cstdint>
#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_err.h"

namespace CODY::event_presentation {
enum class Route : uint8_t {
    None, Hello04, Invitation05, Install06, Pairing07, Failed08, Online09,
    MakeItYours10, Look11, ConnectCodex12, SetupComplete13, LookingForPc14,
    Connected15, Settings36, DebugText, CodexChoice, SetupMenu, NetworkWait, CodexReplace,
    CodexForget, CodexCleanup, CodexForgotten,
    Project17, Working18, Response19, Paused20, Ended20, Lost32, CompanionIdle16, ActivityUnavailable,
    PrPending21 = 64, ChecksRunning22 = 65, ChecksFailed23 = 66, CheckFailures24 = 67,
    DecisionNotice26 = 68, DecisionChoices27 = 69, DecisionConfirm27 = 70,
    DecisionWaiting = 71, ReplySent31 = 72, CodexNotice = 73, ApprovalOnPc = 74, EventUnavailable = 75
};
enum class EventStage : uint8_t { Notice = 0, Choices = 1, Confirm = 2, Waiting = 3, Failures = 4 };
enum class EventRetry : uint8_t { None = 0, Reply = 2, Dismiss = 3 };

// route: Stable presentation identity, independent of atlas page numbers or catalog order.
inline bool is_codex_event_route(Route route)
{
    switch (route) {
    case Route::PrPending21:
    case Route::ChecksRunning22:
    case Route::ChecksFailed23:
    case Route::CheckFailures24:
    case Route::DecisionNotice26:
    case Route::DecisionChoices27:
    case Route::DecisionConfirm27:
    case Route::DecisionWaiting:
    case Route::ReplySent31:
    case Route::CodexNotice:
    case Route::ApprovalOnPc:
    case Route::EventUnavailable:
        return true;
    default:
        return false;
    }
}

// event: Retained immutable event projection; no hardware or transport access occurs here.
// id: Stable semantic choice identity independent of card ordering.
inline const LUNASHELL::CodexEventChoice *find_choice(
    const LUNASHELL::CodexEventSnapshot &event, const char *id)
{
    for (const auto &choice : event.choices) {
        if (choice.id[0] != '\0' && std::strcmp(choice.id, id) == 0) {
            return &choice;
        }
    }
    return nullptr;
}

// event: Retained owner snapshot; terminal outcomes take precedence over local navigation.
// stage: Reversible local event navigation.
// reply_admitted: True only after the owner accepts a locally confirmed reply.
// observation_error: Actual canonical observation failure, displayed as unavailable.
inline Route destination(const LUNASHELL::CodexEventSnapshot &event,
                         EventStage stage, bool reply_admitted, esp_err_t observation_error)
{
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    if (observation_error != ESP_OK) {
        return Route::EventUnavailable;
    }
    if (event.kind == Kind::Question && event.state == State::Delivered) {
        return Route::ReplySent31;
    }
    if (LUNASHELL::codexEventIsTerminal(event.state)) {
        return Route::CodexNotice;
    }
    switch (event.kind) {
    case Kind::Question:
        if (reply_admitted || event.state == State::Sending || event.state == State::Received) {
            return Route::DecisionWaiting;
        }
        if (stage == EventStage::Confirm) {
            return Route::DecisionConfirm27;
        }
        return stage == EventStage::Choices ? Route::DecisionChoices27 : Route::DecisionNotice26;
    case Kind::PrPending: return Route::PrPending21;
    case Kind::ChecksRunning:
    case Kind::ChecksPassed: return Route::ChecksRunning22;
    case Kind::ChecksFailed:
        return stage == EventStage::Failures ? Route::CheckFailures24 : Route::ChecksFailed23;
    case Kind::ApprovalOnPc: return Route::ApprovalOnPc;
    case Kind::Notice: return Route::CodexNotice;
    case Kind::None: return Route::Connected15;
    }
    return Route::EventUnavailable;
}

} // namespace CODY::event_presentation
