#include "qsee_platform.h"

#include <atomic>
#include <mutex>

#include "esp_log.h"
#include "qsee_logger.h"

namespace {

constexpr char kTag[] = "qsee_performance";
std::atomic_bool s_enabled{false};
std::mutex s_transition_mutex;

void log_rollback_failure(const char *component, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Performance Mode rollback failed for %s (%s)",
            component,
            esp_err_to_name(error)
        );
    }
}

}  // namespace

extern "C" esp_err_t qsee_performance_mode_set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_transition_mutex);
    if (s_enabled.load(std::memory_order_acquire) == enabled) {
        return ESP_OK;
    }

    if (enabled) {
        esp_err_t error = qsee_storage_set_performance_suspended(true);
        if (error != ESP_OK) {
            return error;
        }
        error = qsee_voice_afe_set_performance_suspended(true);
        if (error != ESP_OK) {
            log_rollback_failure(
                "SD logger",
                qsee_storage_set_performance_suspended(false)
            );
            return error;
        }
        error =
            qsee_hardware_set_full_snapshot_performance_suspended(true);
        if (error != ESP_OK) {
            log_rollback_failure(
                "voice AFE",
                qsee_voice_afe_set_performance_suspended(false)
            );
            log_rollback_failure(
                "SD logger",
                qsee_storage_set_performance_suspended(false)
            );
            return error;
        }
        error = qsee_logger_set_capture_suspended(true);
        if (error != ESP_OK) {
            log_rollback_failure(
                "Full Snapshot",
                qsee_hardware_set_full_snapshot_performance_suspended(false)
            );
            log_rollback_failure(
                "voice AFE",
                qsee_voice_afe_set_performance_suspended(false)
            );
            log_rollback_failure(
                "SD logger",
                qsee_storage_set_performance_suspended(false)
            );
            return error;
        }
    } else {
        esp_err_t error = qsee_logger_set_capture_suspended(false);
        if (error != ESP_OK) {
            return error;
        }
        error =
            qsee_hardware_set_full_snapshot_performance_suspended(false);
        if (error != ESP_OK) {
            log_rollback_failure(
                "RAM logger",
                qsee_logger_set_capture_suspended(true)
            );
            return error;
        }
        error = qsee_voice_afe_set_performance_suspended(false);
        if (error != ESP_OK) {
            log_rollback_failure(
                "Full Snapshot",
                qsee_hardware_set_full_snapshot_performance_suspended(true)
            );
            log_rollback_failure(
                "RAM logger",
                qsee_logger_set_capture_suspended(true)
            );
            return error;
        }
        error = qsee_storage_set_performance_suspended(false);
        if (error != ESP_OK) {
            log_rollback_failure(
                "voice AFE",
                qsee_voice_afe_set_performance_suspended(true)
            );
            log_rollback_failure(
                "Full Snapshot",
                qsee_hardware_set_full_snapshot_performance_suspended(true)
            );
            log_rollback_failure(
                "RAM logger",
                qsee_logger_set_capture_suspended(true)
            );
            return error;
        }
    }

    s_enabled.store(enabled, std::memory_order_release);
    esp_log_level_set(
        "*",
        enabled ? ESP_LOG_WARN : ESP_LOG_INFO
    );
    ESP_LOGI(
        kTag,
        "Performance Mode: %s; connectivity and sensor services unchanged",
        enabled ? "enabled" : "disabled"
    );
    return ESP_OK;
}

extern "C" bool qsee_performance_mode_get_enabled(void)
{
    return s_enabled.load(std::memory_order_acquire);
}
