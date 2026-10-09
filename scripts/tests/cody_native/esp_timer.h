#pragma once

#include <cstdint>

// No parameters: host fixtures supply deterministic monotonic time, never a device clock.
extern "C" int64_t esp_timer_get_time(void);
