/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

// A copied view of the last coherent generation. ready is true only for an
// accepted session with a baseline and a live lease for the matching epoch.
struct CodexDomainStatus {
    uint64_t source = 0;
    uint64_t epoch = 0;
    uint64_t revision = 0;
    uint32_t chats = 0;
    uint32_t turns = 0;
    uint32_t active_calls = 0;
    uint32_t terminal_calls = 0;
    uint8_t protocol_error = 0;
    bool ready = false;
};

// status: Destination for the bounded network-owner copy.
// Returns the last transport error with the coherent summary still copied.
esp_err_t readCodexDomainStatus(CodexDomainStatus &status);

} // namespace LUNASHELL
