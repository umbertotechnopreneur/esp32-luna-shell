#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace LUNASHELL::acquisition_pmu {
// Exception: Returns allocation failure if PMU synchronization cannot be initialized.
esp_err_t initialize_synchronization();
// bus: Existing BSP-owned bus borrowed by the PMU acquisition component.
// Exception: Returns the original PMU initialization or canonical publication failure.
esp_err_t initialize(i2c_master_bus_handle_t bus);
// Component readiness is queried only by the acquisition orchestrator.
bool ready();
// phase: Original acquisition context used for publication and logging.
// Exception: Returns the original hardware, locking or canonical publication failure.
esp_err_t sample(const char *phase);
}  // namespace LUNASHELL::acquisition_pmu
