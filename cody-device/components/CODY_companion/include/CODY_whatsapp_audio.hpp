#pragma once

#include "LUNASHELL_platform.h"

namespace CODY {

// status: Receives the shared audio owner's actual admission or suppression.
// Exception: Returns the original audio-owner error; no delayed replay is queued.
esp_err_t notify_whatsapp_audio(lunashell_audio_cue_status_t &status);

} // namespace CODY
