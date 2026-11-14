#include "LUNASHELL_platform.h"
#include "esp_timer.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>

#if defined(_WIN32)
#define CODY_HOST_POWER_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_POWER_API extern "C" __attribute__((visibility("default")))
#endif

namespace {
// This is a preference only; the host has no simulated IMU producer yet.
std::atomic<bool> s_shake_wake_enabled{false};
struct HostPower {
    std::mutex mutex;
    std::condition_variable changed;
    LUNASHELL_display_lifecycle_callback_t lifecycle = nullptr;
    void (*boot_home)() = nullptr;
    void (*boot_options)() = nullptr;
    esp_err_t (*farewell)() = nullptr;
    esp_err_t (*shutdown)() = nullptr;
    esp_err_t (*shutdown_error)(esp_err_t) = nullptr;
    uint32_t override_seconds = 0;
    uint64_t revision = 0;
    std::chrono::steady_clock::time_point last_activity = std::chrono::steady_clock::now();
    bool worker_started = false;
    bool asleep = false;
    bool transitioning = false;
    esp_err_t lifecycle_error = ESP_OK;
};
// UI worker lifetime equals process lifetime; no detached thread may outlive its state.
HostPower *const s_power = new (std::nothrow) HostPower{};

// state: Process-lifetime host power owner.
// The host implements the explicit per-scene timeout only; absent base power
// policy is not inferred from the PC or claimed as a physical observation.
void power_worker(HostPower *state)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    for (;;) {
        if (!state->override_seconds || state->asleep || !state->lifecycle) {
            state->changed.wait(lock);
            continue;
        }
        const auto revision = state->revision;
        const auto deadline = state->last_activity + std::chrono::seconds(state->override_seconds);
        if (state->changed.wait_until(lock, deadline, [&] { return state->revision != revision; })) continue;
        const auto callback = state->lifecycle;
        state->transitioning = true;
        lock.unlock();
        const auto error = callback(true);
        lock.lock();
        state->transitioning = false;
        state->lifecycle_error = error;
        state->asleep = error == ESP_OK;
        // A rejected transition must not busy-loop or imply successful sleep.
        if (error != ESP_OK) state->override_seconds = 0;
        ++state->revision;
    }
}
}

// callback: Native UI lifecycle consumer, registered exactly once.
// Exception: Rejects a missing or duplicate consumer.
extern "C" esp_err_t LUNASHELL_power_manager_register_display_lifecycle(LUNASHELL_display_lifecycle_callback_t callback)
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (!callback) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (s_power->lifecycle) return ESP_ERR_INVALID_STATE;
    s_power->lifecycle = callback;
    return ESP_OK;
}

// request: Native UI callback for the simulated BOOT short press.
// Exception: Rejects null or duplicate registration.
extern "C" esp_err_t LUNASHELL_power_manager_register_boot_home(void (*request)(void))
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (!request) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (s_power->boot_home) return ESP_ERR_INVALID_STATE;
    s_power->boot_home = request;
    return ESP_OK;
}

// request: Native UI callback for the simulated BOOT hold.
// Exception: Rejects null or duplicate registration.
extern "C" esp_err_t LUNASHELL_power_manager_register_boot_options(void (*request)(void))
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (!request) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (s_power->boot_options) return ESP_ERR_INVALID_STATE;
    s_power->boot_options = request;
    return ESP_OK;
}

// prepare: Native callback retained for an explicit future simulated standby request.
// Exception: Rejects null or duplicate registration; no physical power action occurs.
extern "C" esp_err_t LUNASHELL_power_manager_register_offline_farewell(esp_err_t (*prepare)(void))
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (!prepare) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (s_power->farewell) return ESP_ERR_INVALID_STATE;
    s_power->farewell = prepare;
    return ESP_OK;
}

// prepare: Native UI shutdown-preparation callback.
// report_error: Native UI callback for an actual shutdown failure.
// Exception: Rejects null or duplicate registration; no host shutdown occurs.
extern "C" esp_err_t LUNASHELL_power_manager_register_shutdown_callbacks(
    esp_err_t (*prepare)(void), esp_err_t (*report_error)(esp_err_t))
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (!prepare || !report_error) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (s_power->shutdown || s_power->shutdown_error) return ESP_ERR_INVALID_STATE;
    s_power->shutdown = prepare;
    s_power->shutdown_error = report_error;
    return ESP_OK;
}

// seconds: Explicit temporary scene timeout; zero clears the override.
// Exception: Reports thread allocation or absent lifecycle without claiming a timeout exists.
// Static Studio samples retain the declared policy but never schedule wall-time standby.
extern "C" esp_err_t LUNASHELL_power_manager_set_scene_standby_override(uint32_t seconds)
{
    if (!s_power) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    if (!s_power->lifecycle) return ESP_ERR_INVALID_STATE;
    if (!s_power->worker_started && cody_host_manual_time_us.load(std::memory_order_acquire) == 0) {
        try { std::thread(power_worker, s_power).detach(); }
        catch (...) { return ESP_ERR_NO_MEM; }
        s_power->worker_started = true;
    }
    s_power->override_seconds = seconds;
    s_power->last_activity = std::chrono::steady_clock::now();
    ++s_power->revision;
    s_power->changed.notify_one();
    return ESP_OK;
}

// No parameters: records a real host input event against the simulated scene timer.
extern "C" void LUNASHELL_power_manager_note_activity(void)
{
    if (!s_power) return;
    std::lock_guard<std::mutex> lock(s_power->mutex);
    s_power->last_activity = std::chrono::steady_clock::now();
    ++s_power->revision;
    s_power->changed.notify_one();
}

// held: Zero for BOOT tap, one for the explicitly simulated BOOT hold.
// Exception: Rejects unavailable callbacks or failed wake without invoking UI input.
CODY_HOST_POWER_API esp_err_t cody_host_boot_event(uint8_t held)
{
    if (!s_power) return ESP_ERR_NO_MEM;
    if (held > 1) return ESP_ERR_INVALID_ARG;
    std::unique_lock<std::mutex> lock(s_power->mutex);
    if (s_power->transitioning) return ESP_ERR_NOT_FINISHED;
    const auto callback = held ? s_power->boot_options : s_power->boot_home;
    if (!callback) return ESP_ERR_INVALID_STATE;
    if (s_power->asleep) {
        const auto lifecycle = s_power->lifecycle;
        if (!lifecycle) return ESP_ERR_INVALID_STATE;
        s_power->transitioning = true;
        lock.unlock();
        const auto error = lifecycle(false);
        lock.lock();
        s_power->transitioning = false;
        s_power->lifecycle_error = error;
        if (error != ESP_OK) return error;
        s_power->asleep = false;
    }
    s_power->last_activity = std::chrono::steady_clock::now();
    ++s_power->revision;
    s_power->changed.notify_one();
    lock.unlock();
    callback();
    return ESP_OK;
}

// No parameters: a real simulated contact wakes the retained native UI if needed.
// Exception: Preserves pending-transition or native wake errors before touch admission.
extern "C" esp_err_t cody_host_power_touch()
{
    if (!s_power) return ESP_ERR_NO_MEM;
    std::unique_lock<std::mutex> lock(s_power->mutex);
    if (s_power->transitioning) return ESP_ERR_NOT_FINISHED;
    if (s_power->asleep) {
        const auto lifecycle = s_power->lifecycle;
        if (!lifecycle) return ESP_ERR_INVALID_STATE;
        s_power->transitioning = true;
        lock.unlock();
        const auto error = lifecycle(false);
        lock.lock();
        s_power->transitioning = false;
        s_power->lifecycle_error = error;
        if (error != ESP_OK) return error;
        s_power->asleep = false;
    }
    s_power->last_activity = std::chrono::steady_clock::now();
    ++s_power->revision;
    s_power->changed.notify_one();
    return ESP_OK;
}

// No parameters: restart requires a host lifecycle owner that is not yet available.
extern "C" esp_err_t LUNASHELL_power_manager_request_restart(void) { return ESP_ERR_NOT_SUPPORTED; }

// request_id: An unsupported host reset never assigns an operation identity.
// Exception: Rejects null, otherwise returns NOT_SUPPORTED without altering settings.
extern "C" esp_err_t LUNASHELL_power_manager_request_factory_reset(uint32_t *request_id)
{
    if (!request_id) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Stores the simulated wake policy independently of IMU availability,
// as the firmware power owner does; no physical motion input is implied.
extern "C" esp_err_t LUNASHELL_power_manager_set_shake_wake_enabled(bool enabled)
{
    s_shake_wake_enabled.store(enabled, std::memory_order_release);
    return ESP_OK;
}

extern "C" bool LUNASHELL_power_manager_shake_wake_enabled(void)
{
    return s_shake_wake_enabled.load(std::memory_order_acquire);
}
