#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

#if defined(_WIN32)
#define CODY_HOST_DISPLAY_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_DISPLAY_API extern "C" __attribute__((visibility("default")))
#endif

// pixels: Receives exactly 466 x 466 native-endian RGB565 pixels from the last completed frame.
// count: Capacity in pixels; must match the physical logical display exactly.
// revision: Receives the monotonically increasing completed-frame identity.
// Exception: Returns invalid arguments or an unavailable/overflowed display state.
CODY_HOST_DISPLAY_API esp_err_t cody_host_display_snapshot(uint16_t *pixels, size_t count, uint32_t *revision);
