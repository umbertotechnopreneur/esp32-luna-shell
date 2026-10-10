#pragma once
#include <cstdint>
#include "esp_err.h"
// The host neither opens nor emulates firmware NVS.
using nvs_handle_t = uint32_t;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
