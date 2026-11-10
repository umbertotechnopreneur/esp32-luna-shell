/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_pet_status.hpp"
#include "esp_err.h"

namespace CODY {

// Session observation owned by the pet service, including the last delivery result.
struct PetObservation {
    Domain::PetStatus status{};
    int64_t last_attempt_at_us = 0;
    esp_err_t last_update_error = ESP_OK;
};

// timestamp_us: Original device monotonic time of a recognized pet tap.
// observation: Receives the resulting facts and original delivery error together.
// Exception: Rejected events leave tap facts unchanged and publish their failure.
esp_err_t record_pet_tap(int64_t timestamp_us, PetObservation &observation);

// observation: Receives a coherent session-only copy, independent of the display view.
void read_pet_status(PetObservation &observation);

} // namespace CODY
