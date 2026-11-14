#pragma once

#include "FreeRTOS.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>

struct CodyHostBinarySemaphore {
    std::mutex mutex;
    std::condition_variable condition;
    bool available = false;
    bool mutex_kind = false;
    std::thread::id owner{};
};
// The settings executor's semaphore and worker both live for the host process.
struct StaticSemaphore_t { CodyHostBinarySemaphore *semaphore = nullptr; };
using SemaphoreHandle_t = CodyHostBinarySemaphore *;

// control: One static firmware-owned semaphore control with host process lifetime.
// Exception: Returns nullptr on absent control, duplicate creation or allocation failure.
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *control)
{
    if (!control || control->semaphore) return nullptr;
    control->semaphore = new (std::nothrow) CodyHostBinarySemaphore{};
    return control->semaphore;
}

// control: Static firmware-owned mutex control, with host process lifetime.
// Exception: Returns nullptr on absent control, duplicate creation or allocation failure.
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *control)
{
    auto *semaphore = xSemaphoreCreateBinaryStatic(control);
    if (semaphore) {
        semaphore->mutex_kind = true;
        semaphore->available = true;
    }
    return semaphore;
}

// semaphore: Host notification owner created by xSemaphoreCreateBinaryStatic.
// Exception: Returns pdFALSE when the handle is absent or already signaled.
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    if (!semaphore) return pdFALSE;
    {
        std::lock_guard<std::mutex> lock(semaphore->mutex);
        if (semaphore->available) return pdFALSE;
        if (semaphore->mutex_kind && semaphore->owner != std::this_thread::get_id()) return pdFALSE;
        semaphore->owner = {};
        semaphore->available = true;
    }
    semaphore->condition.notify_one();
    return pdTRUE;
}

// semaphore: Host completion semaphore.
// ticks: Milliseconds to wait, zero for poll or portMAX_DELAY for no deadline.
// Exception: Returns pdFALSE for missing semaphore or expired timeout.
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    if (!semaphore) return pdFALSE;
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    if (ticks == portMAX_DELAY)
        semaphore->condition.wait(lock, [semaphore] { return semaphore->available; });
    else if (ticks != 0)
        semaphore->condition.wait_for(lock, std::chrono::milliseconds(ticks),
            [semaphore] { return semaphore->available; });
    if (!semaphore->available) return pdFALSE;
    semaphore->available = false;
    if (semaphore->mutex_kind) semaphore->owner = std::this_thread::get_id();
    return pdTRUE;
}
