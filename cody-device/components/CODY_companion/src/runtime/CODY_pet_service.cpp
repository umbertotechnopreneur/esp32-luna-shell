/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_pet_service.hpp"

#include <mutex>

namespace CODY {
namespace {
std::mutex s_pet_mutex;
Domain::PetModel s_pet;
} // namespace

// timestamp_us: Original device monotonic time of a recognized pet tap.
// Exception: Returns the lock or typed model error without mutation on rejection.
esp_err_t record_pet_tap(int64_t timestamp_us)
{
    if (timestamp_us <= 0) return ESP_ERR_INVALID_ARG;
    std::unique_lock<std::mutex> lock(s_pet_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return ESP_ERR_TIMEOUT;
    switch (s_pet.record_tap(static_cast<uint64_t>(timestamp_us))) {
    case Domain::PetUpdateResult::Ok: return ESP_OK;
    case Domain::PetUpdateResult::InvalidTime: return ESP_ERR_INVALID_ARG;
    case Domain::PetUpdateResult::OutOfOrder: return ESP_ERR_INVALID_STATE;
    case Domain::PetUpdateResult::CapacityExceeded: return ESP_ERR_INVALID_SIZE;
    }
    return ESP_ERR_INVALID_STATE;
}

// status: Receives a coherent model copy only after successful lock acquisition.
// Exception: Returns the lock error without changing status.
esp_err_t read_pet_status(Domain::PetStatus &status)
{
    std::unique_lock<std::mutex> lock(s_pet_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return ESP_ERR_TIMEOUT;
    status = s_pet.snapshot();
    return ESP_OK;
}

} // namespace CODY
