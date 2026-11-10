/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_pet_service.hpp"

#include <mutex>
#include "esp_log.h"

namespace CODY {
namespace {
std::mutex s_pet_mutex;
Domain::PetModel s_pet;
PetObservation s_observation;

// result: Portable model rejection mapped without losing the failure category.
esp_err_t update_error(Domain::PetUpdateResult result)
{
    switch (result) {
    case Domain::PetUpdateResult::Ok: return ESP_OK;
    case Domain::PetUpdateResult::InvalidTime: return ESP_ERR_INVALID_ARG;
    case Domain::PetUpdateResult::OutOfOrder: return ESP_ERR_INVALID_STATE;
    case Domain::PetUpdateResult::CapacityExceeded: return ESP_ERR_INVALID_SIZE;
    }
    return ESP_ERR_INVALID_STATE;
}
} // namespace

// timestamp_us: Original device monotonic time of a recognized pet tap.
// observation: Receives accepted facts and the latest delivery result under one lock.
// Exception: Rejection preserves tap facts and publishes the original error.
esp_err_t record_pet_tap(int64_t timestamp_us, PetObservation &observation)
{
    {
        // Readers and writers only copy/update a few scalars under this lock.
        // No callbacks, rendering, logging or I/O may run inside this section.
        std::lock_guard<std::mutex> lock(s_pet_mutex);
        s_observation.last_attempt_at_us = timestamp_us;
        s_observation.last_update_error = timestamp_us <= 0 ? ESP_ERR_INVALID_ARG :
            update_error(s_pet.record_tap(static_cast<uint64_t>(timestamp_us)));
        s_observation.status = s_pet.snapshot();
        observation = s_observation;
    }
    if (observation.last_update_error != ESP_OK) {
        ESP_LOGE("CODY_pet", "Tap rejected at %lld us (%s)",
            static_cast<long long>(timestamp_us), esp_err_to_name(observation.last_update_error));
    }
    return observation.last_update_error;
}

// observation: Receives the coherent facts and most recent delivery outcome.
void read_pet_status(PetObservation &observation)
{
    std::lock_guard<std::mutex> lock(s_pet_mutex);
    observation = s_observation;
}

} // namespace CODY
