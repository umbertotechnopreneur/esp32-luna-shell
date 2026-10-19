#pragma once

#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

// Presence describes authenticated MCP client transports, not chat phases or OS windows.
enum class CodexAppPresenceState : uint8_t { Unavailable, Open, Closed };

struct CodexAppPresenceSnapshot {
    CodexAppPresenceState state = CodexAppPresenceState::Unavailable;
    bool data_valid = false;
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    int64_t received_at_us = 0;
    uint64_t source_age_at_receive_us = 0;
    uint64_t sequence = 0;
};
static_assert(sizeof(CodexAppPresenceSnapshot) <= 40, "App presence must remain a bounded scalar projection");

namespace codex_presence {
constexpr uint64_t kTransportTtlUs = 8000000;
constexpr uint64_t kSourceLeaseUs = 60000000;

// presence: Owner projection; original receipt, source age and sequence remain evidence.
// error: Actual source, transport, lifecycle or clock failure; never successful closure.
// Returns true only when the public state changes.
inline bool invalidate(CodexAppPresenceSnapshot &presence, esp_err_t error)
{
    const bool changed = presence.state != CodexAppPresenceState::Unavailable ||
        presence.data_valid || presence.last_error != error;
    presence.state = CodexAppPresenceState::Unavailable;
    presence.data_valid = false;
    presence.last_error = error;
    return changed;
}

// presence: Canonical source observation; this helper cannot create fresh evidence.
// now_us: Device monotonic observation time, never compared with a PC timestamp.
// Returns true when the existing transport or source lease expires.
inline bool age(CodexAppPresenceSnapshot &presence, int64_t now_us)
{
    if (!presence.data_valid) {
        return false;
    }
    if (presence.received_at_us <= 0 || now_us < presence.received_at_us) {
        return invalidate(presence, ESP_ERR_INVALID_STATE);
    }
    const uint64_t elapsed = static_cast<uint64_t>(now_us - presence.received_at_us);
    if (elapsed >= kTransportTtlUs ||
        (presence.state == CodexAppPresenceState::Open &&
         (presence.source_age_at_receive_us >= kSourceLeaseUs ||
          elapsed >= kSourceLeaseUs - presence.source_age_at_receive_us))) {
        return invalidate(presence, ESP_ERR_TIMEOUT);
    }
    return false;
}

// presence: Existing canonical owner; rejected candidates never mutate it.
// state: Parsed authenticated app state, independent of chat and LAN status.
// source_error: Explicit unavailable reason; successful Open/Closed use ESP_OK.
// sequence: Strictly increasing sequence within the current authenticated READY nonce.
// source_age_us: Genuine host duration at transmission, not a cross-machine timestamp.
// received_at_us: Original local packet admission time.
// last_sequence: Current nonce replay barrier; reset only when READY admits a new nonce.
// Exception: Returns INVALID_ARG/INVALID_STATE for malformed, expired or replayed evidence.
inline esp_err_t accept(CodexAppPresenceSnapshot &presence, CodexAppPresenceState state,
                        esp_err_t source_error, uint64_t sequence, uint64_t source_age_us,
                        int64_t received_at_us, uint64_t &last_sequence)
{
    if (sequence == 0 || sequence <= last_sequence || received_at_us <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state != CodexAppPresenceState::Open && state != CodexAppPresenceState::Closed &&
        state != CodexAppPresenceState::Unavailable) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool unavailable = state == CodexAppPresenceState::Unavailable;
    if ((unavailable && (source_error == ESP_OK || source_age_us != 0)) ||
        (!unavailable && source_error != ESP_OK) || source_age_us > UINT32_MAX * uint64_t{1000} ||
        (state == CodexAppPresenceState::Open && source_age_us >= kSourceLeaseUs)) {
        return ESP_ERR_INVALID_ARG;
    }
    last_sequence = sequence;
    presence.state = state;
    presence.data_valid = !unavailable;
    presence.last_error = source_error;
    presence.received_at_us = received_at_us;
    presence.source_age_at_receive_us = source_age_us;
    presence.sequence = sequence;
    return ESP_OK;
}
} // namespace codex_presence
} // namespace LUNASHELL
