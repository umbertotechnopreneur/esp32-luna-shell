/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_pet_service.hpp"
#include "LUNASHELL_assistant_service.hpp"
#include "LUNASHELL_codex_state_status.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

namespace {
int failures = 0;

// condition: Observable behavior of the production service.
// message: Contract violated by the service under explicit synthetic inputs.
void require(bool condition, const char *message)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

// id: Synthetic source identity; these fixtures never enter firmware snapshots.
CODY::Domain::AssistantStatus source(uint64_t id)
{
    CODY::Domain::AssistantStatus result{};
    result.source = {id};
    result.epoch = {100 + id};
    result.revision = 3;
    result.generation = 7;
    std::snprintf(result.provider.data(), result.provider.size(), "SYNTHETIC-%llu",
        static_cast<unsigned long long>(id));
    std::snprintf(result.native_namespace.data(), result.native_namespace.size(), "test-owner-%llu",
        static_cast<unsigned long long>(id));
    result.health = CODY::Domain::StateViewHealth::Stale;
    result.link.fact.availability = CODY::Domain::Availability::Known;
    result.link.fact.freshness = CODY::Domain::Freshness::Stale;
    result.link.fact.age_us = 900000;
    return result;
}

CODY::Domain::AssistantStatus second = source(22);
esp_err_t second_error = ESP_OK;
bool detach_fourth = false;
esp_err_t detach_error = ESP_OK;

// status: Receives a synthetic source, even on failure to test output quarantine.
esp_err_t read_second(CODY::Domain::AssistantStatus &status)
{
    if (detach_fourth) {
        detach_fourth = false;
        detach_error = LUNASHELL::unregisterAssistantOwner({4});
    }
    status = second;
    return second_error;
}

// status: Receives a third independent synthetic source.
esp_err_t read_third(CODY::Domain::AssistantStatus &status)
{
    status = source(33);
    return ESP_OK;
}

// status: Receives a fourth independent synthetic source.
esp_err_t read_fourth(CODY::Domain::AssistantStatus &status)
{
    status = source(44);
    return ESP_OK;
}

// status: Receives a fifth source that must not fit a full registry.
esp_err_t read_fifth(CODY::Domain::AssistantStatus &status)
{
    status = source(55);
    return ESP_OK;
}

// No parameters: concurrent readers must not reject a valid writer or see torn facts.
void check_pet_delivery()
{
    CODY::PetObservation observation{};
    CODY::read_pet_status(observation);
    require(observation.status.tap_count == 0, "A new session begins without fabricated taps");
    require(CODY::record_pet_tap(100, observation) == ESP_OK && observation.status.tap_count == 1,
        "First observed tap is delivered");
    require(CODY::record_pet_tap(100, observation) == ESP_ERR_INVALID_STATE &&
        observation.status.tap_count == 1 && observation.status.last_tap_at_us == 100,
        "Duplicate delivery is rejected without changing accepted facts");
    require(CODY::record_pet_tap(-7, observation) == ESP_ERR_INVALID_ARG &&
        observation.last_attempt_at_us == -7 && observation.status.tap_count == 1,
        "Invalid original time remains visible without inventing a replacement");
    CODY::PetObservation readback{};
    CODY::read_pet_status(readback);
    require(readback.last_update_error == ESP_ERR_INVALID_ARG && readback.last_attempt_at_us == -7,
        "Delivery error is readable independently of the renderer");

    std::atomic<bool> reader_started{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> coherent{true};
    // No parameters: read the actual service while the caller publishes taps.
    std::thread reader([&] {
        do {
            CODY::PetObservation copy{};
            CODY::read_pet_status(copy);
            if (copy.status.tap_count > 1 &&
                copy.status.last_tap_at_us != 998 + copy.status.tap_count) coherent.store(false);
            reader_started.store(true);
        } while (!finished.load());
    });
    while (!reader_started.load()) std::this_thread::yield();
    for (int64_t index = 0; index < 256; ++index) {
        require(CODY::record_pet_tap(1000 + index, observation) == ESP_OK,
            "A concurrent read cannot drop a valid tap");
    }
    finished.store(true);
    reader.join();
    require(coherent.load() && observation.status.tap_count == 257 &&
        observation.status.last_tap_at_us == 1255 && observation.last_update_error == ESP_OK,
        "Concurrent snapshots stay coherent and accepted taps are counted exactly once");
}

// No parameters: exercise actual registration, independent errors and identity selection.
void check_assistant_owners()
{
    using namespace LUNASHELL;
    AssistantCatalog catalog{};
    require(registerAssistantOwner({}, read_second) == ESP_ERR_INVALID_ARG,
        "Zero owner identity is rejected");
    require(registerAssistantOwner({2}, nullptr) == ESP_ERR_INVALID_ARG,
        "Missing callback is rejected");
    require(registerAssistantOwner({2}, read_second) == ESP_OK, "Second owner registers");
    require(registerAssistantOwner({2}, read_third) == ESP_ERR_INVALID_STATE &&
        registerAssistantOwner({3}, read_second) == ESP_ERR_INVALID_STATE,
        "Duplicate owner or reader never replaces existing ownership");
    require(readAssistantCatalog(catalog) == ESP_OK && catalog.enumerated && catalog.count == 2,
        "Independent owners are enumerated together");
    CODY::Domain::AssistantStatus selected{};
    require(catalog.copy_source({22}, selected) == ESP_OK && selected.source.value == 22 &&
        selected.epoch.value == 122 && selected.generation == 7 &&
        selected.health == CODY::Domain::StateViewHealth::Stale && selected.link.fact.age_us == 900000,
        "Selection uses source identity and preserves original stale evidence");
    second_error = ESP_ERR_TIMEOUT;
    require(readAssistantCatalog(catalog) == ESP_ERR_TIMEOUT && catalog.count == 2 &&
        catalog.rows[1].error == ESP_ERR_TIMEOUT && !catalog.rows[1].has_status &&
        !catalog.rows[1].status.source.valid(),
        "Failed owner stays in a partial catalog without stale-as-current output");
    require(catalog.copy_source({22}, selected) == ESP_ERR_TIMEOUT && selected.source.value == 22,
        "Partial absence preserves the original failure and caller destination");
    require(catalog.copy_source({11}, selected) == ESP_OK,
        "One failing source does not hide another successful source");
    second_error = ESP_OK;
    second = source(11);
    require(readAssistantCatalog(catalog) == ESP_ERR_INVALID_STATE &&
        catalog.rows[0].error == ESP_ERR_INVALID_STATE && catalog.rows[1].error == ESP_ERR_INVALID_STATE &&
        catalog.copy_source({11}, selected) == ESP_ERR_INVALID_STATE,
        "Both owners are rejected when their source identity conflicts");
    second = source(22);
    second.provider.fill('x');
    require(readAssistantCatalog(catalog) == ESP_ERR_INVALID_RESPONSE && !catalog.rows[1].has_status,
        "Unterminated owner metadata is never exposed as valid");
    second = source(22);
    require(registerAssistantOwner({3}, read_third) == ESP_OK &&
        registerAssistantOwner({4}, read_fourth) == ESP_OK &&
        registerAssistantOwner({5}, read_fifth) == ESP_ERR_NO_MEM,
        "Registry capacity rejects additional owners explicitly");
    require(readAssistantCatalog(catalog) == ESP_OK && catalog.count == 4, "All admitted owners remain");
    const auto captured_revision = catalog.registry_revision;
    detach_fourth = true;
    require(readAssistantCatalog(catalog) == ESP_OK && detach_error == ESP_OK && catalog.count == 4 &&
        catalog.registry_revision == captured_revision,
        "Callbacks run outside the registry lock; in-flight enumeration retains its owner set");
    require(readAssistantCatalog(catalog) == ESP_OK && catalog.count == 3 &&
        catalog.registry_revision > captured_revision && catalog.copy_source({44}, selected) == ESP_ERR_NOT_FOUND,
        "Next enumeration observes unregistration without stale rows");
    require(unregisterAssistantOwner({2}) == ESP_OK && readAssistantCatalog(catalog) == ESP_OK &&
        catalog.copy_source({33}, selected) == ESP_OK && selected.source.value == 33,
        "Removing an earlier slot does not change another source identity");
    require(unregisterAssistantOwner(kCcpAssistantOwner) == ESP_ERR_NOT_SUPPORTED,
        "Built-in CCP ownership cannot be accidentally detached");
}
} // namespace

// status: Explicit synthetic CCP owner for this executable only; no device/network I/O.
esp_err_t LUNASHELL::readCodexAssistantStatus(CODY::Domain::AssistantStatus &status)
{
    status = source(11);
    return ESP_OK;
}

// No parameters: the public runtime check runner owns execution and failure reporting.
int main()
{
    check_pet_delivery();
    check_assistant_owners();
    return failures == 0 ? 0 : 1;
}
