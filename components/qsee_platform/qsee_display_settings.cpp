#include "qsee_platform.h"
#include "qsee_settings.hpp"

#include "bsp/display.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "qsee_display_settings";
constexpr char kBrightnessSetting[] = "brightness";

uint32_t clamp_brightness(uint32_t percent)
{
    return percent < 10U ? 10U : percent > 100U ? 100U : percent;
}

}  // namespace

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
    const esp_err_t persist_error =
        settings.setUInt32(kBrightnessSetting, percent);
    if (persist_error != ESP_OK) return persist_error;
    const esp_err_t display_error =
        bsp_display_brightness_set(static_cast<int>(percent));
    ESP_LOGI(kTag, "AMOLED brightness: %lu%%",
             static_cast<unsigned long>(percent));
    return display_error;
}

extern "C" uint32_t qsee_display_get_brightness_percent(void)
{
    const int brightness = bsp_display_brightness_get();
    return brightness < 0 ? 100U : clamp_brightness(static_cast<uint32_t>(brightness));
}

extern "C" esp_err_t qsee_display_apply_saved_brightness(void)
{
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) return init_error;
    }
    uint32_t brightness = 100;
    const esp_err_t read_error =
        settings.getUInt32(kBrightnessSetting, 100, &brightness);
    if (read_error != ESP_OK) return read_error;
    brightness = clamp_brightness(brightness);
    ESP_LOGI(kTag, "Applying saved AMOLED brightness: %lu%%",
             static_cast<unsigned long>(brightness));
    return bsp_display_brightness_set(static_cast<int>(brightness));
}
