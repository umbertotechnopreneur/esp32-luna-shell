#pragma once
#include <cstdint>

using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using StackType_t = uint32_t;
using TickType_t = uint32_t;
struct StaticTask_t { uint32_t unused; };
struct StaticSemaphore_t { bool available; };
using TaskHandle_t = StaticTask_t *;
using SemaphoreHandle_t = StaticSemaphore_t *;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(milliseconds) (static_cast<TickType_t>(milliseconds))
