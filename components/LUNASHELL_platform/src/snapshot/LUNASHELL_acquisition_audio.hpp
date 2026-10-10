#pragma once

#include "esp_err.h"

namespace LUNASHELL::snapshot_detail::audio {
// Exception: Returns original codec initialization or canonical publication failures.
esp_err_t initialize();

// Confirmed codec input topology, used only for acquisition scheduling.
bool microphone_ready();

// phase: Acquisition phase used for diagnostic logging.
// spectrum_requested: Whether the caller holds an explicit spectrum capture lease.
// Exception: Returns original topology, capture or canonical publication failures.
// The caller holds the canonical acquisition owner's refresh mutex.
esp_err_t sample_microphone(const char *phase, bool spectrum_requested);
}  // namespace LUNASHELL::snapshot_detail::audio
