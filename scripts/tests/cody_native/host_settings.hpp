#pragma once
#include <string>
#include "nvs.h"

namespace HostSettings {

// Explicit fault injection stays in the host executable, never firmware snapshots.
struct State {
    bool initialized = false;
    esp_err_t initialize_error = ESP_OK;
    esp_err_t read_error = ESP_ERR_NVS_NOT_FOUND;
    esp_err_t write_error = ESP_OK;
    unsigned write_attempts = 0;
    std::string saved_value;
};

// No parameters: expose the host fixture for deterministic storage outcomes.
State &state();

// No parameters: discard the preceding test's storage state and injected failures.
void reset();

}  // namespace HostSettings
