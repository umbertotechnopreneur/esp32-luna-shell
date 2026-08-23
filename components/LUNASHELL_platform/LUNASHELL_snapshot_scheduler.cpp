#include "LUNASHELL_platform.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_snapshot_cadence.hpp"

namespace {

constexpr char kTag[] = "LUNASHELL_full_snapshot";
constexpr char kPeriodSetting[] = "snapshot_ms";
constexpr char kRefreshReason[] = "periodic-full";
constexpr uint32_t kDefaultPeriodMs = 1000;
constexpr uint32_t kPowerSuspendTimeoutMs = 5000;
constexpr EventBits_t kWorkerQuiescentBit =
    static_cast<EventBits_t>(1U << 0);
// A full refresh keeps two DeviceSnapshot values on the caller stack.
constexpr uint32_t kTaskStackBytes = 12U * 1024U;
constexpr UBaseType_t kTaskPriority = 2;
static_assert(
    CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U >=
        LUNASHELL::kFullSnapshotPeriodsMs.back() + 1000U,
    "Task WDT must outlive the longest snapshot cadence with margin"
);
std::atomic<uint32_t> s_period_ms{kDefaultPeriodMs};
std::atomic_bool s_period_valid{false};
std::atomic_bool s_monitor_requested{false};
std::atomic_bool s_performance_suspended{false};
std::atomic_bool s_power_suspended{false};
std::atomic_bool s_monitor_active{false};
bool s_task_started = false;
TaskHandle_t s_task_handle = nullptr;
EventGroupHandle_t s_task_events = nullptr;
std::mutex s_settings_mutex;
std::mutex s_power_transition_mutex;
StaticTask_t s_task_control{};
StackType_t s_task_stack[kTaskStackBytes]{};
StaticEventGroup_t s_task_event_storage{};

void publish_period(uint32_t period_ms, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportFullSnapshotPeriod(
        period_ms,
        error
    );
}

void publish_monitor(bool active, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportFullSnapshotMonitor(
        active,
        error
    );
}

esp_err_t load_or_create_period_locked()
{
    LUNASHELL::UserSettingsService &settings =
        LUNASHELL::UserSettingsService::instance();
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

    if (!LUNASHELL::isFullSnapshotPeriodSupported(saved_period_ms)) {
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

// Park without polling whenever no lifecycle owner needs Full Snapshots.
void full_snapshot_task(void *)
{
    bool stack_reported = false;
    bool watch_active = false;
    while (true) {
        if (!s_monitor_active.load(std::memory_order_acquire)) {
            if (watch_active) {
                const esp_err_t end_result = LUNASHELL_liveness_watch_end(
                    LUNASHELL_LIVENESS_SNAPSHOT,
                    ESP_OK
                );
                if (end_result != ESP_OK) {
                    ESP_LOGE(
                        kTag,
                        "Full Snapshot watchdog stop failed (%s)",
                        esp_err_to_name(end_result)
                    );
                    std::abort();
                }
                watch_active = false;
            }
            (void)xEventGroupSetBits(
                s_task_events,
                kWorkerQuiescentBit
            );
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        (void)xEventGroupClearBits(s_task_events, kWorkerQuiescentBit);
        if (!s_monitor_active.load(std::memory_order_acquire)) {
            continue;
        }

        esp_err_t liveness_result = watch_active
            ? LUNASHELL_liveness_watch_progress(
                  LUNASHELL_LIVENESS_SNAPSHOT
              )
            : LUNASHELL_liveness_watch_begin(
                  LUNASHELL_LIVENESS_SNAPSHOT
              );
        if (liveness_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Full Snapshot watchdog cycle start failed (%s)",
                esp_err_to_name(liveness_result)
            );
            std::abort();
        }
        watch_active = true;

        const int64_t started_at_us = esp_timer_get_time();
        const uint32_t full_snapshot_period_ms =
            s_period_ms.load(std::memory_order_acquire);
        const uint32_t diagnostic_reuse_limit_ms = std::min(
            full_snapshot_period_ms,
            LUNASHELL_hardware_get_diagnostic_period_ms()
        );
        const esp_err_t refresh_error =
            LUNASHELL::DeviceSnapshotService::instance().refresh(
                kRefreshReason,
                diagnostic_reuse_limit_ms
            );
        if (refresh_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Periodic Full Snapshot refresh failed (%s)",
                esp_err_to_name(refresh_error)
            );
        }
        liveness_result = LUNASHELL_liveness_watch_progress(
            LUNASHELL_LIVENESS_SNAPSHOT
        );
        if (liveness_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Full Snapshot watchdog completion failed (%s)",
                esp_err_to_name(liveness_result)
            );
            std::abort();
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
        // Keep the quiescence acknowledgement clear for the entire active
        // lifecycle, including cadence sleep. A power transition may consume
        // it only after the inactive branch closes the SNAPSHOT watch.
        (void)ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(std::max<uint32_t>(1, remaining_ms))
        );
    }
}

// Create the worker and its static quiescence acknowledgement channel once.
esp_err_t ensure_task_locked()
{
    if (s_task_started) {
        return ESP_OK;
    }

    if (s_task_events == nullptr) {
        s_task_events = xEventGroupCreateStatic(&s_task_event_storage);
        if (s_task_events == nullptr) {
            ESP_LOGE(kTag, "Full Snapshot event group creation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    s_task_handle = xTaskCreateStatic(
        full_snapshot_task,
        "LUNASHELL_full_snapshot",
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

// Combine independent lifecycle reasons without overwriting app intent.
void apply_effective_monitor_state_locked()
{
    const bool active =
        s_monitor_requested.load(std::memory_order_relaxed) &&
        !s_performance_suspended.load(std::memory_order_relaxed) &&
        !s_power_suspended.load(std::memory_order_relaxed);
    s_monitor_active.store(active, std::memory_order_release);
    if (s_task_handle != nullptr) {
        xTaskNotifyGive(s_task_handle);
    }
    publish_monitor(active, ESP_OK);
}

}  // namespace

extern "C" esp_err_t LUNASHELL_hardware_start_full_snapshot_monitor(void)
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

    s_monitor_requested.store(true, std::memory_order_relaxed);
    apply_effective_monitor_state_locked();
    const bool active = s_monitor_active.load(std::memory_order_acquire);
    ESP_LOGI(
        kTag,
        "Full Snapshot monitor %s at %lu ms",
        active ? "active" : "suppressed by Performance Mode",
        static_cast<unsigned long>(
            s_period_ms.load(std::memory_order_acquire)
        )
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_hardware_stop_full_snapshot_monitor(void)
{
    std::lock_guard<std::mutex> lock(s_settings_mutex);
    s_monitor_requested.store(false, std::memory_order_relaxed);
    apply_effective_monitor_state_locked();
    ESP_LOGI(kTag, "Full Snapshot monitor standby");
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_hardware_set_full_snapshot_performance_suspended(
    bool suspended
)
{
    std::lock_guard<std::mutex> lock(s_settings_mutex);
    s_performance_suspended.store(suspended, std::memory_order_relaxed);
    apply_effective_monitor_state_locked();
    ESP_LOGI(
        kTag,
        "Full Snapshot Performance Mode suppression: %s",
        suspended ? "active" : "inactive"
    );
    return ESP_OK;
}

// Quiesce the worker synchronously while preserving its requested lifecycle.
extern "C" esp_err_t LUNASHELL_hardware_set_full_snapshot_power_suspended(
    bool suspended
)
{
    std::lock_guard<std::mutex> transition_lock(s_power_transition_mutex);
    bool wait_for_quiescence = false;
    {
        std::lock_guard<std::mutex> lock(s_settings_mutex);
        s_power_suspended.store(suspended, std::memory_order_relaxed);
        apply_effective_monitor_state_locked();
        wait_for_quiescence = suspended && s_task_started;
    }

    if (wait_for_quiescence) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_task_events,
            kWorkerQuiescentBit,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(kPowerSuspendTimeoutMs)
        );
        if ((bits & kWorkerQuiescentBit) == 0U) {
            {
                std::lock_guard<std::mutex> lock(s_settings_mutex);
                s_power_suspended.store(false, std::memory_order_relaxed);
                apply_effective_monitor_state_locked();
                publish_monitor(
                    s_monitor_active.load(std::memory_order_acquire),
                    ESP_ERR_TIMEOUT
                );
            }
            ESP_LOGE(
                kTag,
                "Full Snapshot power suspension timed out after %lu ms; request rolled back (%s)",
                static_cast<unsigned long>(kPowerSuspendTimeoutMs),
                esp_err_to_name(ESP_ERR_TIMEOUT)
            );
            return ESP_ERR_TIMEOUT;
        }
    }

    ESP_LOGI(
        kTag,
        "Full Snapshot power suspension: %s",
        suspended ? "active" : "inactive"
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_hardware_set_full_snapshot_period_ms(
    uint32_t period_ms
)
{
    if (!LUNASHELL::isFullSnapshotPeriodSupported(period_ms)) {
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
    LUNASHELL::UserSettingsService &settings =
        LUNASHELL::UserSettingsService::instance();
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
