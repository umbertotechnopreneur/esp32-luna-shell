#pragma once

#include "esp_err.h"

namespace CODY {

// No parameters: prepare one dormant audio worker for the original shutter cue.
// Exception: Returns the real worker allocation error.
esp_err_t prepare_capture_sound();

// No parameters: queue one shutter after a successfully retired capture frame.
// Exception: Returns the real notification or unavailable-route error.
esp_err_t notify_capture_sound();

} // namespace CODY
