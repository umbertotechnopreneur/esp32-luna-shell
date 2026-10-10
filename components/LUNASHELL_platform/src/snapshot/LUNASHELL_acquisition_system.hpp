#pragma once

#include "esp_err.h"

namespace LUNASHELL::snapshot_detail::system_acquisition {
// No parameters: acquire measured memory and CPU telemetry.
// Exception: Returns original acquisition or canonical publication failures.
esp_err_t sample();
}  // namespace LUNASHELL::snapshot_detail::system_acquisition
