#pragma once

#include <cstdint>

#include "esp_err.h"

namespace CODY {

enum class RelayProbeStage : uint8_t { Start, Wifi, Clock, Https, Http, Response, WebSocketUpgrade, WebSocketFrame };

struct RelayProbeResult {
    RelayProbeStage stage = RelayProbeStage::Start;
    esp_err_t error = ESP_OK;
    int http_status = 0;
};

// No parameters: Called once per explicit action on the relay worker.
// Exception: Reports the failed stage and original SDK error when available.
RelayProbeResult check_relay_liveness();

} // namespace CODY
