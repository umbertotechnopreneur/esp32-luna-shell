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

#include "LUNASHELL_codex_events.hpp"

namespace LUNASHELL {

// Owner-held references keep the portable decision engine off task stacks.
struct CodexTransactionView {
    CodexEventPayload &payload;
    char (&request_id)[33];
    CodexEventState &state;
    int64_t &expires_at_us;
    esp_err_t &last_error;
    char (&selection_request_id)[33];
};

struct CodexTransactionEffect {
    bool changed = false;
    bool wake = false;
    bool clear_reply = false;
    bool acknowledge = false;
    bool unavailable = false;
};

// token: Exact EVENT wire token, including the broker's unavailable observation.
// state: Receives a supported state; Sending and Rejected are never accepted wire tokens.
bool decodeCodexEventState(const char *token, CodexEventState &state);

// current: Exclusive owner-held live transaction, modified only after admission.
// request_id: Exact lowercase hexadecimal identity from the authenticated envelope.
// state: Wire state; Rejected denotes the special unavailable observation.
// ttl: Original bounded lifetime in seconds, never extended on retransmission.
// candidate: Decoded immutable payload, or nullptr for None/Unavailable.
// now_us: Explicit monotonic time from the acquisition/transport owner.
// effect: Small publication/transport decisions; owner performs their side effects.
// Exception: Returns precise schema/state/identity errors without partially applying a frame.
esp_err_t applyCodexEvent(CodexTransactionView current, const char *request_id,
    CodexEventState state, uint16_t ttl, const CodexEventPayload *candidate,
    int64_t now_us, CodexTransactionEffect &effect);

// current: Exclusive owner-held transaction.
// request_id: Identity bound to the authenticated acknowledgement.
// choice_id: Semantic choice bound to the acknowledgement.
// status: Exact wire acknowledgement status.
// now_us: Owner's explicit monotonic receive time.
// effect: Reports admitted mutation even when the recipient reports an error.
// Exception: Returns admission errors, or actual timeout/cancellation/conflict errors after mutation.
esp_err_t applyCodexReplyAck(CodexTransactionView current, const char *request_id,
    const char *choice_id, const char *status, int64_t now_us, CodexTransactionEffect &effect);

// current: Exclusive owner-held transaction.
// now_us: Explicit monotonic time; expiry never fabricates a response.
// effect: Reports expiry or informational withdrawal for owner publication.
void expireCodexTransaction(CodexTransactionView current, int64_t now_us, CodexTransactionEffect &effect);

// current: Exclusive owner-held current question.
// request_id: Identity displayed at explicit Send confirmation.
// choice_id: Semantic choice admitted independently of display order.
// fresh: Owner-confirmed authenticated connection and canonical freshness.
// now_us: Explicit monotonic confirmation time.
// Exception: Returns invalid identity/choice/state/freshness or timeout; no mutation on failure.
esp_err_t submitCodexChoice(CodexTransactionView current, const char *request_id,
    const char *choice_id, bool fresh, int64_t now_us);

// current: Exclusive owner-held unanswered question and advertised queue.
// request_id: Full request identity selected from that queue; not a choice ID.
// fresh: Authenticated owner freshness for a new navigation request.
// now_us: Explicit monotonic admission time, preserving the original question deadline.
// Exception: Returns identity/state/freshness/expiry errors without altering the current question.
esp_err_t selectCodexQuestion(CodexTransactionView current, const char *request_id,
    bool fresh, int64_t now_us);

// current: Exclusive owner-held transaction.
// request_id: Exact informational/terminal identity explicitly dismissed.
// Exception: Returns INVALID_STATE for an open question or changed identity.
esp_err_t dismissCodexTransaction(CodexTransactionView current, const char *request_id);

} // namespace LUNASHELL
