// Display-brightness settings and panel fade control for QSee.
// Keeps applied brightness separate from the user's preferred value.

#include "qsee_platform.h"
#include "qsee_settings.hpp"

#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "qsee_display_settings";
constexpr char kBrightnessSetting[] = "brightness";
constexpr uint32_t kFadeStepMs = 20;

// Keep the user setting separate from temporary fade levels.
volatile uint32_t s_preferred_brightness_percent = 100;
volatile uint32_t s_applied_brightness_percent = 100;
bool s_panel_enabled = true;
StaticSemaphore_t s_fade_mutex_control;
SemaphoreHandle_t s_fade_mutex;

uint32_t clamp_brightness(uint32_t percent)
{
    return percent < 10U ? 10U : percent > 100U ? 100U : percent;
}

// Lazily create the mutex shared by fades, brightness writes, and panel state.
SemaphoreHandle_t fade_mutex()
{
    if (s_fade_mutex == nullptr) {
        s_fade_mutex = xSemaphoreCreateMutexStatic(&s_fade_mutex_control);
    }
    return s_fade_mutex;
}

// Apply one hardware level without changing the persisted user preference.
esp_err_t apply_runtime_brightness(uint32_t percent)
{
    const esp_err_t result =
        bsp_display_brightness_set(static_cast<int>(percent));
    if (result == ESP_OK) {
        s_applied_brightness_percent = percent;
    }
    return result;
}

// Interpolate the active level; the caller must hold the fade mutex.
esp_err_t fade_locked(uint32_t target_percent, uint32_t duration_ms)
{
    if (!s_panel_enabled && target_percent > 0U) {
        const esp_err_t panel_result = bsp_display_panel_set_enabled(true);
        if (panel_result != ESP_OK) {
            return panel_result;
        }
        s_panel_enabled = true;
        s_applied_brightness_percent = 0;
    }

    const uint32_t start_percent = s_applied_brightness_percent;
    if (start_percent == target_percent || duration_ms == 0U) {
        return apply_runtime_brightness(target_percent);
    }

    const int32_t difference =
        static_cast<int32_t>(target_percent) -
        static_cast<int32_t>(start_percent);
    const uint32_t distance =
        static_cast<uint32_t>(difference < 0 ? -difference : difference);
    uint32_t step_count = duration_ms / kFadeStepMs;
    if (step_count == 0U) {
        step_count = 1U;
    }
    if (step_count > distance) {
        step_count = distance;
    }

    // Limit writes to distinct percentage values while keeping an even cadence.
    const TickType_t step_ticks = pdMS_TO_TICKS(
        duration_ms / step_count > 0U ? duration_ms / step_count : 1U
    );
    TickType_t last_wake = xTaskGetTickCount();
    for (uint32_t step = 1; step <= step_count; ++step) {
        const int32_t interpolated =
            static_cast<int32_t>(start_percent) +
            (difference * static_cast<int32_t>(step)) /
                static_cast<int32_t>(step_count);
        const esp_err_t result =
            apply_runtime_brightness(static_cast<uint32_t>(interpolated));
        if (result != ESP_OK) {
            return result;
        }
        if (step < step_count) {
            vTaskDelayUntil(&last_wake, step_ticks > 0 ? step_ticks : 1);
        }
    }
    return ESP_OK;
}

}  // namespace

// Persist the user preference and apply it immediately to the panel.
extern "C" esp_err_t qsee_display_set_brightness_percent(uint32_t percent)
{
    if (percent < 10U || percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) return init_error;
    }

    SemaphoreHandle_t mutex = fade_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t persist_error =
        settings.setUInt32(kBrightnessSetting, percent);
    if (persist_error != ESP_OK) {
        xSemaphoreGive(mutex);
        return persist_error;
    }
    s_preferred_brightness_percent = percent;
    const esp_err_t display_error = fade_locked(percent, 0);
    xSemaphoreGive(mutex);
    ESP_LOGI(kTag, "AMOLED brightness: %lu%%",
             static_cast<unsigned long>(percent));
    return display_error;
}

extern "C" uint32_t qsee_display_get_brightness_percent(void)
{
    return s_preferred_brightness_percent;
}

// Restore both preferred and active levels from NVS at startup.
extern "C" esp_err_t qsee_display_apply_saved_brightness(void)
{
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) return init_error;
    }
    SemaphoreHandle_t mutex = fade_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    uint32_t brightness = 100;
    const esp_err_t read_error =
        settings.getUInt32(kBrightnessSetting, 100, &brightness);
    if (read_error != ESP_OK) {
        xSemaphoreGive(mutex);
        return read_error;
    }
    brightness = clamp_brightness(brightness);
    s_preferred_brightness_percent = brightness;
    const esp_err_t display_error = fade_locked(brightness, 0);
    xSemaphoreGive(mutex);
    ESP_LOGI(kTag, "Applying saved AMOLED brightness: %lu%%",
             static_cast<unsigned long>(brightness));
    return display_error;
}

// Fade to a transient level without overwriting the user preference.
extern "C" esp_err_t qsee_display_fade_to_percent(
    uint32_t target_percent,
    uint32_t duration_ms
)
{
    if (target_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t mutex = fade_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = fade_locked(target_percent, duration_ms);
    xSemaphoreGive(mutex);
    return result;
}

extern "C" esp_err_t qsee_display_fade_out(uint32_t duration_ms)
{
    return qsee_display_fade_to_percent(0U, duration_ms);
}

extern "C" esp_err_t qsee_display_fade_in(uint32_t duration_ms)
{
    SemaphoreHandle_t mutex = fade_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result =
        fade_locked(s_preferred_brightness_percent, duration_ms);
    xSemaphoreGive(mutex);
    return result;
}

// Toggle panel scan output while preserving the saved brightness preference.
extern "C" esp_err_t qsee_display_set_panel_enabled(bool enabled)
{
    SemaphoreHandle_t mutex = fade_mutex();
    if (mutex == nullptr) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = bsp_display_panel_set_enabled(enabled);
    if (result == ESP_OK) {
        s_panel_enabled = enabled;
        if (!enabled) {
            s_applied_brightness_percent = 0;
        }
    }
    xSemaphoreGive(mutex);
    return result;
}
