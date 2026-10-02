/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_settings.hpp"

#include <algorithm>
#include <mutex>

#include "bsp/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "LUNASHELL_display_settings";
constexpr char kBrightnessSetting[] = "brightness";
constexpr uint32_t kBrightnessDefaultPercent = 100;
constexpr uint32_t kBrightnessMinPercent = 10;
constexpr uint32_t kBrightnessMaxPercent = 100;
constexpr uint32_t kDimmedBrightnessNumerator = 50;
constexpr uint32_t kDimmedBrightnessDenominator = 100;
constexpr uint32_t kFadeStepPeriodMs = 20;
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
constexpr uint32_t kDisplayOperationLockTimeoutMs = 1500;
#else
constexpr uint32_t kDisplayOperationLockTimeoutMs = 1000;
#endif
std::mutex s_brightness_mutex;

class DisplayOperationLock {
public:
    DisplayOperationLock()
        : error_(bsp_display_lock(kDisplayOperationLockTimeoutMs))
    {
    }

    ~DisplayOperationLock()
    {
        if (error_ == ESP_OK) {
            bsp_display_unlock();
        }
    }

    DisplayOperationLock(const DisplayOperationLock &) = delete;
    DisplayOperationLock &operator=(const DisplayOperationLock &) = delete;

    esp_err_t error() const
    {
        return error_;
    }

private:
    esp_err_t error_;
};

esp_err_t read_applied_brightness(uint32_t *percent)
{
    if (percent == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const int brightness = bsp_display_brightness_get();
    if (brightness < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (brightness > static_cast<int>(kBrightnessMaxPercent)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *percent = static_cast<uint32_t>(brightness);
    return ESP_OK;
}

esp_err_t ensure_user_settings()
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    return settings.isInitialized() ? ESP_OK : settings.initialize();
}

void publish_brightness(uint32_t percent, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayBrightness(
        percent,
        error
    );
}

void publish_configured_brightness(uint32_t percent, esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayConfiguredBrightness(
        percent,
        error
    );
}

esp_err_t read_configured_brightness(uint32_t *percent)
{
    if (percent == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        return init_error;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = settings.getUInt32(kBrightnessSetting, percent);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *percent = kBrightnessDefaultPercent;
        ESP_LOGI(
            kTag,
            "Brightness: first boot key '%s' absent; persisting explicit default %lu",
            kBrightnessSetting,
            static_cast<unsigned long>(*percent)
        );
        error = settings.setUInt32(kBrightnessSetting, *percent);
    }
    if (error != ESP_OK) {
        return error;
    }
    if (*percent < kBrightnessMinPercent ||
        *percent > kBrightnessMaxPercent) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

LUNASHELL::UiHardwareSnapshot::DisplayPowerMode snapshot_power_mode(
    LUNASHELL_display_power_mode_t mode
)
{
    switch (mode) {
    case LUNASHELL_DISPLAY_POWER_ACTIVE:
        return LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
    case LUNASHELL_DISPLAY_POWER_DIMMED:
        return LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Dimmed;
    case LUNASHELL_DISPLAY_POWER_OFF:
        return LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
    }
    return LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown;
}

const char *power_mode_name(LUNASHELL_display_power_mode_t mode)
{
    switch (mode) {
    case LUNASHELL_DISPLAY_POWER_ACTIVE:
        return "active";
    case LUNASHELL_DISPLAY_POWER_DIMMED:
        return "dimmed";
    case LUNASHELL_DISPLAY_POWER_OFF:
        return "off";
    }
    return "unknown";
}

esp_err_t transition_power_mode_locked(
    LUNASHELL_display_power_mode_t mode,
    uint32_t duration_ms
)
{
    const auto report_failure = [&](esp_err_t error) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
            error
        );
        ESP_LOGE(
            kTag,
            "Display transition to %s failed (%s)",
            power_mode_name(mode),
            esp_err_to_name(error)
        );
        return error;
    };
    if (snapshot_power_mode(mode) ==
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown) {
        return report_failure(ESP_ERR_INVALID_ARG);
    }

    uint32_t configured_brightness = 0;
    if (mode != LUNASHELL_DISPLAY_POWER_OFF) {
        const esp_err_t configured_error =
            read_configured_brightness(&configured_brightness);
        publish_configured_brightness(
            configured_brightness,
            configured_error
        );
        if (configured_error != ESP_OK) {
            return report_failure(configured_error);
        }
    }
    const uint32_t target_brightness =
        mode == LUNASHELL_DISPLAY_POWER_ACTIVE
            ? configured_brightness
            : mode == LUNASHELL_DISPLAY_POWER_DIMMED
                ? configured_brightness *
                      kDimmedBrightnessNumerator /
                      kDimmedBrightnessDenominator
                : 0U;

    uint32_t current_brightness = 0;
    const esp_err_t read_error =
        read_applied_brightness(&current_brightness);
    if (read_error != ESP_OK) {
        publish_brightness(0, read_error);
        return report_failure(read_error);
    }

    if (mode != LUNASHELL_DISPLAY_POWER_OFF) {
        const esp_err_t panel_error =
            bsp_display_panel_set_enabled(true);
        if (panel_error != ESP_OK) {
            publish_brightness(current_brightness, ESP_OK);
            return report_failure(panel_error);
        }
    }

    const uint32_t step_count = duration_ms == 0U
        ? 1U
        : std::max<uint32_t>(
              1,
              std::min<uint32_t>(
                  24,
                  duration_ms / kFadeStepPeriodMs
              )
          );
    for (uint32_t step = 1; step <= step_count; ++step) {
        const int32_t delta =
            static_cast<int32_t>(target_brightness) -
            static_cast<int32_t>(current_brightness);
        const int32_t interpolated =
            static_cast<int32_t>(current_brightness) +
            delta * static_cast<int32_t>(step) /
                static_cast<int32_t>(step_count);
        const esp_err_t step_error =
            bsp_display_brightness_set(interpolated);
        if (step_error != ESP_OK) {
            publish_brightness(
                static_cast<uint32_t>(
                    std::max<int32_t>(interpolated, 0)
                ),
                step_error
            );
            return report_failure(step_error);
        }
        if (duration_ms > 0U && step < step_count) {
            vTaskDelay(pdMS_TO_TICKS(duration_ms / step_count));
        }
    }

    uint32_t applied_brightness = 0;
    const esp_err_t verify_error =
        read_applied_brightness(&applied_brightness);
    if (verify_error != ESP_OK) {
        publish_brightness(0, verify_error);
        return report_failure(verify_error);
    }
    const bool target_missed =
        applied_brightness + 1U < target_brightness ||
        applied_brightness > target_brightness + 1U;
    if (target_missed) {
        publish_brightness(
            applied_brightness,
            ESP_ERR_INVALID_RESPONSE
        );
        ESP_LOGE(
            kTag,
            "Display transition %s missed target: requested=%lu readback=%lu",
            power_mode_name(mode),
            static_cast<unsigned long>(target_brightness),
            static_cast<unsigned long>(applied_brightness)
        );
        return report_failure(ESP_ERR_INVALID_RESPONSE);
    }

    publish_brightness(applied_brightness, ESP_OK);
    if (mode == LUNASHELL_DISPLAY_POWER_OFF) {
        const esp_err_t panel_error =
            bsp_display_panel_set_enabled(false);
        if (panel_error != ESP_OK) {
            return report_failure(panel_error);
        }
    }
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
        snapshot_power_mode(mode),
        ESP_OK
    );
    ESP_LOGI(
        kTag,
        "Display transition %s: configured=%lu applied=%lu duration=%lu ms",
        power_mode_name(mode),
        static_cast<unsigned long>(configured_brightness),
        static_cast<unsigned long>(applied_brightness),
        static_cast<unsigned long>(duration_ms)
    );
    return ESP_OK;
}

}  // namespace

extern "C" esp_err_t LUNASHELL_display_set_brightness_percent(uint32_t percent)
{
    if (percent < kBrightnessMinPercent ||
        percent > kBrightnessMaxPercent) {
        publish_brightness(0, ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            kTag,
            "Brightness %lu outside [%lu, %lu]",
            static_cast<unsigned long>(percent),
            static_cast<unsigned long>(kBrightnessMinPercent),
            static_cast<unsigned long>(kBrightnessMaxPercent)
        );
        return ESP_ERR_INVALID_ARG;
    }

    DisplayOperationLock display_lock;
    if (display_lock.error() != ESP_OK) {
        publish_brightness(0, display_lock.error());
        ESP_LOGE(
            kTag,
            "Brightness update display lock failed (%s)",
            esp_err_to_name(display_lock.error())
        );
        return display_lock.error();
    }
    std::lock_guard<std::mutex> lock(s_brightness_mutex);
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        publish_brightness(0, init_error);
        ESP_LOGE(
            kTag,
            "Brightness settings initialization failed (%s)",
            esp_err_to_name(init_error)
        );
        return init_error;
    }

    uint32_t previous_percent = 0;
    const esp_err_t read_error =
        read_applied_brightness(&previous_percent);
    if (read_error != ESP_OK) {
        publish_brightness(0, read_error);
        ESP_LOGE(
            kTag,
            "Reading applied brightness failed (%s)",
            esp_err_to_name(read_error)
        );
        return read_error;
    }

    const esp_err_t display_error =
        bsp_display_brightness_set(static_cast<int>(percent));
    if (display_error != ESP_OK) {
        publish_brightness(previous_percent, display_error);
        ESP_LOGE(
            kTag,
            "Applying brightness failed (%s)",
            esp_err_to_name(display_error)
        );
        return display_error;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    const esp_err_t persist_error =
        settings.setUInt32(kBrightnessSetting, percent);
    if (persist_error != ESP_OK) {
        const esp_err_t rollback_error =
            bsp_display_brightness_set(static_cast<int>(previous_percent));
        publish_brightness(previous_percent, persist_error);
        ESP_LOGE(
            kTag,
            "Brightness persistence failed (%s)",
            esp_err_to_name(persist_error)
        );
        if (rollback_error != ESP_OK) {
            ESP_LOGW(
                kTag,
                "Brightness persistence failed (%s); rollback also failed (%s)",
                esp_err_to_name(persist_error),
                esp_err_to_name(rollback_error)
            );
        }
        return persist_error;
    }

    publish_brightness(percent, ESP_OK);
    publish_configured_brightness(percent, ESP_OK);
    LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active,
        ESP_OK
    );
    ESP_LOGI(kTag, "AMOLED brightness: %lu%%",
             static_cast<unsigned long>(percent));
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_display_get_brightness_percent(uint32_t *percent)
{
    if (percent == nullptr) {
        ESP_LOGE(kTag, "Brightness read rejected: output is null");
        return ESP_ERR_INVALID_ARG;
    }
    DisplayOperationLock display_lock;
    if (display_lock.error() != ESP_OK) {
        publish_brightness(0, display_lock.error());
        ESP_LOGE(
            kTag,
            "Brightness read display lock failed (%s)",
            esp_err_to_name(display_lock.error())
        );
        return display_lock.error();
    }
    std::lock_guard<std::mutex> lock(s_brightness_mutex);
    const esp_err_t error = read_applied_brightness(percent);
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Reading applied brightness failed (%s)",
            esp_err_to_name(error)
        );
    }
    return error;
}

extern "C" esp_err_t LUNASHELL_display_apply_saved_brightness(void)
{
    DisplayOperationLock display_lock;
    if (display_lock.error() != ESP_OK) {
        publish_brightness(0, display_lock.error());
        ESP_LOGE(
            kTag,
            "Saved brightness display lock failed (%s)",
            esp_err_to_name(display_lock.error())
        );
        return display_lock.error();
    }
    std::lock_guard<std::mutex> lock(s_brightness_mutex);
    const esp_err_t init_error = ensure_user_settings();
    if (init_error != ESP_OK) {
        publish_brightness(0, init_error);
        ESP_LOGE(
            kTag,
            "Brightness settings initialization failed (%s)",
            esp_err_to_name(init_error)
        );
        return init_error;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    uint32_t brightness = 0;
    esp_err_t read_error =
        settings.getUInt32(kBrightnessSetting, &brightness);
    if (read_error == ESP_ERR_NVS_NOT_FOUND) {
        brightness = kBrightnessDefaultPercent;
        ESP_LOGI(
            kTag,
            "Brightness: first boot key '%s' absent; "
            "persisting explicit default %lu",
            kBrightnessSetting,
            static_cast<unsigned long>(brightness)
        );
        read_error = settings.setUInt32(kBrightnessSetting, brightness);
        if (read_error != ESP_OK) {
            publish_brightness(0, read_error);
            ESP_LOGE(
                kTag,
                "Persisting first-boot brightness failed (%s)",
                esp_err_to_name(read_error)
            );
            return read_error;
        }
    } else if (read_error != ESP_OK) {
        publish_brightness(0, read_error);
        ESP_LOGE(
            kTag,
            "Reading persisted brightness failed (%s)",
            esp_err_to_name(read_error)
        );
        return read_error;
    }

    if (brightness < kBrightnessMinPercent ||
        brightness > kBrightnessMaxPercent) {
        publish_brightness(0, ESP_ERR_INVALID_ARG);
        ESP_LOGE(
            kTag,
            "Persisted brightness %lu outside [%lu, %lu]; "
            "explicit reset required",
            static_cast<unsigned long>(brightness),
            static_cast<unsigned long>(kBrightnessMinPercent),
            static_cast<unsigned long>(kBrightnessMaxPercent)
        );
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(kTag, "Applying saved AMOLED brightness: %lu%%",
             static_cast<unsigned long>(brightness));
    const esp_err_t display_error =
        bsp_display_brightness_set(static_cast<int>(brightness));
    publish_brightness(brightness, display_error);
    publish_configured_brightness(brightness, display_error);
    if (display_error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Applying persisted brightness failed (%s)",
            esp_err_to_name(display_error)
        );
    } else {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active,
            ESP_OK
        );
    }
    return display_error;
}

extern "C" esp_err_t LUNASHELL_display_set_power_mode(
    LUNASHELL_display_power_mode_t mode
)
{
    DisplayOperationLock display_lock;
    if (display_lock.error() != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
            display_lock.error()
        );
        ESP_LOGE(
            kTag,
            "Display power-mode lock failed (%s)",
            esp_err_to_name(display_lock.error())
        );
        return display_lock.error();
    }
    std::lock_guard<std::mutex> lock(s_brightness_mutex);
    return transition_power_mode_locked(mode, 0);
}

extern "C" esp_err_t LUNASHELL_display_fade_to_power_mode(
    LUNASHELL_display_power_mode_t mode,
    uint32_t duration_ms
)
{
    DisplayOperationLock display_lock;
    if (display_lock.error() != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
            display_lock.error()
        );
        ESP_LOGE(
            kTag,
            "Display fade lock failed (%s)",
            esp_err_to_name(display_lock.error())
        );
        return display_lock.error();
    }
    std::lock_guard<std::mutex> lock(s_brightness_mutex);
    return transition_power_mode_locked(mode, duration_ms);
}
