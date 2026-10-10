#pragma once

#include "LUNASHELL_platform.h"
#include "driver/i2c_master.h"

namespace LUNASHELL::snapshot_detail::rtc {
// Small commit token, not a second calendar or snapshot cache.
struct LatchObservation {
    bool valid = false;
    bool oscillator_stop = false;
    uint32_t trusted_write_generation_at_read = 0;
};
// bus: BSP-owned shared bus.
// Exception: Returns original chip initialization failure or explicit exclusion.
esp_err_t initialize(i2c_master_bus_handle_t bus);
// No parameters: readiness reported by the RTC acquisition owner.
bool ready();
// phase: Existing cadence identity.
// observation: Optional commit token borrowed until the synchronous call completes.
// Exception: Returns original acquisition/publication failure.
esp_err_t sample(const char *phase, LatchObservation *observation);
// observation: Integrity token from a successfully committed acquisition stage.
void apply_committed_observation(const LatchObservation &observation);
}  // namespace LUNASHELL::snapshot_detail::rtc
