#pragma once

#include "CODY_codex_event_presentation.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include <cstring>
#include <limits>

// Simulator-only fixed event observations. No authenticated event or command is sent.
namespace CODY::host_studio_events {
using event_presentation::EventStage;
using event_presentation::Route;
using LUNASHELL::CodexEventKind;
using LUNASHELL::CodexEventState;

struct Definition {
    const char *target;
    CodexEventKind kind;
    CodexEventState state;
    EventStage stage;
    Route route;
};

inline constexpr Definition kDefinitions[]{
    {"cody.event.pr-pending", CodexEventKind::PrPending, CodexEventState::Pending, EventStage::Notice, Route::PrPending21},
    {"cody.event.checks-running", CodexEventKind::ChecksRunning, CodexEventState::Pending, EventStage::Notice, Route::ChecksRunning22},
    {"cody.event.checks-failed", CodexEventKind::ChecksFailed, CodexEventState::Pending, EventStage::Notice, Route::ChecksFailed23},
    {"cody.event.check-failures", CodexEventKind::ChecksFailed, CodexEventState::Pending, EventStage::Failures, Route::CheckFailures24},
    {"cody.event.question", CodexEventKind::Question, CodexEventState::Pending, EventStage::Notice, Route::DecisionNotice26},
    {"cody.event.question-choices", CodexEventKind::Question, CodexEventState::Pending, EventStage::Choices, Route::DecisionChoices27},
    {"cody.event.question-queue", CodexEventKind::Question, CodexEventState::Pending, EventStage::Questions, Route::DecisionQueue},
    {"cody.event.sending-reply", CodexEventKind::Question, CodexEventState::Sending, EventStage::Notice, Route::DecisionWaiting},
    {"cody.event.reply-sent", CodexEventKind::Question, CodexEventState::Delivered, EventStage::Notice, Route::ReplySent31},
    {"cody.event.notice", CodexEventKind::Notice, CodexEventState::Pending, EventStage::Notice, Route::CodexNotice},
    {"cody.event.approval-pc", CodexEventKind::ApprovalOnPc, CodexEventState::Pending, EventStage::Notice, Route::ApprovalOnPc},
};

// target: Complete stable host semantic identity, never a Python enum ordinal.
// Returns its immutable definition, or nullptr without guessing a route.
// Exception: No exceptions; null and unknown identities are rejected.
inline const Definition *definition(const char *target)
{
    if (!target) return nullptr;
    for (const auto &item : kDefinitions) if (std::strcmp(target, item.target) == 0) return &item;
    return nullptr;
}

// target: Complete named Studio target.
// Exception: No exceptions; unknown targets never acquire a synthetic event.
inline bool is_target(const char *target) { return definition(target) != nullptr; }

// target: Complete named Studio target.
// stage: Receives the original presenter's typed local stage.
// route: Receives its expected native destination.
// Exception: No exceptions; outputs are unchanged for an unknown target.
inline bool describe(const char *target, EventStage &stage, Route &route)
{
    const auto *item = definition(target);
    if (!item) return false;
    stage = item->stage;
    route = item->route;
    return true;
}

// target: Exact declared synthetic event fixture.
// now_us: Current monotonic scenario instant for the observation owner.
// event: Caller-owned staging buffer; never publish unless this function returns true.
// Exception: No exceptions; invalid input or route mismatch returns false without publication.
inline bool fill(const char *target, int64_t now_us, LUNASHELL::CodexEventSnapshot &event)
{
    const auto *item = definition(target);
    if (!item || now_us <= 0 || now_us > std::numeric_limits<int64_t>::max() - 180000000LL) return false;
    auto &value = event;
    value = {};
    value.health = LUNASHELL::DeviceHealth::Ready;
    value.last_error = ESP_OK;
    value.data_valid = true;
    value.data_stale = false;
    value.last_attempt_at_us = value.last_success_at_us = now_us;
    value.kind = item->kind;
    value.state = item->state;
    value.revision = 1;
    value.expires_at_us = now_us + 180000000LL;
    std::strcpy(value.request_id, "11111111111111111111111111111111");
    std::strcpy(value.context, "Synthetic Studio");
    const char *title = "Review update";
    const char *body = "Review the proposed change.";
    switch (item->kind) {
    case CodexEventKind::PrPending:
        title = "PR pending"; body = "A pull request awaits review."; break;
    case CodexEventKind::ChecksRunning:
        title = "Checks running"; body = "Checks are still running."; break;
    case CodexEventKind::ChecksFailed:
        title = "Checks failed"; body = "Some checks need attention."; break;
    case CodexEventKind::Notice:
        title = "Codex notice"; body = "A new update is available."; break;
    case CodexEventKind::ApprovalOnPc:
        title = "Approval on PC"; body = "Review this request on your PC."; break;
    default: break;
    }
    std::strcpy(value.title, title);
    std::strcpy(value.body, body);
    // A short synthetic origin and omitted optional status let meaningful progress
    // and PC-action instructions remain within the original five-line viewport.
    std::strcpy(value.origin_title, "Studio chat");
    if (item->kind == CodexEventKind::ChecksRunning || item->kind == CodexEventKind::ChecksFailed) {
        value.progress_valid = true;
        value.passed = 1;
        value.total = 3;
    }
    if (item->kind == CodexEventKind::ChecksFailed) {
        value.failure_count = 2;
        std::strcpy(value.failed_checks[0], "Synthetic build check");
        std::strcpy(value.failed_checks[1], "Synthetic test check");
    }
    if (item->kind == CodexEventKind::Question) {
        value.choice_count = 2;
        std::strcpy(value.choices[0].id, "approve");
        std::strcpy(value.choices[0].label, "Approve");
        std::strcpy(value.choices[0].detail, "Proceed with the change");
        value.choices[0].tone = LUNASHELL::CodexChoiceTone::Positive;
        std::strcpy(value.choices[1].id, "decline");
        std::strcpy(value.choices[1].label, "Decline");
        std::strcpy(value.choices[1].detail, "Keep the current result");
        value.choices[1].tone = LUNASHELL::CodexChoiceTone::Negative;
        value.pending_question_count = value.pending_entry_count = 2;
        std::strcpy(value.pending_questions[0].request_id, "11111111111111111111111111111111");
        std::strcpy(value.pending_questions[0].context, "Synthetic Studio");
        std::strcpy(value.pending_questions[0].title, "Review update");
        std::strcpy(value.pending_questions[1].request_id, "22222222222222222222222222222222");
        std::strcpy(value.pending_questions[1].context, "Synthetic Studio");
        std::strcpy(value.pending_questions[1].title, "Review another change");
        if (item->state == CodexEventState::Sending || item->state == CodexEventState::Delivered) {
            std::strcpy(value.selected_id, "approve");
            // A selected question is not selectable again in the pending queue.
            // A delivered question is also excluded from the open-question total.
            value.pending_questions[0] = value.pending_questions[1];
            value.pending_questions[1] = {};
            value.pending_entry_count = 1;
            if (item->state == CodexEventState::Delivered) value.pending_question_count = 1;
        }
    }
    const bool admitted = item->state == CodexEventState::Sending;
    if (event_presentation::destination(value, item->stage, admitted, ESP_OK) != item->route) return false;
    return true;
}
} // namespace CODY::host_studio_events
