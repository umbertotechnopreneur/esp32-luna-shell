/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace CODY::Domain {
struct StateQuery;
struct StateQueryResult;
} // namespace CODY::Domain

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
    bool historical = false;
    esp_err_t storage_error = ESP_OK;
};

// status: Destination for the bounded network-owner copy.
// Returns the last transport error with the coherent summary still copied.
esp_err_t readCodexDomainStatus(CodexDomainStatus &status);

// query: Requested bounded projection or stable page cursor.
// result: Caller-owned copy. A busy/unavailable profile leaves it untouched.
// Exception: Returns ESP_ERR_TIMEOUT on owner contention, or the query failure.
esp_err_t readCodexDomain(const CODY::Domain::StateQuery &query, CODY::Domain::StateQueryResult &result);

} // namespace LUNASHELL
