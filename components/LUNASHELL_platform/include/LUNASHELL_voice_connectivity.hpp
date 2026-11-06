#pragma once

#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {

// deadline_us: Absolute monotonic deadline for this single gesture.
// check_id: Receives its nonzero identity only after the worker is admitted.
// Exception: Returns the actual admission error, including INVALID_STATE while offline or busy.
esp_err_t startVoiceInternetCheck(int64_t deadline_us, uint32_t &check_id);

// check_id: Identity of the gesture's admitted check.
// complete: False only while that check can still finish within its deadline.
// Exception: Returns the original check failure, TIMEOUT for an expired check or INVALID_STATE
// for an absent, superseded or consumed identity. Each completed result can be consumed once.
esp_err_t pollVoiceInternetCheck(uint32_t check_id, bool &complete);

} // namespace LUNASHELL
