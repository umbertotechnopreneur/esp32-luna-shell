#pragma once
#include <cstdint>
#include "esp_err.h"

// Host-only declarations for the real settings header; no NVS driver is linked.
using nvs_handle_t = uint32_t;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
