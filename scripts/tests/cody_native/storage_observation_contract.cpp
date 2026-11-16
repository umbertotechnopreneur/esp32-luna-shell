/* Copyright (c) 2026 Umberto Giacobbi */
#include "LUNASHELL_snapshot_aging.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>

namespace {
// condition: Observable native policy result being verified.
// message: Names the regression when the condition fails.
// Exception: Terminates this host-only contract with a nonzero exit code.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Storage observation contract failed: %s\n", message);
        std::exit(1);
    }
}

// No parameters: coherent measured storage from the sole acquisition owner.
LUNASHELL::StorageSnapshot mounted_storage()
{
    LUNASHELL::StorageSnapshot storage;
    storage.health = LUNASHELL::DeviceHealth::Ready;
    storage.last_error = ESP_OK;
    storage.data_valid = true;
    storage.last_attempt_at_us = 1000000;
    storage.last_success_at_us = 1000000;
    storage.mounted = true;
    storage.card_response_verified = true;
    storage.capacity_bytes = 8ULL * 1024 * 1024 * 1024;
    storage.filesystem_total_bytes = 7ULL * 1024 * 1024 * 1024;
    storage.free_bytes = 3ULL * 1024 * 1024 * 1024;
    return storage;
}

// storage: Aged copy retaining every original measurement and ownership fact.
// original: Source acquisition, independent from the consumer's observation time.
void require_preserved(const LUNASHELL::StorageSnapshot &storage,
    const LUNASHELL::StorageSnapshot &original)
{
    require(storage.last_success_at_us == original.last_success_at_us &&
        storage.last_attempt_at_us == original.last_attempt_at_us,
        "consumer aging cannot renew acquisition timestamps");
    require(storage.capacity_bytes == original.capacity_bytes &&
        storage.filesystem_total_bytes == original.filesystem_total_bytes &&
        storage.free_bytes == original.free_bytes,
        "consumer aging cannot fabricate filesystem measurements");
    require(storage.last_error == original.last_error && storage.health == original.health &&
        storage.mounted == original.mounted && storage.card_response_verified == original.card_response_verified,
        "consumer aging preserves the owner's original availability and error");
}
} // namespace

// No parameters: exercise the production helper used by speech, diagnostics and storage reads.
// Exception: Failed invariants terminate the host process; no hardware or service is accessed.
int main()
{
    using namespace LUNASHELL;
    const auto original = mounted_storage();
    auto snapshot = std::make_unique<DeviceSnapshot>();
    ComponentSnapshot sensor = original;
    age_measured_sample(sensor, 6000000, measured_sample_ttl_us(*snapshot));
    require(!sensor.data_valid && sensor.data_stale, "the default sensor TTL is still five seconds");
    for (const int64_t elapsed_us : {5000000LL, 10000000LL, 59999999LL, 60000000LL, 89999999LL}) {
        auto storage = original;
        age_storage_sample(storage, original.last_success_at_us + elapsed_us);
        require(storage.data_valid && !storage.data_stale,
            "healthy SD remains usable between 60-second filesystem samples");
        require_preserved(storage, original);
    }
    for (const int64_t observed_us : {91000000LL, 91000001LL, 999999LL}) {
        auto storage = original;
        age_storage_sample(storage, observed_us);
        require(!storage.data_valid && storage.data_stale,
            "expired or future-dated SD acquisition is unavailable");
        require_preserved(storage, original);
    }
    auto failed = original;
    failed.data_valid = false;
    failed.last_error = ESP_ERR_TIMEOUT;
    failed.health = DeviceHealth::Degraded;
    failed.mounted = false;
    failed.card_response_verified = false;
    auto observation = failed;
    age_storage_sample(observation, 2000000);
    require(!observation.data_valid, "a recent real SD failure cannot become available");
    require_preserved(observation, failed);
    auto stale = original;
    stale.data_valid = false;
    stale.data_stale = true;
    age_storage_sample(stale, 2000000);
    require(!stale.data_valid && stale.data_stale, "an earlier observation cannot revive stale SD");
    require_preserved(stale, original);
    std::puts("Storage observation contract passed: cadence, expiry, clock regression and source preservation");
    return 0;
}
