/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_state_query.hpp"
#include "esp_err.h"

namespace LUNASHELL {

using AssistantOwnerId = CODY::Domain::Token<struct AssistantOwnerTag>;
inline constexpr AssistantOwnerId kCcpAssistantOwner{1};
inline constexpr uint8_t kAssistantOwnerCapacity = 4;

// One callback represents one source owner. It copies only committed observations,
// preserves freshness, and does no network I/O. Its code/state must live for the
// process lifetime, including after unregister (an in-flight read may finish).
// Each owner synchronizes its own copy; readers must not recursively enumerate.
// On failure the service discards its output and retains the original error.
using AssistantStatusReader = esp_err_t (*)(CODY::Domain::AssistantStatus &status);

struct AssistantObservation {
    AssistantOwnerId owner{};
    CODY::Domain::AssistantStatus status{};
    esp_err_t error = ESP_ERR_INVALID_STATE;
    bool has_status = false; // False on read failure; true also for an identity conflict.
};

// Caller-owned projections, never a second mutable source store. Includes failed
// owners, so a partial read cannot masquerade as an empty assistant list.
struct AssistantCatalog {
    std::array<AssistantObservation, kAssistantOwnerCapacity> rows{};
    uint64_t registry_revision = 0;
    uint8_t count = 0;
    bool enumerated = false; // Captured the complete registered owner set at this revision.

    // No parameters: invalidates old observations without a catalog-sized temporary.
    void reset();
    // source: Exact source identity; never an array index or provider brand.
    // status: Receives one unambiguous successful observation only on success.
    // Exception: Preserves errors and leaves status unchanged on failure or ambiguity.
    esp_err_t copy_source(CODY::Domain::SourceId source, CODY::Domain::AssistantStatus &status) const;
};
static_assert(sizeof(AssistantCatalog) <= 2304, "Keep catalogs in caller-owned storage");

// owner: Stable adapter identity, distinct from the source identity in its data.
// reader: Process-lifetime callback for one independent committed source owner.
// Exception: Rejects invalid/duplicate registrations or capacity without mutation.
esp_err_t registerAssistantOwner(AssistantOwnerId owner, AssistantStatusReader reader);

// owner: Registered adapter to detach; the built-in CCP slot cannot be removed.
// Exception: Returns the original registry error without removing another owner.
esp_err_t unregisterAssistantOwner(AssistantOwnerId owner);

// catalog: Receives every captured owner, including per-owner failures.
// Exception: Returns the first read/identity error while preserving partial results.
// Consumers enter through DeviceSnapshotService; no hardware is read here.
esp_err_t readAssistantCatalog(AssistantCatalog &catalog);

} // namespace LUNASHELL
