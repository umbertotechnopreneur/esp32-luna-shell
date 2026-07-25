/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"

#include <mutex>

#include "bsp/display.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "qsee_display_settings";
constexpr char kBrightnessSetting[] = "brightness";
constexpr uint32_t kBrightnessDefaultPercent = 100;
constexpr uint32_t kBrightnessMinPercent = 10;
constexpr uint32_t kBrightnessMaxPercent = 100;
std::mutex s_brightness_mutex;

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
    auto &settings = qsee::UserSettingsService::instance();
    return settings.isInitialized() ? ESP_OK : settings.initialize();
}

void publish_brightness(uint32_t percent, esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportDisplayBrightness(
        percent,
        error
    );
}

}  // namespace

extern "C" esp_err_t qsee_display_set_brightness_percent(uint32_t percent)
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

    auto &settings = qsee::UserSettingsService::instance();
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
    ESP_LOGI(kTag, "AMOLED brightness: %lu%%",
             static_cast<unsigned long>(percent));
    return ESP_OK;
}

extern "C" esp_err_t qsee_display_get_brightness_percent(uint32_t *percent)
{
    if (percent == nullptr) {
        ESP_LOGE(kTag, "Brightness read rejected: output is null");
        return ESP_ERR_INVALID_ARG;
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

extern "C" esp_err_t qsee_display_apply_saved_brightness(void)
{
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

    auto &settings = qsee::UserSettingsService::instance();
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
    if (display_error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Applying persisted brightness failed (%s)",
            esp_err_to_name(display_error)
        );
    }
    return display_error;
}
