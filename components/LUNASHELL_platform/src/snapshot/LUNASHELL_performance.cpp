/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_platform.h"

#include <atomic>
#include <cstddef>
#include <mutex>

#include "esp_log.h"
#include "LUNASHELL_logger.h"

namespace {

constexpr char kTag[] = "LUNASHELL_performance";
std::atomic_bool s_enabled{false};
std::atomic_bool s_state_known{true};
std::atomic<esp_err_t> s_primary_error{ESP_OK};
std::atomic<esp_err_t> s_rollback_error{ESP_OK};
std::mutex s_transition_mutex;

using PerformanceSetter = esp_err_t (*)(bool suspended);

struct PerformanceStep {
    const char *component;
    PerformanceSetter set_suspended;
};

const PerformanceStep kEnableSteps[] = {
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
    {"SD logger", LUNASHELL_storage_set_performance_suspended},
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    {"voice AFE", LUNASHELL_voice_afe_set_performance_suspended},
#endif
    {
        "Full Snapshot",
        LUNASHELL_hardware_set_full_snapshot_performance_suspended,
    },
    {"RAM logger", LUNASHELL_logger_set_capture_suspended},
};

const PerformanceStep kDisableSteps[] = {
    {"RAM logger", LUNASHELL_logger_set_capture_suspended},
    {
        "Full Snapshot",
        LUNASHELL_hardware_set_full_snapshot_performance_suspended,
    },
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    {"voice AFE", LUNASHELL_voice_afe_set_performance_suspended},
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
    {"SD logger", LUNASHELL_storage_set_performance_suspended},
#endif
};

void log_primary_failure(
    const PerformanceStep &step,
    bool enabled,
    esp_err_t error
)
{
    ESP_LOGE(
        kTag,
        "Performance Mode %s failed for %s (%s); state is unknown",
        enabled ? "enable" : "disable",
        step.component,
        esp_err_to_name(error)
    );
}

esp_err_t rollback_applied_steps(
    const PerformanceStep *steps,
    std::size_t applied_count,
    bool previous_enabled
)
{
    esp_err_t first_error = ESP_OK;
    while (applied_count > 0U) {
        const PerformanceStep &step = steps[--applied_count];
        const esp_err_t error = step.set_suspended(previous_enabled);
        if (error == ESP_OK) {
            continue;
        }
        if (first_error == ESP_OK) {
            first_error = error;
        }
        ESP_LOGE(
            kTag,
            "Performance Mode rollback failed for %s (%s)",
            step.component,
            esp_err_to_name(error)
        );
    }
    return first_error;
}

esp_err_t apply_steps(bool enabled, bool reconcile_unknown_state)
{
    const PerformanceStep *steps = enabled ? kEnableSteps : kDisableSteps;
    const std::size_t step_count = enabled
        ? sizeof(kEnableSteps) / sizeof(kEnableSteps[0])
        : sizeof(kDisableSteps) / sizeof(kDisableSteps[0]);

    std::size_t applied_count = 0U;
    for (; applied_count < step_count; ++applied_count) {
        const PerformanceStep &step = steps[applied_count];
        const esp_err_t error = step.set_suspended(enabled);
        if (error == ESP_OK) {
            continue;
        }

        s_state_known.store(false, std::memory_order_release);
        s_primary_error.store(error, std::memory_order_release);
        log_primary_failure(step, enabled, error);

        esp_err_t rollback_error = ESP_OK;
        if (!reconcile_unknown_state) {
            rollback_error = rollback_applied_steps(
                steps,
                applied_count,
                !enabled
            );
            s_rollback_error.store(rollback_error, std::memory_order_release);
        }
        if (rollback_error == ESP_OK && !reconcile_unknown_state) {
            ESP_LOGE(
                kTag,
                "Performance Mode rollback completed after primary error %s; "
                "the failing component remains unverified",
                esp_err_to_name(error)
            );
        }
        return error;
    }

    return ESP_OK;
}

}  // namespace

// enabled: Select the session Performance Mode without overriding the compiled logging policy.
extern "C" esp_err_t LUNASHELL_performance_mode_set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_transition_mutex);
    const bool state_known = s_state_known.load(std::memory_order_acquire);
    if (state_known &&
        s_enabled.load(std::memory_order_acquire) == enabled) {
        return ESP_OK;
    }

    if (!state_known) {
        ESP_LOGW(
            kTag,
            "Reconciling unknown Performance Mode state to %s",
            enabled ? "enabled" : "disabled"
        );
    }

    const esp_err_t error = apply_steps(enabled, !state_known);
    if (error != ESP_OK) {
        return error;
    }

    s_enabled.store(enabled, std::memory_order_release);
    s_primary_error.store(ESP_OK, std::memory_order_release);
    s_rollback_error.store(ESP_OK, std::memory_order_release);
    s_state_known.store(true, std::memory_order_release);
    esp_log_level_set(
        "*",
        enabled ? ESP_LOG_WARN : static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL)
    );
    ESP_LOGI(
        kTag,
        "Performance Mode: %s; connectivity and sensor services unchanged",
        enabled ? "enabled" : "disabled"
    );
    return ESP_OK;
}

extern "C" bool LUNASHELL_performance_mode_get_enabled(void)
{
    return s_enabled.load(std::memory_order_acquire);
}

extern "C" void LUNASHELL_performance_mode_get_status(
    LUNASHELL_performance_mode_status_t *status
)
{
    if (status == nullptr) {
        ESP_LOGE(kTag, "Performance Mode status output is null");
        return;
    }

    std::lock_guard<std::mutex> lock(s_transition_mutex);
    status->enabled = s_enabled.load(std::memory_order_relaxed);
    status->state_known = s_state_known.load(std::memory_order_relaxed);
    status->primary_error = s_primary_error.load(std::memory_order_relaxed);
    status->rollback_error = s_rollback_error.load(std::memory_order_relaxed);
}
