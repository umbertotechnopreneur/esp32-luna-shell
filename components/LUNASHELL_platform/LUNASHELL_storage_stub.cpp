/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr char kTag[] = "LUNASHELL_storage";

void publish_unavailable(bool mirror_requested)
{
    LUNASHELL::StorageMountReport report;
    report.error = ESP_ERR_NOT_SUPPORTED;
    report.logger_mirror_requested = mirror_requested;
    LUNASHELL::DeviceSnapshotService::instance().reportStorage(report);
    LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
        mirror_requested,
        false,
        ESP_ERR_NOT_SUPPORTED,
        0,
        esp_timer_get_time(),
        0
    );
}

esp_err_t reject_operation(const char *operation)
{
    ESP_LOGE(
        kTag,
        "Removable-storage operation '%s' rejected: disabled by build profile",
        operation
    );
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace

extern "C" esp_err_t LUNASHELL_storage_init(void)
{
    publish_unavailable(false);
    ESP_LOGI(kTag, "Removable storage: disabled by build profile");
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_storage_request_format(void)
{
    return reject_operation("format");
}

extern "C" esp_err_t LUNASHELL_storage_request_space_refresh(void)
{
    return reject_operation("space refresh");
}

extern "C" esp_err_t LUNASHELL_storage_set_logger_mirror(bool enabled)
{
    publish_unavailable(enabled);
    return reject_operation(enabled ? "enable log mirror" : "disable log mirror");
}

extern "C" esp_err_t LUNASHELL_storage_set_performance_suspended(bool suspended)
{
    (void)suspended;
    return reject_operation("performance suspension");
}

extern "C" esp_err_t LUNASHELL_storage_set_power_suspended(bool suspended)
{
    (void)suspended;
    return reject_operation("power suspension");
}

// pixels: Caller retains this complete frame because disabled storage rejects it.
// width: Requested visible width.
// height: Requested visible height.
// stride: Requested source row stride.
// byte_swapped: Requested source byte order.
// capture_id: Requested monotonic capture identity.
// completion: Completion is not called because ownership never transfers.
// context: Opaque callback value retained by the caller.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED without taking ownership.
extern "C" esp_err_t LUNASHELL_storage_queue_screenshot_rgb565(
    uint16_t *pixels, uint16_t width, uint16_t height, uint16_t stride,
    bool byte_swapped, uint32_t capture_id,
    LUNASHELL_screenshot_completion_t completion, void *context
)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    (void)byte_swapped;
    (void)capture_id;
    (void)completion;
    (void)context;
    return reject_operation("screenshot write");
}
