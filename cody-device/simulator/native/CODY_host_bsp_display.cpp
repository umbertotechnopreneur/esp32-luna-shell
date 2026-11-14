// Host-only virtual display BSP. All settings, persistence and rollback remain
// owned by the unchanged firmware LUNASHELL_display_settings.cpp.
#include "CODY_host_bsp_display.hpp"
#include "bsp/display.h"
#include "esp_system.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>

namespace {
std::timed_mutex s_operation_mutex;
std::mutex s_state_mutex;
bool s_initialized = false;
int s_brightness = 0;
bool s_panel_enabled = false;
std::atomic<int> s_reset_reason{ESP_RST_UNKNOWN};
}

// initial_brightness: Explicit synthetic panel level from 0 to 100.
// panel_enabled: Explicit virtual panel gate.
// reset_reason: Supported ESP-IDF reset identity for this host scenario.
// Exception: Rejects invalid parameters or repeated initialization.
extern "C" esp_err_t cody_host_bsp_display_initialize(int initial_brightness,
    bool panel_enabled, int reset_reason)
{
    if (initial_brightness < 0 || initial_brightness > 100 || reset_reason < ESP_RST_UNKNOWN ||
        reset_reason > ESP_RST_SDIO) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_state_mutex);
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    s_brightness = initial_brightness;
    s_panel_enabled = panel_enabled;
    s_reset_reason.store(reset_reason, std::memory_order_release);
    s_initialized = true;
    return ESP_OK;
}

// brightness: Receives the current virtual hardware brightness level.
// panel_enabled: Receives the current virtual panel gate.
// Exception: Does not modify outputs when unavailable or invalid.
extern "C" esp_err_t cody_host_bsp_display_read(int *brightness, bool *panel_enabled)
{
    if (!brightness || !panel_enabled) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_state_mutex);
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    *brightness = s_brightness;
    *panel_enabled = s_panel_enabled;
    return ESP_OK;
}

// timeout_ms: Maximum host lock wait; UINT32_MAX selects an unbounded wait.
// Exception: Preserves timeout/unavailable errors and never claims a lock on failure.
extern "C" esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    {
        std::lock_guard<std::mutex> lock(s_state_mutex);
        if (!s_initialized) return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
        s_operation_mutex.lock();
        return ESP_OK;
    }
    return s_operation_mutex.try_lock_for(std::chrono::milliseconds(timeout_ms)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// No parameters: release the lock acquired by this host display worker.
extern "C" void bsp_display_unlock() { s_operation_mutex.unlock(); }

// brightness_percent: Exact virtual BSP brightness from 0 to 100 percent.
// Exception: Returns invalid argument or unavailable without changing state.
extern "C" esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 0 || brightness_percent > 100) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_state_mutex);
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_brightness = brightness_percent;
    return ESP_OK;
}

// No parameters: read the virtual BSP value; -1 is the BSP unavailable sentinel.
extern "C" int bsp_display_brightness_get()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return s_initialized ? s_brightness : -1;
}

// enabled: Exact virtual panel gate; this is not a physical power observation.
// Exception: Returns unavailable without changing state.
extern "C" esp_err_t bsp_display_panel_set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_panel_enabled = enabled;
    return ESP_OK;
}

// No parameters: return the host scenario's explicit reset identity.
extern "C" esp_reset_reason_t esp_reset_reason()
{
    return static_cast<esp_reset_reason_t>(s_reset_reason.load(std::memory_order_acquire));
}
