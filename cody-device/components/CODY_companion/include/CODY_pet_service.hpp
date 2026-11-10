/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_pet_status.hpp"
#include "esp_err.h"

namespace CODY {

// timestamp_us: Original device monotonic time of a recognized pet tap.
// Exception: Returns the lock or model error without changing the model on rejection.
esp_err_t record_pet_tap(int64_t timestamp_us);

// status: Receives a coherent session-only copy, independent of the display view.
// Exception: Returns the lock error without changing status.
esp_err_t read_pet_status(Domain::PetStatus &status);

} // namespace CODY
