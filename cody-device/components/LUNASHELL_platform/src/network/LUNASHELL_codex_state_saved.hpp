/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_state_store.hpp"
#include "esp_err.h"

#include <cstdint>

namespace LUNASHELL::codex_state_saved {

// peer: Current retained PC ID, or null with key when unpaired.
// key: Current persisted and read-back pairing key, never stored in this record.
// Called at startup before the CCP worker begins.
void load(const uint8_t *peer, const uint8_t *key);

// summary: Last complete authenticated domain commit, without entities or text.
// now_us: Device monotonic time used only for flash pacing.
void remember(const CODY::Domain::StateSummary &summary, uint64_t now_us);

// peer: Exact active retained PC ID, borrowed by the sole CCP writer.
// key: Corresponding active retained key.
// now_us: Monotonic time; caller must not hold the CCP lock during settings I/O.
void checkpoint(const uint8_t *peer, const uint8_t *key, uint64_t now_us);

// summary: Historical status from the authenticated, read-back record, if present.
// storage_error: Original settings failure, independent of the wire's health.
// Returns false on contention without changing either output.
bool try_read(CODY::Domain::StateSummary &summary, esp_err_t &storage_error);

// No parameters: explicitly removes the checkpoint and verifies durable absence.
// Called by Forget or authenticated pairing replacement, never by a transient disconnect.
esp_err_t erase();

} // namespace LUNASHELL::codex_state_saved
