#pragma once

#include "esp_err.h"
#include <cstdint>

namespace LUNASHELL::sleep {

// wake_after_us: Optional explicit timer in addition to the physical IRQ wake sources.
// The power owner must confirm full transactional standby before calling this.
esp_err_t try_deep_sleep(uint64_t wake_after_us);
// No parameters: whether rejected entry released every hold and wake-only IMU state.
bool recovery_complete();

}  // namespace LUNASHELL::sleep
