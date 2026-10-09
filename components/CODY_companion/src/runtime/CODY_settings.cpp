#include "CODY_settings.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_settings";
constexpr char kFirstBootKey[] = "cody_firstboot";
constexpr char kShakeKey[] = "cody_shake";
constexpr char kSoundsKey[] = "cody_sounds";
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
constexpr bool kDefaultShake = true;
#else
constexpr bool kDefaultShake = false;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
constexpr bool kDefaultSounds = true;
#else
constexpr bool kDefaultSounds = false;
#endif

// key: Owner-specific boolean stored as an exact uint32 value, never coerced.
// default_value: Used only after the store explicitly reports an absent key.
// value: Assigned only after a successful strict read, including default readback.
// Exception: Returns the original storage error or ESP_ERR_INVALID_ARG for values other than 0/1.
esp_err_t load_switch(const char *key, bool default_value, bool &value)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    uint32_t stored = 0;
    esp_err_t error = store.getUInt32(key, &stored);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "First boot: key '%s' absent; persisting default=%u", key, default_value);
        error = store.setUInt32(key, default_value ? 1U : 0U);
        if (error == ESP_OK) {
            error = store.getUInt32(key, &stored);
        }
        if (error == ESP_OK && stored != static_cast<uint32_t>(default_value)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (error == ESP_OK && stored > 1U) {
        ESP_LOGE(kTag, "Preference '%s' rejected value=%lu", key, static_cast<unsigned long>(stored));
        error = ESP_ERR_INVALID_ARG;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Preference '%s' load/default failed (%s)", key, esp_err_to_name(error));
        return error;
    }
    value = stored == 1U;
    return ESP_OK;
}

// key: Exact owner-specific key selected from a typed preference.
// value: Previously validated 0/1, persisted before any runtime policy changes.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t save_switch(const char *key, uint32_t value)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.setUInt32(key, value);
    uint32_t stored = 2;
    if (error == ESP_OK) {
        error = store.getUInt32(key, &stored);
    }
    if (error == ESP_OK && stored != value) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Preference '%s' save/readback=%lu failed (%s)", key,
            static_cast<unsigned long>(value), esp_err_to_name(error));
    }
    return error;
}
}

// No parameters: exact persisted settings are applied before Cody/audio workers start.
// Exception: Returns the primary storage/policy error; logs any independent publication failure.
esp_err_t load_companion_preferences()
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.isInitialized() ? ESP_OK : store.initialize();
    LUNASHELL::CodyPreferencesSnapshot preferences;
    preferences.last_attempt_at_us = esp_timer_get_time();
    if (error == ESP_OK) {
        error = load_switch(kShakeKey, kDefaultShake, preferences.shake_to_wake);
    }
    if (error == ESP_OK) {
        error = load_switch(kSoundsKey, kDefaultSounds, preferences.sounds_enabled);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    if (error == ESP_OK) {
        error = LUNASHELL_power_manager_set_shake_wake_enabled(preferences.shake_to_wake);
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (error == ESP_OK) {
        error = LUNASHELL_audio_set_user_sounds_enabled(preferences.sounds_enabled);
    }
#endif
    preferences.last_error = error;
    preferences.data_valid = error == ESP_OK;
    preferences.health = error == ESP_OK ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Degraded;
    if (error == ESP_OK) {
        preferences.last_success_at_us = preferences.last_attempt_at_us;
    } else {
        ESP_LOGE(kTag, "Companion preferences load/apply failed (%s)", esp_err_to_name(error));
    }
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportCodyPreferences(preferences);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Companion preferences publication failed (%s)", esp_err_to_name(publication_error));
    }
    return error != ESP_OK ? error : publication_error;
}

// preference: Stable identity of the choice pressed on screen 10.
// value: Boolean 0/1 or a manual half-turn 0/180; invalid input is never clamped.
// Exception: Returns the original validation, persistence, policy or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value)
{
    const bool valid = preference == PreferenceId::Orientation ? value == 0U || value == 180U : value <= 1U;
    if (!valid) {
        ESP_LOGE(kTag, "Preference %u rejected value=%lu (%s)", static_cast<unsigned>(preference),
            static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = ESP_ERR_INVALID_ARG;
    switch (preference) {
    case PreferenceId::ShakeWake:
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        error = save_switch(kShakeKey, value);
#else
        error = ESP_ERR_NOT_SUPPORTED;
#endif
        break;
    case PreferenceId::Sounds:
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        error = save_switch(kSoundsKey, value);
#else
        error = ESP_ERR_NOT_SUPPORTED;
#endif
        break;
    case PreferenceId::AutoRotate:
        error = LUNASHELL_display_set_auto_rotation_enabled(value == 1U);
        break;
    case PreferenceId::Orientation:
        error = LUNASHELL_display_set_rotation_degrees(value);
        break;
    }
    if (error == ESP_OK) {
        // Re-read both keys before publishing values, also recovering a prior failed explicit save.
        error = load_companion_preferences();
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Preference %u save/apply=%lu failed (%s)", static_cast<unsigned>(preference),
            static_cast<unsigned long>(value), esp_err_to_name(error));
        LUNASHELL::CodyOptionsObservation observation;
        const esp_err_t read_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(observation);
        if (read_error == ESP_OK) {
            observation.preferences.last_attempt_at_us = esp_timer_get_time();
            observation.preferences.last_error = error;
            observation.preferences.data_valid = false;
            observation.preferences.health = LUNASHELL::DeviceHealth::Degraded;
            const esp_err_t publication_error =
                LUNASHELL::DeviceSnapshotService::instance().reportCodyPreferences(observation.preferences);
            if (publication_error != ESP_OK) {
                ESP_LOGE(kTag, "Preference failure publication failed (%s)", esp_err_to_name(publication_error));
            }
        } else {
            ESP_LOGE(kTag, "Preference failure projection failed (%s)", esp_err_to_name(read_error));
        }
    }
    return error;
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
// Exception: Returns the original write/readback error or ESP_ERR_INVALID_RESPONSE for a mismatched value.
esp_err_t persist_first_boot(bool first_boot)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.setBool(kFirstBootKey, first_boot);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot preference write '%s'=%u failed (%s)",
            kFirstBootKey, first_boot, esp_err_to_name(error));
        return error;
    }
    bool persisted = !first_boot;
    error = store.getBool(kFirstBootKey, &persisted);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot preference readback '%s' failed (%s)",
            kFirstBootKey, esp_err_to_name(error));
        return error;
    }
    if (persisted != first_boot) {
        ESP_LOGE(kTag, "First-boot preference readback mismatch: '%s' requested=%u stored=%u (%s)",
            kFirstBootKey, first_boot, persisted, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(kTag, "Explicit first-boot preference persisted: '%s'=%u", kFirstBootKey, first_boot);
    return ESP_OK;
}
} // namespace CODY
