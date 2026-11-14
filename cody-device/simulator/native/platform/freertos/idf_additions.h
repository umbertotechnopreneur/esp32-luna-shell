#pragma once

#include "task.h"
#include <cstdlib>

// entry: Unmodified firmware task body executed by a host thread.
// name: Firmware diagnostic label.
// stack_depth: Firmware requested stack size; the host OS owns its thread stack.
// context: Borrowed task argument retained for the task's lifetime.
// priority: Firmware priority; host scheduling priority is not inferred.
// created: Optional returned task handle, assigned only after successful creation.
// core: Original requested ESP32 core; no host CPU affinity is asserted.
// capabilities: Original memory capabilities; no physical PSRAM is claimed.
// Exception: Returns pdFAIL if task creation fails.
inline BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t entry, const char *name,
    uint32_t stack_depth, void *context, UBaseType_t priority, TaskHandle_t *created,
    BaseType_t core, uint32_t capabilities)
{
    (void)core;
    (void)capabilities;
    return xTaskCreate(entry, name, stack_depth, context, priority, created);
}

// task: Only nullptr (the caller's own task) is supported in the host.
// Exception: Non-self deletion fails closed; self deletion exits the host task.
[[noreturn]] inline void vTaskDeleteWithCaps(TaskHandle_t task)
{
    if (task != nullptr || !cody_host_current_task) std::abort();
    throw CodyHostTaskExit{};
}
