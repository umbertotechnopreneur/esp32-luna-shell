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

#include "LUNASHELL_codex_transaction.hpp"
#include <cassert>
#include <cstring>

namespace {
struct Transaction {
    LUNASHELL::CodexEventPayload payload{};
    char id[33]{};
    LUNASHELL::CodexEventState state = LUNASHELL::CodexEventState::None;
    int64_t deadline = 0;
    esp_err_t error = ESP_OK;
    char selection_request_id[33]{};
    // No parameters: Test owner mirrors production owner references without copying snapshots.
    LUNASHELL::CodexTransactionView view()
    {
        return {payload, id, state, deadline, error, selection_request_id};
    }
};
// No parameters: Fixed authored choices represent a decoded question independent of order.
LUNASHELL::CodexEventPayload question()
{
    LUNASHELL::CodexEventPayload payload;
    payload.kind = LUNASHELL::CodexEventKind::Question;
    std::strcpy(payload.title, "Compile?");
    std::strcpy(payload.body, "Choose a build action");
    payload.choice_count = 2;
    std::strcpy(payload.choices[0].id, "compile");
    std::strcpy(payload.choices[0].label, "Compile");
    std::strcpy(payload.choices[1].id, "skip");
    std::strcpy(payload.choices[1].label, "Skip");
    return payload;
}
} // namespace

// No parameters: Regression sequences run the same production mutation engine as both owners.
int main()
{
    using namespace LUNASHELL;
    constexpr char first[] = "11111111111111111111111111111111";
    constexpr char second[] = "22222222222222222222222222222222";
    constexpr char empty[] = "00000000000000000000000000000000";
    Transaction current;
    CodexTransactionEffect effect;
    auto candidate = question();
    assert(applyCodexEvent(current.view(), first, CodexEventState::Pending, 5, &candidate, 0, effect) == ESP_OK);
    assert(effect.wake && current.deadline == 5000000);
    assert(applyCodexEvent(current.view(), second, CodexEventState::Pending, 5, &candidate, 1000000, effect) == ESP_ERR_INVALID_STATE);
    assert(std::strcmp(current.id, first) == 0);
    assert(submitCodexChoice(current.view(), first, "compile", true, 1000000) == ESP_OK);
    assert(current.state == CodexEventState::Sending);
    assert(applyCodexEvent(current.view(), first, CodexEventState::Pending, 5, &candidate, 2000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Sending && current.deadline == 5000000);
    std::strcpy(candidate.selected_id, "compile");
    assert(applyCodexEvent(current.view(), first, CodexEventState::Delivered, 5, &candidate, 5000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Expired && current.error == ESP_ERR_TIMEOUT);
    assert(effect.acknowledge && std::strcmp(current.payload.selected_id, "compile") == 0);
    assert(applyCodexEvent(current.view(), first, CodexEventState::Delivered, 4, &candidate, 6000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Expired);
    candidate.selected_id[0] = '\0';
    assert(applyCodexEvent(current.view(), first, CodexEventState::Pending, 4, &candidate, 6000000, effect) == ESP_ERR_INVALID_STATE);
    assert(dismissCodexTransaction(current.view(), first) == ESP_OK);
    assert(applyCodexEvent(current.view(), second, CodexEventState::Pending, 5, &candidate, 7000000, effect) == ESP_OK);
    assert(applyCodexEvent(current.view(), second, CodexEventState::Rejected, 0, nullptr, 8000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Rejected && current.error == ESP_ERR_NOT_FOUND && effect.unavailable);
    assert(submitCodexChoice(current.view(), second, "skip", true, 8000000) == ESP_ERR_INVALID_STATE);
    assert(dismissCodexTransaction(current.view(), second) == ESP_OK);
    assert(applyCodexEvent(current.view(), first, CodexEventState::Pending, 5, &candidate, 9000000, effect) == ESP_OK);
    assert(submitCodexChoice(current.view(), first, "compile", false, 9000000) == ESP_ERR_INVALID_STATE);
    assert(submitCodexChoice(current.view(), first, "compile", true, 9000000) == ESP_OK);
    assert(applyCodexReplyAck(current.view(), first, "skip", "delivered", 10000000, effect) == ESP_ERR_INVALID_STATE);
    assert(current.state == CodexEventState::Sending);
    assert(applyCodexReplyAck(current.view(), first, "compile", "received", 10000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Received && effect.clear_reply);
    assert(applyCodexReplyAck(current.view(), first, "compile", "delivered", 11000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Delivered);
    assert(applyCodexReplyAck(current.view(), first, "compile", "delivered", 15000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Delivered); // Duplicate recipient delivery is idempotent.
    assert(applyCodexReplyAck(current.view(), first, "compile", "received", 15000000, effect) == ESP_ERR_INVALID_STATE);
    assert(applyCodexEvent(current.view(), empty, CodexEventState::None, 0, nullptr, 15000000, effect) == ESP_OK);
    assert(current.state == CodexEventState::Delivered); // Absence cannot erase delivery evidence.

    // Queued navigation changes the visible request only after an authenticated
    // handoff, and never chooses an answer or renews the original deadline.
    Transaction queued;
    auto pending = question();
    pending.pending_question_count = 2;
    pending.pending_entry_count = 2;
    std::strcpy(pending.pending_questions[0].request_id, first);
    std::strcpy(pending.pending_questions[0].title, "Compile?");
    std::strcpy(pending.pending_questions[1].request_id, second);
    std::strcpy(pending.pending_questions[1].title, "Choose cache");
    assert(applyCodexEvent(queued.view(), first, CodexEventState::Pending, 15, &pending, 0, effect) == ESP_OK);
    assert(selectCodexQuestion(queued.view(), second, false, 1000000) == ESP_ERR_INVALID_STATE);
    assert(selectCodexQuestion(queued.view(), empty, true, 1000000) == ESP_ERR_INVALID_ARG);
    assert(selectCodexQuestion(queued.view(), "33333333333333333333333333333333", true, 1000000) == ESP_ERR_NOT_FOUND);
    assert(selectCodexQuestion(queued.view(), second, true, 1000000) == ESP_OK);
    assert(std::strcmp(queued.id, first) == 0 && queued.payload.selected_id[0] == '\0');
    assert(submitCodexChoice(queued.view(), first, "compile", true, 2000000) == ESP_ERR_INVALID_STATE);
    assert(applyCodexEvent(queued.view(), first, CodexEventState::Pending, 15, &pending, 2000000, effect) == ESP_OK);
    assert(queued.deadline == 15000000 && !effect.changed);
    assert(applyCodexEvent(queued.view(), second, CodexEventState::Pending, 12, &pending, 3000000, effect) == ESP_OK);
    assert(effect.wake && std::strcmp(queued.id, second) == 0 && queued.selection_request_id[0] == '\0');
    assert(queued.deadline == 15000000 && queued.payload.selected_id[0] == '\0');
    assert(selectCodexQuestion(queued.view(), first, true, 4000000) == ESP_OK);
    pending.pending_question_count = 1;
    pending.pending_entry_count = 1;
    pending.pending_questions[0] = pending.pending_questions[1];
    assert(applyCodexEvent(queued.view(), second, CodexEventState::Pending, 10, &pending, 5000000, effect) == ESP_OK);
    assert(effect.changed && queued.selection_request_id[0] == '\0' && queued.error == ESP_ERR_NOT_FOUND);
    assert(selectCodexQuestion(queued.view(), first, true, 5000000) == ESP_ERR_NOT_FOUND);
    pending.pending_entry_count = 2;
    pending.pending_question_count = 2;
    std::strcpy(pending.pending_questions[1].request_id, first);
    assert(applyCodexEvent(queued.view(), second, CodexEventState::Pending, 9, &pending, 6000000, effect) == ESP_OK);
    assert(selectCodexQuestion(queued.view(), first, true, 6000000) == ESP_OK);
    assert(applyCodexEvent(queued.view(), second, CodexEventState::Rejected, 0, nullptr, 7000000, effect) == ESP_OK);
    assert(queued.state == CodexEventState::Rejected && queued.selection_request_id[0] == '\0');
    return 0;
}
