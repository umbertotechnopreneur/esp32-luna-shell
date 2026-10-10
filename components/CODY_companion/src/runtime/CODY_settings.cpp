#include "CODY_settings.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "CODY_settings.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_codex_name.hpp"
#include "LUNASHELL_platform.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <mutex>

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_settings";
constexpr char kFirstBootKey[] = "cody_firstboot";
constexpr char kShakeKey[] = "cody_shake";
constexpr char kSoundsKey[] = "cody_sounds";
constexpr char kUserNameKey[] = "cody_user_name";
std::mutex s_preferences_mutex;

// name: Exact user-provided printable ASCII, including empty explicit clear.
// Exception: Returns invalid argument/size without trimming or transliterating input.
esp_err_t validate_user_name(const char *name)
{
    if (name == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen(name, LUNASHELL::kPreferredNameMaximum + 1U);
    if (length > LUNASHELL::kPreferredNameMaximum) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0; index < length; ++index) {
        if (name[index] < 32 || name[index] > 126) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (length != 0 && (name[0] == ' ' || name[length - 1U] == ' ')) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// profile: Receives a genuine strict read or a failure observation with original error.
// Exception: Returns real NVS/type/readback/validation errors; defaults only a genuinely absent string.
esp_err_t load_user_name(LUNASHELL::CodyUserNameSnapshot &profile)
{
    profile.last_attempt_at_us = esp_timer_get_time();
    auto &store = LUNASHELL::UserSettingsService::instance();
    std::string stored;
    esp_err_t error = store.getString(kUserNameKey, &stored);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "First boot: key '%s' absent; persisting empty preferred name", kUserNameKey);
        error = store.setString(kUserNameKey, "");
        if (error == ESP_OK) {
            error = store.getString(kUserNameKey, &stored);
        }
        if (error == ESP_OK && !stored.empty()) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (error == ESP_OK) {
        error = stored.size() <= LUNASHELL::kPreferredNameMaximum && stored.size() == std::strlen(stored.c_str())
            ? validate_user_name(stored.c_str()) : ESP_ERR_INVALID_SIZE;
    }
    profile.last_error = error;
    profile.data_valid = error == ESP_OK;
    profile.data_stale = error != ESP_OK && profile.last_success_at_us > 0;
    profile.health = error == ESP_OK ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Degraded;
    if (error == ESP_OK) {
        std::memcpy(profile.name, stored.c_str(), stored.size() + 1U);
        profile.last_success_at_us = profile.last_attempt_at_us;
    } else {
        ESP_LOGE(kTag, "Preferred-name load/default rejected: bytes=%u (%s)",
            static_cast<unsigned>(stored.size()), esp_err_to_name(error));
    }
    return error;
}

#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
// operation: Typed read or explicit user-requested name change admitted by authenticated CCP.
// name: Exact bounded ASCII or empty explicit clear; unused for read.
// result: Receives canonical published profile, with independent validity/error metadata.
// Exception: Returns original validation, persistence, readback or publication errors.
esp_err_t execute_user_name(LUNASHELL::CodexNameOperation operation, const char *name,
                            LUNASHELL::CodyUserNameSnapshot &result)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observation;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    esp_err_t error = snapshots.readCodyOptionsObservation(observation);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Preferred-name canonical projection failed (%s)", esp_err_to_name(error));
        return error;
    }
    if (operation == LUNASHELL::CodexNameOperation::Read) {
        result = observation.preferences.user_name;
        return result.data_valid && !result.data_stale && result.last_error == ESP_OK ? ESP_OK :
            result.last_error != ESP_OK ? result.last_error : ESP_ERR_INVALID_STATE;
    }
    if (operation != LUNASHELL::CodexNameOperation::Configure) {
        return ESP_ERR_INVALID_ARG;
    }
    error = validate_user_name(name);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Preferred-name input rejected (%s)", esp_err_to_name(error));
        return error;
    }
    auto profile = observation.preferences.user_name;
    profile.last_attempt_at_us = esp_timer_get_time();
    error = LUNASHELL::UserSettingsService::instance().setString(kUserNameKey, name);
    if (error == ESP_OK) {
        error = load_user_name(profile);
    }
    if (error == ESP_OK && std::strcmp(profile.name, name) != 0) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        profile.last_error = error;
        profile.data_valid = false;
        profile.data_stale = profile.last_success_at_us > 0;
        profile.health = LUNASHELL::DeviceHealth::Degraded;
        ESP_LOGE(kTag, "Preferred-name save/readback failed (%s)", esp_err_to_name(error));
    }
    ++profile.revision;
    observation.preferences.user_name = profile;
    const esp_err_t publication_error = snapshots.reportCodyPreferences(observation.preferences);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Preferred-name canonical publication failed (%s)", esp_err_to_name(publication_error));
    } else {
        result = profile;
    }
    return error != ESP_OK ? error : publication_error;
}
#endif
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
static esp_err_t load_companion_preferences_locked()
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.isInitialized() ? ESP_OK : store.initialize();
    LUNASHELL::CodyPreferencesSnapshot preferences;
    preferences.last_attempt_at_us = esp_timer_get_time();
    LUNASHELL::CodyOptionsObservation previous;
    const esp_err_t projection_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(previous);
    if (projection_error == ESP_OK) {
        // A failed sounds/wake reload does not erase the independent profile's
        // genuine last successful read or silently republish an empty name.
        preferences.user_name = previous.preferences.user_name;
    } else {
        preferences.user_name.last_error = projection_error;
        preferences.user_name.health = LUNASHELL::DeviceHealth::Degraded;
        ESP_LOGE(kTag, "Optional preferred-name prior projection unavailable (%s)", esp_err_to_name(projection_error));
    }
    if (error == ESP_OK) {
        error = load_switch(kShakeKey, kDefaultShake, preferences.shake_to_wake);
    }
    if (error == ESP_OK) {
        error = load_switch(kSoundsKey, kDefaultSounds, preferences.sounds_enabled);
    }
    if (error == ESP_OK) {
        if (projection_error == ESP_OK) {
            const esp_err_t profile_error = load_user_name(preferences.user_name);
            ++preferences.user_name.revision;
            if (profile_error != ESP_OK) {
                // Personalization is optional; its canonical component retains
                // the real failure while sounds and wake preferences remain usable.
                ESP_LOGE(kTag, "Optional preferred-name profile unavailable (%s)", esp_err_to_name(profile_error));
            }
        }
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

// No parameters: serializes complete preference reloads and registers the sole profile owner.
// Exception: Returns original boolean preference/owner publication or registration errors.
esp_err_t load_companion_preferences()
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    esp_err_t error = load_companion_preferences_locked();
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    if (error == ESP_OK) {
        error = LUNASHELL::registerCodexNameExecutor(execute_user_name);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Preferred-name remote owner registration failed (%s)", esp_err_to_name(error));
        }
    }
#endif
    return error;
}

// preference: Stable identity of the choice pressed on screen 10.
// value: Boolean 0/1 or a manual half-turn 0/180; invalid input is never clamped.
// Exception: Returns the original validation, persistence, policy or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
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
        error = load_companion_preferences_locked();
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
