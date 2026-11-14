#pragma once

// Host-only subset needed by the device onboarding action worker. These types
// are not available to, or compiled into, the ESP32 firmware.
#include "sdkconfig.h"
#include <cstdint>
#include <mutex>

using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using TickType_t = uint32_t;
using StackType_t = uint32_t;

inline constexpr BaseType_t pdTRUE = 1;
inline constexpr BaseType_t pdFALSE = 0;
inline constexpr BaseType_t pdPASS = 1;
inline constexpr BaseType_t pdFAIL = 0;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline constexpr UBaseType_t tskIDLE_PRIORITY = 0;

// Host tick duration is exactly one millisecond; this is not physical timing evidence.
#define pdMS_TO_TICKS(milliseconds) static_cast<TickType_t>(milliseconds)

// Critical sections serialize host threads; they do not emulate interrupt masking.
using portMUX_TYPE = std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}

// mux: Caller-owned host critical-section mutex.
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { mux->lock(); }

// mux: Same critical-section mutex previously entered by this host thread.
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { mux->unlock(); }
