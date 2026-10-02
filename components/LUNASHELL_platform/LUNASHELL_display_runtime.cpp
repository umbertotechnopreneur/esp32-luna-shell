/*
 * Standalone display and touch access beneath the product renderer.
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"

#include <atomic>

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY || CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
#include "bsp/display.h"
#include "bsp/touch.h"
#endif

namespace {

constexpr char kTag[] = "LUNASHELL_display_runtime";
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
std::atomic_bool s_writes_suspended{false};
std::atomic_bool s_runtime_ready{false};
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
#endif

// operation: Display or touch operation whose result is being reported.
// result: Original operation error, or success after the hardware operation completes.
esp_err_t log_operation_result(const char *operation, esp_err_t result)
{
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "%s failed: %s", operation, esp_err_to_name(result));
    }
    return result;
}

}  // namespace

// Initialize the selected standalone hardware and publish each driver's real state.
extern "C" esp_err_t LUNASHELL_display_runtime_init(void)
{
#if (CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY || CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH) && \
    !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    esp_err_t result = bsp_display_base_start();
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    const bool display_ready = bsp_display_is_ready();
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    const bool touch_ready = bsp_touch_is_ready();
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    if (result == ESP_OK && !display_ready) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    if (result == ESP_OK && !touch_ready) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
#endif
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    snapshots.reportDisplay(display_ready, result);
    s_runtime_ready.store(result == ESP_OK && display_ready, std::memory_order_release);
#else
    snapshots.reportDisplay(false, ESP_ERR_NOT_SUPPORTED);
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    snapshots.reportTouch(touch_ready, result);
#else
    snapshots.reportTouch(false, ESP_ERR_NOT_SUPPORTED);
#endif
    return log_operation_result("Standalone display/touch initialization", result);
#else
    return log_operation_result(
        "Standalone display/touch initialization", ESP_ERR_NOT_SUPPORTED
    );
#endif
}

// Return only readiness confirmed by hardware operations and the active write gate.
extern "C" bool LUNASHELL_display_runtime_writes_allowed(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    return s_runtime_ready.load(std::memory_order_acquire) &&
           !s_writes_suspended.load(std::memory_order_acquire);
#else
    return false;
#endif
}

// Read the BSP revision atomically without retaining a framebuffer or full snapshot.
extern "C" uint32_t LUNASHELL_display_runtime_content_revision(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    return bsp_display_content_revision();
#else
    return 0;
#endif
}

// suspended: Whether standalone rectangle writes must remain blocked.
extern "C" esp_err_t LUNASHELL_display_runtime_set_suspended(bool suspended)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const esp_err_t result = bsp_display_lock(kDisplayLockTimeoutMs);
    if (result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, result);
        return log_operation_result("Standalone display quiescence lock", result);
    }
    s_writes_suspended.store(suspended, std::memory_order_release);
    bsp_display_unlock();
    return ESP_OK;
#else
    (void)suspended;
    return log_operation_result("Standalone display quiescence", ESP_ERR_NOT_SUPPORTED);
#endif
}

// x_start: First included horizontal pixel coordinate.
// y_start: First included vertical pixel coordinate.
// x_end: First excluded horizontal pixel coordinate.
// y_end: First excluded vertical pixel coordinate.
// pixels: Packed RGB565 pixels retained until the synchronous write returns.
extern "C" esp_err_t LUNASHELL_display_write_rgb565(
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *pixels
)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, lock_error);
        return log_operation_result("RGB565 rectangle write lock", lock_error);
    }
    if (s_writes_suspended.load(std::memory_order_acquire)) {
        bsp_display_unlock();
        return log_operation_result("RGB565 rectangle write during standby", ESP_ERR_INVALID_STATE);
    }
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_DISPLAY
    );
    if (watch_error != ESP_OK) {
        bsp_display_unlock();
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, watch_error);
        return log_operation_result("RGB565 rectangle watchdog start", watch_error);
    }
    const esp_err_t write_result = bsp_display_write_rgb565(
        x_start, y_start, x_end, y_end, pixels
    );
    const esp_err_t result = LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_DISPLAY, write_result
    );
    s_runtime_ready.store(bsp_display_is_ready(), std::memory_order_release);
    bsp_display_unlock();
    LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
        result == ESP_OK, result
    );
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "RGB565 rectangle [%d,%d)-[%d,%d), pixels=%s: %s",
                 x_start, y_start, x_end, y_end,
                 pixels == nullptr ? "null" : "present", esp_err_to_name(result));
    }
    return log_operation_result("RGB565 rectangle write", result);
#else
    (void)x_start;
    (void)y_start;
    (void)x_end;
    (void)y_end;
    (void)pixels;
    return log_operation_result("RGB565 rectangle write", ESP_ERR_NOT_SUPPORTED);
#endif
}

// point: Receives the real touch state or an explicit invalid state and error.
extern "C" esp_err_t LUNASHELL_touch_read_point(LUNASHELL_touch_point_t *point)
{
    if (point == nullptr) {
        return log_operation_result("Touch read with null output", ESP_ERR_INVALID_ARG);
    }
    *point = {};
    point->error = ESP_ERR_NOT_SUPPORTED;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    bsp_touch_point_t sample{};
    esp_err_t result = bsp_touch_read_point(&sample);
    if (result == ESP_OK && (!sample.valid || sample.error != ESP_OK ||
                            sample.sample_timestamp_us <= 0)) {
        result = sample.error != ESP_OK ? sample.error : ESP_ERR_INVALID_RESPONSE;
    }
    point->error = result;
    point->sample_timestamp_us = sample.sample_timestamp_us;
    if (result == ESP_OK) {
        point->valid = true;
        point->pressed = sample.pressed;
        point->x = sample.x;
        point->y = sample.y;
    }
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    snapshots.reportTouch(result == ESP_OK, result);
    if (point->valid && point->pressed) {
        snapshots.reportTouchActivity();
        LUNASHELL_power_manager_note_activity();
    }
    return log_operation_result("Touch read", result);
#else
    return log_operation_result("Touch read", ESP_ERR_NOT_SUPPORTED);
#endif
}
