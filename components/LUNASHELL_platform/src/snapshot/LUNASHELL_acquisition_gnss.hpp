#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace LUNASHELL::snapshot_detail::gnss {
// bus: Shared I2C bus owned by the hardware initialization stage.
// Exception: Returns the receiver initialization or publication failure.
esp_err_t initialize(i2c_master_bus_handle_t bus);

// Readiness reflects confirmed initialization; it does not imply a current fix.
bool ready();

// phase: Acquisition phase used for existing periodic retry and log policy.
// Exception: Returns the real deferred read, acquisition or publication failure.
// The caller holds the canonical acquisition owner's refresh mutex.
esp_err_t sample(const char *phase);

// suspended: Whether the receiver subsystem must enter acknowledged standby.
// Exception: Returns real readiness, locking, receiver or publication failures.
// This operation acquires the canonical acquisition owner's refresh mutex.
esp_err_t set_power_suspended(bool suspended);
}  // namespace LUNASHELL::snapshot_detail::gnss
