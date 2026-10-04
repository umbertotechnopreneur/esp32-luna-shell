/*
 * Standalone display and touch access beneath the product renderer.
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"

#include <atomic>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
std::atomic_bool s_startup_quiescent{false};
std::atomic_bool s_runtime_ready{false};
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
std::atomic<TaskHandle_t> s_frame_owner{nullptr};
// These metrics belong to the owner while the BSP frame lock is held.
LUNASHELL::DisplayFrameSnapshot s_frame_metrics{};

// Return whether this call belongs to the currently locked frame.
bool owns_display_frame()
{
    return s_frame_owner.load(std::memory_order_acquire) == xTaskGetCurrentTaskHandle();
}
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
           !s_writes_suspended.load(std::memory_order_acquire) &&
           !s_startup_quiescent.load(std::memory_order_acquire);
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

// quiescent: Block only startup DMA contention; leave the power write gate unchanged.
extern "C" esp_err_t LUNASHELL_display_runtime_set_startup_quiescent(bool quiescent)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    // A retired frame has released its transient DMA lease before this lock opens.
    const esp_err_t result = bsp_display_lock(kDisplayLockTimeoutMs);
    if (result != ESP_OK) {
        return log_operation_result("Startup display quiescence lock", result);
    }
    s_startup_quiescent.store(quiescent, std::memory_order_release);
    bsp_display_unlock();
    return ESP_OK;
#else
    (void)quiescent;
    return log_operation_result("Startup display quiescence", ESP_ERR_NOT_SUPPORTED);
#endif
}

// budget_ms: Total budget for one standalone frame, including geometry and raster work.
extern "C" esp_err_t LUNASHELL_display_frame_begin(uint32_t budget_ms)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const int64_t lock_started_us = esp_timer_get_time();
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, lock_error);
        return log_operation_result("Frame lock", lock_error);
    }
    const uint32_t lock_wait_us = static_cast<uint32_t>(esp_timer_get_time() - lock_started_us);
    if (!LUNASHELL_display_runtime_writes_allowed() ||
        s_frame_owner.load(std::memory_order_acquire) != nullptr) {
        bsp_display_unlock();
        return log_operation_result("Frame begin state", ESP_ERR_INVALID_STATE);
    }
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(LUNASHELL_LIVENESS_DISPLAY);
    if (watch_error != ESP_OK) {
        bsp_display_unlock();
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, watch_error);
        return log_operation_result("Frame watchdog start", watch_error);
    }
    const esp_err_t begin_error = bsp_display_frame_begin(budget_ms);
    // Keep both levels on success: watch cleanup and publication precede final unlock.
    if (begin_error != ESP_OK) {
        const esp_err_t error = LUNASHELL_liveness_watch_end(LUNASHELL_LIVENESS_DISPLAY, begin_error);
        bsp_display_unlock();
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, error);
        return log_operation_result("Frame begin", error);
    }
    s_frame_metrics.last_lock_wait_us = lock_wait_us;
    s_frame_owner.store(xTaskGetCurrentTaskHandle(), std::memory_order_release);
    return ESP_OK;
#else
    (void)budget_ms;
    return log_operation_result("Frame begin", ESP_ERR_NOT_SUPPORTED);
#endif
}

// Deadline failures remain failures even when the caller has not submitted pixels yet.
extern "C" esp_err_t LUNASHELL_display_frame_check_deadline(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (!owns_display_frame()) {
        return log_operation_result("Frame deadline owner", ESP_ERR_INVALID_STATE);
    }
    const esp_err_t error = bsp_display_frame_check_deadline();
    if (error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, error);
    }
    return error;
#else
    return log_operation_result("Frame deadline", ESP_ERR_NOT_SUPPORTED);
#endif
}

// pixels: Receives idle internal raster storage borrowed only by the frame owner.
// capacity_bytes: Actual capacity, including an explicitly degraded DMA row count.
extern "C" esp_err_t LUNASHELL_display_frame_buffer(void **pixels, size_t *capacity_bytes)
{
    if (pixels == nullptr || capacity_bytes == nullptr) {
        return log_operation_result("Frame buffer outputs", ESP_ERR_INVALID_ARG);
    }
    *pixels = nullptr;
    *capacity_bytes = 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (!owns_display_frame()) {
        return log_operation_result("Frame buffer owner", ESP_ERR_INVALID_STATE);
    }
    const esp_err_t error = bsp_display_frame_buffer(pixels, capacity_bytes);
    if (error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, error);
    }
    return log_operation_result("Frame buffer", error);
#else
    return log_operation_result("Frame buffer", ESP_ERR_NOT_SUPPORTED);
#endif
}

// frame_error: Original failure from the renderer, or success after its final strip.
extern "C" esp_err_t LUNASHELL_display_frame_end(esp_err_t frame_error)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (!owns_display_frame()) {
        return log_operation_result("Frame end owner", ESP_ERR_INVALID_STATE);
    }
    // The outer level is already ours, even if a DMA fault now rejects new locks.
    bsp_display_frame_result_t metrics{};
    const esp_err_t deadline_error = bsp_display_frame_check_deadline();
    const esp_err_t render_error = frame_error != ESP_OK ? frame_error : deadline_error;
    const esp_err_t watched_error = LUNASHELL_liveness_watch_end(LUNASHELL_LIVENESS_DISPLAY, render_error);
    const esp_err_t end_error = bsp_display_frame_end(watched_error, &metrics);
    // State and metric publication must precede releasing our platform owner below.
    s_frame_metrics.measured = true;
    s_frame_metrics.last_duration_us = metrics.duration_us;
    s_frame_metrics.last_rectangle_count = metrics.rectangles;
    s_frame_metrics.last_conversion_us = metrics.conversion_us;
    s_frame_metrics.last_submission_us = metrics.submission_us;
    s_frame_metrics.last_completion_wait_us = metrics.completion_wait_us;
    s_frame_metrics.retained_dma_bytes = metrics.retained_dma_bytes;
    s_frame_metrics.reset_required = metrics.retained_dma_bytes != 0;
    s_frame_metrics.last_error = end_error;
    s_frame_metrics.completed_at_us = esp_timer_get_time();
    if (end_error == ESP_OK) {
        ++s_frame_metrics.completed_frames;
    } else {
        ++s_frame_metrics.failed_frames;
    }
    const auto completed_metrics = s_frame_metrics;
    s_runtime_ready.store(bsp_display_is_ready(), std::memory_order_release);
    s_frame_owner.store(nullptr, std::memory_order_release);
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayFrame(completed_metrics);
    bsp_display_unlock();
    return log_operation_result("Frame completion", end_error);
#else
    (void)frame_error;
    return log_operation_result("Frame end", ESP_ERR_NOT_SUPPORTED);
#endif
}

// x_start: First included horizontal pixel coordinate.
// y_start: First included vertical pixel coordinate.
// x_end: First excluded horizontal pixel coordinate.
// y_end: First excluded vertical pixel coordinate.
// pixels: Packed RGB565 pixels retained until the synchronous write returns.
// byte_order: Explicit source representation mapped to the matching BSP contract.
extern "C" esp_err_t LUNASHELL_display_write_rgb565(
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *pixels,
    LUNASHELL_display_rgb565_byte_order_t byte_order
)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    bsp_display_rgb565_byte_order_t bsp_byte_order;
    switch (byte_order) {
    case LUNASHELL_DISPLAY_RGB565_BYTE_ORDER_NATIVE:
        bsp_byte_order = BSP_DISPLAY_RGB565_BYTE_ORDER_NATIVE;
        break;
    case LUNASHELL_DISPLAY_RGB565_BYTE_ORDER_PANEL:
        bsp_byte_order = BSP_DISPLAY_RGB565_BYTE_ORDER_PANEL;
        break;
    default:
        ESP_LOGE(kTag, "RGB565 rectangle byte order rejected: %d (%s)",
                 static_cast<int>(byte_order), esp_err_to_name(ESP_ERR_INVALID_ARG));
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
            false, ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, lock_error);
        return log_operation_result("RGB565 rectangle write lock", lock_error);
    }
    if (s_writes_suspended.load(std::memory_order_acquire)) {
        bsp_display_unlock();
        return log_operation_result("RGB565 rectangle write during standby", ESP_ERR_INVALID_STATE);
    }
    const bool in_frame = owns_display_frame();
    const esp_err_t watch_error = in_frame ? ESP_OK : LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_DISPLAY
    );
    if (watch_error != ESP_OK) {
        bsp_display_unlock();
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, watch_error);
        return log_operation_result("RGB565 rectangle watchdog start", watch_error);
    }
    const esp_err_t write_result = bsp_display_write_rgb565(
        x_start, y_start, x_end, y_end, pixels, bsp_byte_order
    );
    const esp_err_t result = in_frame ? write_result : LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_DISPLAY, write_result
    );
    s_runtime_ready.store(bsp_display_is_ready(), std::memory_order_release);
    bsp_display_unlock();
    // Every failed strip is visible immediately; success belongs to the whole frame.
    if (!in_frame || result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(result == ESP_OK, result);
    }
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "RGB565 rectangle [%d,%d)-[%d,%d), pixels=%s order=%d: %s",
                 x_start, y_start, x_end, y_end,
                 pixels == nullptr ? "null" : "present", static_cast<int>(byte_order),
                 esp_err_to_name(result));
    }
    return log_operation_result("RGB565 rectangle write", result);
#else
    (void)x_start;
    (void)y_start;
    (void)x_end;
    (void)y_end;
    (void)pixels;
    (void)byte_order;
    return log_operation_result("RGB565 rectangle write", ESP_ERR_NOT_SUPPORTED);
#endif
}

// timeout_ms: Maximum idle wait; this signal carries no synthetic touch measurement.
extern "C" esp_err_t LUNASHELL_touch_wait_for_report(uint32_t timeout_ms)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const esp_err_t result = bsp_touch_wait_for_report(timeout_ms);
    if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) {
        return log_operation_result("Touch IRQ wait", result);
    }
    return result;
#else
    (void)timeout_ms;
    return log_operation_result("Touch IRQ wait", ESP_ERR_NOT_SUPPORTED);
#endif
}

// point: Receives a real report, or invalid state; NOT_FOUND with timestamp zero means no report.
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
    if (result == ESP_ERR_NOT_FOUND && !sample.valid && sample.sample_timestamp_us == 0) {
        // The initialized transport stays healthy while its IRQ has no work.
        // Preserve the unavailable sample instead of publishing a fake release.
        point->error = result;
        return result;
    }
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
