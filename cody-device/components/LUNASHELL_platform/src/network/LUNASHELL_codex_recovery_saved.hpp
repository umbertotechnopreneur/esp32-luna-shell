/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_types.hpp"
#include "LUNASHELL_codex_recovery.hpp"
#include "LUNASHELL_codex_events.hpp"

namespace LUNASHELL::codex_recovery {

// peer: Active persisted PC identity, or null with key before commissioning.
// key: Corresponding retained key; never stored in the checkpoint.
void load(const uint8_t *peer, const uint8_t *key);

// No parameters: retire this pairing's checkpoint and confirm durable absence.
esp_err_t erase();

// peer: Active retained PC ID borrowed by the sole CCP worker.
// key: Corresponding retained key borrowed during the synchronous settings call.
// now_us: Monotonic time used only for pacing.
void checkpoint(const uint8_t *peer, const uint8_t *key, uint64_t now_us);

// chat: Caller-validated live bookmark or explicit zero to clear.
// now_us: Monotonic admission time. No flash I/O in this call.
esp_err_t queue_selection(const CODY::Domain::ChatId &chat, uint64_t now_us);

// chat: Verified durable bookmark, untouched on errors or a pending write.
esp_err_t read_selection(CODY::Domain::ChatId &chat);

// result: Copied durable historical projection, untouched on contention.
esp_err_t read_recovery(CodexRecoveryObservation &result);

// request: Exact authenticated CCP1 question ticket in lowercase hexadecimal.
// choice: Immutable semantic choice, required for Received/Delivered.
// state: Exact authenticated broker state; transport expiry is not resolution.
// now_us: Device monotonic time for coalescing.
void observe(const char *request, const char *choice, CodexEventState state, uint64_t now_us);

// request: Exact currently displayed authenticated question ticket.
// choice: Explicitly confirmed choice; no authority from restored observations.
// now_us: Monotonic admission time. This queues write-ahead durability.
esp_err_t queue_answer(const char *request, const char *choice, uint64_t now_us);

// request: Exact confirmed question ticket from current live state.
// choice: Exact confirmed choice.
// Returns OK only after commit/readback, before the first wire send.
esp_err_t answer_ready(const char *request, const char *choice);

} // namespace LUNASHELL::codex_recovery
