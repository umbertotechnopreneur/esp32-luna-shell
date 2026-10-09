#pragma once

#include <cstdint>

// Declaration-only SDK boundary for the portable event-layout fixture. No task
// execution is emulated: the linked owner boundary aborts if a task is invoked.
using StackType_t = uint32_t;
using TaskHandle_t = void *;
struct StaticTask_t { uint32_t unused; };
constexpr bool pdTRUE = true;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
