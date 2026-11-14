#pragma once

#include "FreeRTOS.h"
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include "esp_timer.h"

// One host tick is one elapsed millisecond, wrapping like TickType_t on device.
inline TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(esp_timer_get_time() / 1000); }

// One simulator process owns one static firmware action task. The condition
// variable models counting task notifications, not application events.
struct CodyHostTask {
    std::mutex mutex;
    std::condition_variable condition;
    uint32_t pending = 0;
    bool notified = false;
};
// The host task's notification owner has process lifetime because the existing
// device worker has no shutdown path. Never unload the host library while it runs.
struct StaticTask_t { CodyHostTask *task = nullptr; };
using TaskHandle_t = CodyHostTask *;
using TaskFunction_t = void (*)(void *);
inline thread_local TaskHandle_t cody_host_current_task = nullptr;
struct CodyHostTaskExit {};

// entry: Original FreeRTOS task body from the device source.
// name: Borrowed diagnostic label; no platform task is renamed.
// stack_depth: Size declared by the caller; the host owns its thread stack.
// context: Borrowed task argument, with task-long lifetime.
// priority: Firmware scheduler priority, not mapped to an OS scheduling policy.
// stack: Firmware stack storage; unused because the OS allocates host stacks.
// control: Static notification owner; must outlive the simulator process.
// Exception: Thread allocation failures return nullptr and never escape the ABI.
inline TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name,
    uint32_t stack_depth, void *context, UBaseType_t priority, StackType_t *stack,
    StaticTask_t *control)
{
    (void)name;
    (void)priority;
    if (!entry || !stack || stack_depth == 0 || !control) return nullptr;
    if (control->task) return nullptr;
    auto *task = new (std::nothrow) CodyHostTask{};
    if (!task) return nullptr;
    try {
        std::thread([entry, context, task] {
            cody_host_current_task = task;
            try { entry(context); } catch (const CodyHostTaskExit &) {}
            cody_host_current_task = nullptr;
        }).detach();
    } catch (...) {
        delete task;
        return nullptr;
    }
    control->task = task;
    return task;
}

// entry: Original firmware task body executed by one host thread.
// name: Borrowed task label; no OS scheduling identity is claimed.
// stack_depth: Requested firmware stack size; the OS owns the host stack.
// context: Borrowed task argument with task-long lifetime.
// priority: Firmware priority, not mapped to host scheduling.
// created: Optional output receiving the host task handle only on success.
// Exception: Returns pdFAIL on invalid arguments or allocation/thread failure.
inline BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack_depth,
    void *context, UBaseType_t priority, TaskHandle_t *created)
{
    (void)name;
    (void)priority;
    if (created) *created = nullptr;
    if (!entry || stack_depth == 0) return pdFAIL;
    auto *task = new (std::nothrow) CodyHostTask{};
    if (!task) return pdFAIL;
    try {
        std::thread([entry, context, task] {
            cody_host_current_task = task;
            try { entry(context); } catch (const CodyHostTaskExit &) {}
            cody_host_current_task = nullptr;
        }).detach();
    } catch (...) {
        delete task;
        return pdFAIL;
    }
    if (created) *created = task;
    return pdPASS;
}

// task: Static host task created by xTaskCreateStatic.
// Exception: Returns pdFAIL for an absent, unstarted or saturated task.
inline BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
    if (!task) return pdFAIL;
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        if (task->pending == std::numeric_limits<uint32_t>::max()) return pdFAIL;
        ++task->pending;
        task->notified = true;
    }
    task->condition.notify_one();
    return pdPASS;
}

// clear_on_exit: pdTRUE drains the notification count; pdFALSE consumes one.
// ticks_to_wait: Host timeout in milliseconds, or portMAX_DELAY for no deadline.
// Exception: Calls outside a host task return zero.
inline uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait)
{
    auto *task = cody_host_current_task;
    if (!task) return 0;
    std::unique_lock<std::mutex> lock(task->mutex);
    if (ticks_to_wait == portMAX_DELAY)
        task->condition.wait(lock, [task] { return task->pending != 0; });
    else if (ticks_to_wait != 0)
        task->condition.wait_for(lock, std::chrono::milliseconds(ticks_to_wait),
            [task] { return task->pending != 0; });
    if (task->pending == 0) return 0;
    const auto result = task->pending;
    if (clear_on_exit == pdTRUE) task->pending = 0;
    else --task->pending;
    task->notified = false;
    return result;
}

enum eNotifyAction { eNoAction, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite };

// task: Target host task with notification storage.
// value: Bit mask for eSetBits.
// action: Only eSetBits is currently supported by the device audio owner.
// Exception: Unsupported actions or absent targets return pdFAIL.
inline BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action)
{
    if (!task || action != eSetBits) return pdFAIL;
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        task->pending |= value;
        task->notified = true;
    }
    task->condition.notify_one();
    return pdPASS;
}

// clear_on_entry: Bits cleared before blocking only when no notification is pending.
// clear_on_exit: Bits cleared after recording the received value.
// value: Optional destination for the actual notification value.
// ticks: Millisecond deadline, zero to poll, or portMAX_DELAY.
// Exception: Returns pdFALSE on a missing task or expired deadline.
inline BaseType_t xTaskNotifyWait(uint32_t clear_on_entry, uint32_t clear_on_exit,
    uint32_t *value, TickType_t ticks)
{
    auto *task = cody_host_current_task;
    if (!task) return pdFALSE;
    std::unique_lock<std::mutex> lock(task->mutex);
    if (!task->notified) task->pending &= ~clear_on_entry;
    if (ticks == portMAX_DELAY)
        task->condition.wait(lock, [task] { return task->notified; });
    else if (ticks != 0)
        task->condition.wait_for(lock, std::chrono::milliseconds(ticks), [task] { return task->notified; });
    if (!task->notified) return pdFALSE;
    if (value) *value = task->pending;
    task->pending &= ~clear_on_exit;
    task->notified = false;
    return pdTRUE;
}

// No parameters: identifies the current firmware worker or host-calling thread.
// Exception: A host proxy allocation failure returns nullptr.
inline TaskHandle_t xTaskGetCurrentTaskHandle()
{
    if (cody_host_current_task) return cody_host_current_task;
    // The firmware audio owner compares task identity when a host caller claims
    // a stream. Use a separate process-lifetime identity per calling thread.
    thread_local TaskHandle_t external = new (std::nothrow) CodyHostTask{};
    return external;
}

// task: Ignored host-task selector; physical FreeRTOS stack telemetry is unavailable.
// The host logger suppresses this sentinel rather than publishing it as a measurement.
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 0;
}

// ticks: Host scheduling delay in milliseconds, never physical-device evidence.
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
