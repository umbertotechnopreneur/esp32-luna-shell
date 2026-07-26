#include "qsee_platform.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"
#include "qsee_snapshot_cadence.hpp"

namespace {

constexpr char kTag[] = "qsee_full_snapshot";
constexpr char kPeriodSetting[] = "snapshot_ms";
constexpr char kRefreshReason[] = "periodic-full";
constexpr uint32_t kDefaultPeriodMs = 1000;
constexpr uint32_t kStandbyDelayMs = 250;
// A full refresh keeps two DeviceSnapshot values on the caller stack.
constexpr uint32_t kTaskStackBytes = 12U * 1024U;
constexpr UBaseType_t kTaskPriority = 2;
std::atomic<uint32_t> s_period_ms{kDefaultPeriodMs};
std::atomic_bool s_period_valid{false};
std::atomic_bool s_monitor_active{false};
bool s_task_started = false;
TaskHandle_t s_task_handle = nullptr;
std::mutex s_settings_mutex;
StaticTask_t s_task_control{};
StackType_t s_task_stack[kTaskStackBytes]{};

void publish_period(uint32_t period_ms, esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportFullSnapshotPeriod(
        period_ms,
        error
    );
}

void publish_monitor(bool active, esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportFullSnapshotMonitor(
        active,
        error
    );
}

esp_err_t load_or_create_period_locked()
{
    qsee::UserSettingsService &settings =
        qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        publish_period(0, error);
        ESP_LOGE(
            kTag,
            "Full Snapshot cadence initialization failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }

    uint32_t saved_period_ms = 0;
    error = settings.getUInt32(kPeriodSetting, &saved_period_ms);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = settings.setUInt32(kPeriodSetting, kDefaultPeriodMs);
        if (error != ESP_OK) {
            publish_period(kDefaultPeriodMs, error);
            ESP_LOGE(
                kTag,
                "First-boot Full Snapshot default could not be persisted "
                "(key=%s value=%lu ms error=%s)",
                kPeriodSetting,
                static_cast<unsigned long>(kDefaultPeriodMs),
                esp_err_to_name(error)
            );
            return error;
        }
        saved_period_ms = kDefaultPeriodMs;
        ESP_LOGI(
            kTag,
            "First-boot Full Snapshot default persisted "
            "(key=%s value=%lu ms)",
            kPeriodSetting,
            static_cast<unsigned long>(saved_period_ms)
        );
    } else if (error != ESP_OK) {
        publish_period(0, error);
        ESP_LOGE(
            kTag,
            "Full Snapshot cadence read failed "
            "(key=%s error=%s)",
            kPeriodSetting,
            esp_err_to_name(error)
        );
        return error;
    }

    if (!qsee::isFullSnapshotPeriodSupported(saved_period_ms)) {
        publish_period(saved_period_ms, ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            kTag,
            "Full Snapshot cadence rejected "
            "(key=%s value=%lu ms error=%s)",
            kPeriodSetting,
            static_cast<unsigned long>(saved_period_ms),
            esp_err_to_name(ESP_ERR_INVALID_ARG)
        );
        return ESP_ERR_INVALID_ARG;
    }

    s_period_ms.store(saved_period_ms, std::memory_order_release);
    s_period_valid.store(true, std::memory_order_release);
    publish_period(saved_period_ms, ESP_OK);
    return ESP_OK;
}

void full_snapshot_task(void *)
{
    bool stack_reported = false;
    while (true) {
        if (!s_monitor_active.load(std::memory_order_acquire)) {
            (void)ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(kStandbyDelayMs)
            );
            continue;
        }

        const int64_t started_at_us = esp_timer_get_time();
        const esp_err_t refresh_error =
            qsee_hardware_refresh(kRefreshReason);
        if (refresh_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Periodic Full Snapshot refresh failed (%s)",
                esp_err_to_name(refresh_error)
            );
        }
        if (!stack_reported) {
            ESP_LOGI(
                kTag,
                "Full Snapshot stack high-water mark: %lu bytes",
                static_cast<unsigned long>(
                    uxTaskGetStackHighWaterMark(nullptr) *
                    sizeof(StackType_t)
                )
            );
            stack_reported = true;
        }

        const uint32_t period_ms =
            s_period_ms.load(std::memory_order_acquire);
        const int64_t elapsed_us = esp_timer_get_time() - started_at_us;
        const uint32_t elapsed_ms =
            elapsed_us > 0
                ? static_cast<uint32_t>(elapsed_us / 1000)
                : 0;
        const uint32_t remaining_ms =
            elapsed_ms < period_ms ? period_ms - elapsed_ms : 1;
        (void)ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(std::max<uint32_t>(1, remaining_ms))
        );
    }
}

esp_err_t ensure_task_locked()
{
    if (s_task_started) {
        return ESP_OK;
    }

    s_task_handle = xTaskCreateStatic(
        full_snapshot_task,
        "qsee_full_snapshot",
        sizeof(s_task_stack) / sizeof(s_task_stack[0]),
        nullptr,
        kTaskPriority,
        s_task_stack,
        &s_task_control
    );
    if (s_task_handle == nullptr) {
        ESP_LOGE(kTag, "Full Snapshot worker creation failed");
        return ESP_ERR_NO_MEM;
    }

    s_task_started = true;
    ESP_LOGI(kTag, "Full Snapshot worker ready");
    return ESP_OK;
}

}  // namespace

extern "C" esp_err_t qsee_hardware_start_full_snapshot_monitor(void)
{
    std::lock_guard<std::mutex> lock(s_settings_mutex);
    if (!s_period_valid.load(std::memory_order_acquire)) {
        const esp_err_t load_error = load_or_create_period_locked();
        if (load_error != ESP_OK) {
            publish_monitor(false, load_error);
            return load_error;
        }
    }

    const esp_err_t task_error = ensure_task_locked();
    if (task_error != ESP_OK) {
        publish_monitor(false, task_error);
        return task_error;
    }

    s_monitor_active.store(true, std::memory_order_release);
    xTaskNotifyGive(s_task_handle);
    publish_monitor(true, ESP_OK);
    ESP_LOGI(
        kTag,
        "Full Snapshot monitor active at %lu ms",
        static_cast<unsigned long>(
            s_period_ms.load(std::memory_order_acquire)
        )
    );
    return ESP_OK;
}

extern "C" esp_err_t qsee_hardware_stop_full_snapshot_monitor(void)
{
    std::lock_guard<std::mutex> lock(s_settings_mutex);
    s_monitor_active.store(false, std::memory_order_release);
    if (s_task_handle != nullptr) {
        xTaskNotifyGive(s_task_handle);
    }
    publish_monitor(false, ESP_OK);
    ESP_LOGI(kTag, "Full Snapshot monitor standby");
    return ESP_OK;
}

extern "C" esp_err_t qsee_hardware_set_full_snapshot_period_ms(
    uint32_t period_ms
)
{
    if (!qsee::isFullSnapshotPeriodSupported(period_ms)) {
        publish_period(period_ms, ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            kTag,
            "Full Snapshot cadence update rejected "
            "(value=%lu ms error=%s)",
            static_cast<unsigned long>(period_ms),
            esp_err_to_name(ESP_ERR_INVALID_ARG)
        );
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_settings_mutex);
    qsee::UserSettingsService &settings =
        qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error == ESP_OK) {
        error = settings.setUInt32(kPeriodSetting, period_ms);
    }
    if (error != ESP_OK) {
        publish_period(period_ms, error);
        ESP_LOGE(
            kTag,
            "Full Snapshot cadence persistence failed "
            "(key=%s value=%lu ms error=%s)",
            kPeriodSetting,
            static_cast<unsigned long>(period_ms),
            esp_err_to_name(error)
        );
        return error;
    }

    s_period_ms.store(period_ms, std::memory_order_release);
    s_period_valid.store(true, std::memory_order_release);
    if (s_task_handle != nullptr) {
        xTaskNotifyGive(s_task_handle);
    }
    publish_period(period_ms, ESP_OK);
    ESP_LOGI(
        kTag,
        "Full Snapshot cadence changed to %lu ms",
        static_cast<unsigned long>(period_ms)
    );
    return ESP_OK;
}
