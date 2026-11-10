/* Copyright (c) 2026 Umberto Giacobbi */
#include "LUNASHELL_assistant_service.hpp"
#include "LUNASHELL_codex_state_status.hpp"

#include <limits>
#include <mutex>
#include "esp_log.h"

namespace LUNASHELL {
namespace {
struct Registration {
    AssistantOwnerId owner{};
    AssistantStatusReader read = nullptr;
};
using Registrations = std::array<Registration, kAssistantOwnerCapacity>;
std::mutex s_registry_mutex;
Registrations s_owners{{{kCcpAssistantOwner, readCodexAssistantStatus}}};
uint64_t s_registry_revision = 1;

// text: Bounded source metadata supplied by an owner, including a terminator.
template<std::size_t Size>
bool valid_identity_text(const std::array<char, Size> &text)
{
    if (text[0] == '\0') return false;
    for (char character : text) {
        if (character == '\0') return true;
        if (character < 0x20 || character > 0x7e) return false;
    }
    return false;
}

// status: Copied source projection; reject malformed owner output before presentation.
bool valid_status(const CODY::Domain::AssistantStatus &status)
{
    return status.source.valid() && status.epoch.valid() && status.revision != 0 &&
        status.generation != 0 && valid_identity_text(status.provider) &&
        valid_identity_text(status.native_namespace);
}

// owner: Adapter whose registration failed.
// error: Original failure to log and return outside the registry lock.
esp_err_t registration_error(AssistantOwnerId owner, esp_err_t error)
{
    ESP_LOGE("AssistantStatus", "Registration owner=%llu failed (%s)",
        static_cast<unsigned long long>(owner.value), esp_err_to_name(error));
    return error;
}

// owner: Validated adapter identity.
// reader: Validated process-lifetime reader.
// Exception: Rejects duplicates/capacity; never invokes a reader under the lock.
esp_err_t add_owner(AssistantOwnerId owner, AssistantStatusReader reader)
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    Registration *free_slot = nullptr;
    for (auto &entry : s_owners) {
        if (entry.owner == owner || entry.read == reader) return ESP_ERR_INVALID_STATE;
        if (!entry.owner.valid() && free_slot == nullptr) free_slot = &entry;
    }
    if (free_slot == nullptr) return ESP_ERR_NO_MEM;
    if (s_registry_revision == std::numeric_limits<uint64_t>::max()) return ESP_ERR_INVALID_STATE;
    *free_slot = {owner, reader};
    ++s_registry_revision;
    return ESP_OK;
}

// owner: Non-reserved adapter identity to remove.
// Exception: Missing identity or revision exhaustion leaves the registry unchanged.
esp_err_t remove_owner(AssistantOwnerId owner)
{
    std::lock_guard<std::mutex> lock(s_registry_mutex);
    for (auto &entry : s_owners) {
        if (entry.owner != owner) continue;
        if (s_registry_revision == std::numeric_limits<uint64_t>::max()) return ESP_ERR_INVALID_STATE;
        entry = {};
        ++s_registry_revision;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}
} // namespace

// No parameters: resets caller storage without another full catalog on the stack.
void AssistantCatalog::reset()
{
    for (auto &row : rows) row = {};
    registry_revision = 0;
    count = 0;
    enumerated = false;
}

// source: Stable source identity from a previously observed owner.
// status: Replaced only by one unambiguous successful row.
// Exception: Missing sources in partial catalogs return the original read error.
esp_err_t AssistantCatalog::copy_source(CODY::Domain::SourceId source,
                                       CODY::Domain::AssistantStatus &status) const
{
    if (!source.valid()) return ESP_ERR_INVALID_ARG;
    if (!enumerated || count > rows.size()) return ESP_ERR_INVALID_STATE;
    const AssistantObservation *found = nullptr;
    esp_err_t missing_error = ESP_OK;
    for (uint8_t index = 0; index < count; ++index) {
        const auto &row = rows[index];
        if (row.error != ESP_OK && missing_error == ESP_OK) missing_error = row.error;
        if (!row.has_status || row.status.source != source) continue;
        if (found != nullptr) return ESP_ERR_INVALID_STATE;
        found = &row;
    }
    if (found == nullptr) return missing_error == ESP_OK ? ESP_ERR_NOT_FOUND : missing_error;
    if (found->error != ESP_OK) return found->error;
    status = found->status;
    return ESP_OK;
}

// owner: Adapter identity; CCP reserves value one.
// reader: Process-lifetime bounded observation callback.
// Exception: Reports registration rejection without replacing another adapter.
esp_err_t registerAssistantOwner(AssistantOwnerId owner, AssistantStatusReader reader)
{
    const auto error = !owner.valid() || reader == nullptr ? ESP_ERR_INVALID_ARG : add_owner(owner, reader);
    return error == ESP_OK ? ESP_OK : registration_error(owner, error);
}

// owner: Detachable adapter identity; outstanding copies may still invoke its reader.
// Exception: Reserved/missing identities and revision exhaustion remain explicit.
esp_err_t unregisterAssistantOwner(AssistantOwnerId owner)
{
    const auto error = !owner.valid() ? ESP_ERR_INVALID_ARG :
        owner == kCcpAssistantOwner ? ESP_ERR_NOT_SUPPORTED : remove_owner(owner);
    return error == ESP_OK ? ESP_OK : registration_error(owner, error);
}

// catalog: Caller-owned output; no full catalog temporary is created on this stack.
// Exception: Returns the first source error, with every owner result retained.
esp_err_t readAssistantCatalog(AssistantCatalog &catalog)
{
    catalog.reset();
    Registrations owners;
    {
        std::lock_guard<std::mutex> lock(s_registry_mutex);
        owners = s_owners;
        catalog.registry_revision = s_registry_revision;
    }
    // No registry lock across callbacks: source owners retain their own locks.
    // These observations are independent, not a cross-source atomic snapshot.
    for (const auto &entry : owners) {
        if (!entry.owner.valid()) continue;
        auto &row = catalog.rows[catalog.count++];
        row.owner = entry.owner;
        row.error = entry.read(row.status);
        if (row.error == ESP_OK && !valid_status(row.status)) row.error = ESP_ERR_INVALID_RESPONSE;
        row.has_status = row.error == ESP_OK;
        if (!row.has_status) row.status = {};
    }
    catalog.enumerated = true;
    for (uint8_t index = 0; index < catalog.count; ++index) {
        auto &row = catalog.rows[index];
        if (!row.has_status) continue;
        for (uint8_t previous = 0; previous < index; ++previous) {
            auto &other = catalog.rows[previous];
            if (other.has_status && other.status.source == row.status.source) {
                other.error = ESP_ERR_INVALID_STATE;
                row.error = ESP_ERR_INVALID_STATE;
            }
        }
    }
    for (uint8_t index = 0; index < catalog.count; ++index) {
        if (catalog.rows[index].error != ESP_OK) return catalog.rows[index].error;
    }
    return ESP_OK;
}

} // namespace LUNASHELL
