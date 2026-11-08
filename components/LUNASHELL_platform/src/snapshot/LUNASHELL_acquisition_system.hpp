#pragma once

#include "esp_err.h"

namespace LUNASHELL::snapshot_detail::system_acquisition {
// No parameters: acquire measured memory and CPU telemetry.
// Exception: Returns original acquisition or canonical publication failures.
esp_err_t sample();
// No parameters: the sampling owner starts a fresh CPU interval after suspension.
// Does not acquire hardware, block a power transition or publish guessed utilization.
void request_processor_baseline();
}  // namespace LUNASHELL::snapshot_detail::system_acquisition
