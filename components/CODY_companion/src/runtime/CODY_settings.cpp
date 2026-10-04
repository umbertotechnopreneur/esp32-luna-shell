#include "CODY_settings.hpp"
#include "LUNASHELL_settings.hpp"
#include "sdkconfig.h"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_settings";
constexpr char kFirstBootKey[] = "cody_firstboot";
}

// settings: Receives persisted/default/explicit-development decisions on success only.
esp_err_t load_onboarding_settings(OnboardingSettings &settings)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.isInitialized() ? ESP_OK : store.initialize();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Settings initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    OnboardingSettings loaded{};
    error = store.getBool(kFirstBootKey, &loaded.persisted_first_boot);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "First boot: key '%s' absent; persisting default=1", kFirstBootKey);
        error = store.setBool(kFirstBootKey, true);
        if (error == ESP_OK) {
            loaded.persisted_first_boot = true;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot read/default persistence '%s' failed (%s)",
            kFirstBootKey, esp_err_to_name(error));
        return error;
    }
#if CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT
    loaded.force_first_boot = true;
#endif
    // The development override never repairs a failed read or overwrites a user's stored 0.
    loaded.effective_first_boot = loaded.persisted_first_boot || loaded.force_first_boot;
    ESP_LOGI(kTag, "First boot: key='%s' stored=%u force-development=%u effective=%u",
        kFirstBootKey, loaded.persisted_first_boot, loaded.force_first_boot, loaded.effective_first_boot);
    settings = loaded;
    return ESP_OK;
}

// first_boot: Explicit user preference; greeting completion never writes this key.
esp_err_t persist_first_boot(bool first_boot)
{
    const esp_err_t error = LUNASHELL::UserSettingsService::instance().setBool(kFirstBootKey, first_boot);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot preference write '%s'=%u failed (%s)",
            kFirstBootKey, first_boot, esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(kTag, "Explicit first-boot preference persisted: '%s'=%u", kFirstBootKey, first_boot);
    return ESP_OK;
}
} // namespace CODY
