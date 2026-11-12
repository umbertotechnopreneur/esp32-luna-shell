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

#include "CODY_settings.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_clock.hpp"
#include "LUNASHELL_codex_name.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_audio_controls.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
#include "LUNASHELL_tts.hpp"
#endif
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <mutex>

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_settings";
constexpr char kFirstBootKey[] = "cody_firstboot";
constexpr char kBirthIntroKey[] = "cody_birth";
constexpr char kFirstBootMusicKey[] = "cody_firstmusic";
constexpr char kOnboardingTourKey[] = "cody_tour_step";
constexpr char kSetupProgressKey[] = "cody_setup";
constexpr char kShakeKey[] = "cody_shake";
constexpr char kSoundsKey[] = "cody_sounds";
constexpr char kScreensaverKey[] = "cody_saver";
constexpr char kCounterModeKey[] = "cody_counter";
constexpr char kWeeklyAlarmKey[] = "cody_walarm";
constexpr char kIdlePolicyKey[] = "cody_idle";
constexpr char kEventCodyKey[] = "cody_evtmuse";
constexpr char kParallaxKey[] = "cody_parallax";
constexpr char kCharacterThemeKey[] = "cody_theme";
constexpr uint16_t kIdleDurations[] = {15, 30, 60, 120, 300};
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
constexpr char kSofiaKey[] = "cody_sofia";
#endif
constexpr char kQuotaRingKey[] = "cody_qring";
constexpr char kBatteryRingKey[] = "cody_bring";
constexpr char kPlaybackVolumeKey[] = "cody_outvol";
constexpr char kMicrophoneGainKey[] = "cody_ingain";
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
constexpr char kSynthVoiceKey[] = "cody_synth";
#endif
constexpr char kUserNameKey[] = "cody_user_name";
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
constexpr char kDictationKey[] = "cody_dictation";
#endif
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
constexpr bool kDefaultShake = false;
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
// value: Previously validated value, persisted before any runtime policy changes.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t save_preference_uint32(const char *key, uint32_t value)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = store.setUInt32(key, value);
    uint32_t stored = ~value;
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

// storage_error: Owner initialization result; absent data alone receives a default.
// Exception: Returns the original read, validation or publication error.
esp_err_t load_idle_policy(esp_err_t storage_error)
{
    uint32_t stored = 0;
    auto &store = LUNASHELL::UserSettingsService::instance();
    auto error = storage_error == ESP_OK ? store.getUInt32(kIdlePolicyKey, &stored) : storage_error;
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        error = save_preference_uint32(kIdlePolicyKey, 4U); // Counters, 60 seconds.
        if (error == ESP_OK) stored = 4U;
    }
    if (error == ESP_OK && (stored >> 1U) >= std::size(kIdleDurations)) error = ESP_ERR_INVALID_RESPONSE;
    LUNASHELL::CodyIdleObservation observation{};
    observation.last_error = error;
    observation.valid = error == ESP_OK;
    if (error == ESP_OK) {
        observation.show_muse = (stored & 1U) != 0;
        observation.delay_seconds = kIdleDurations[stored >> 1U];
    } else {
        ESP_LOGE(kTag, "Idle preference read failed (%s)", esp_err_to_name(error));
    }
    const auto publication = LUNASHELL::DeviceSnapshotService::instance().reportCodyIdleObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Idle preference publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// preference: Idle display or idle delay control.
// value: Exact mode 0/1 or one of the five supported durations in seconds.
// Exception: Returns the original projection, validation, write or publication error.
esp_err_t persist_idle_policy(PreferenceId preference, uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::CodyIdleObservation observation{};
    auto error = snapshots.readCodyIdleObservation(observation);
    if (error != ESP_OK) return error;
    if (!observation.valid || observation.last_error != ESP_OK)
        return observation.last_error != ESP_OK ? observation.last_error : ESP_ERR_INVALID_STATE;
    if (preference == PreferenceId::IdleMode) {
        if (value > 1U) return ESP_ERR_INVALID_ARG;
        observation.show_muse = value != 0;
    } else if (preference == PreferenceId::IdleDelay) {
        bool supported = false;
        for (const auto seconds : kIdleDurations) supported |= value == seconds;
        if (!supported) return ESP_ERR_INVALID_ARG;
        observation.delay_seconds = static_cast<uint16_t>(value);
    } else return ESP_ERR_INVALID_ARG;
    uint32_t index = 0;
    while (index < std::size(kIdleDurations) && kIdleDurations[index] != observation.delay_seconds) ++index;
    if (index == std::size(kIdleDurations)) return ESP_ERR_INVALID_RESPONSE;
    error = save_preference_uint32(kIdlePolicyKey, (index << 1U) | (observation.show_muse ? 1U : 0U));
    observation.valid = error == ESP_OK;
    observation.last_error = error;
    const auto publication = snapshots.reportCodyIdleObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Idle preference publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// storage_error: Settings store initialization result.
// Exception: Returns the original read or snapshot publication error.
esp_err_t load_event_cody(esp_err_t storage_error)
{
    LUNASHELL::CodyEventVisualObservation observation{};
    observation.last_error = storage_error == ESP_OK
        ? load_switch(kEventCodyKey, false, observation.enabled) : storage_error;
    observation.valid = observation.last_error == ESP_OK;
    const auto publication = LUNASHELL::DeviceSnapshotService::instance().reportCodyEventVisualObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Event Cody publication failed (%s)", esp_err_to_name(publication));
    return observation.last_error != ESP_OK ? observation.last_error : publication;
}

// value: Explicit zero/one event Cody preference.
// Exception: Returns the original storage or snapshot publication error.
esp_err_t persist_event_cody(uint32_t value)
{
    if (value > 1U) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::CodyEventVisualObservation observation{};
    const auto read_error = snapshots.readCodyEventVisualObservation(observation);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kEventCodyKey, value);
    observation.valid = error == ESP_OK;
    observation.last_error = error;
    if (error == ESP_OK) observation.enabled = value != 0;
    const auto publication = snapshots.reportCodyEventVisualObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Event Cody publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// storage_error: Settings store initialization result.
// Exception: Returns the original read, validation or publication error.
esp_err_t load_parallax_policy(esp_err_t storage_error)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    uint32_t stored = 0;
    auto error = storage_error == ESP_OK ? store.getUInt32(kParallaxKey, &stored) : storage_error;
    if (error == ESP_ERR_NVS_NOT_FOUND) error = save_preference_uint32(kParallaxKey, 0U);
    if (error == ESP_OK && stored > 3U) error = ESP_ERR_INVALID_RESPONSE;
    LUNASHELL::CodyParallaxObservation observation{};
    observation.last_error = error;
    observation.valid = error == ESP_OK;
    if (error == ESP_OK) observation.level = static_cast<uint8_t>(stored);
    else ESP_LOGE(kTag, "Parallax preference read failed (%s)", esp_err_to_name(error));
    const auto publication = LUNASHELL::DeviceSnapshotService::instance().reportCodyParallaxObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Parallax preference publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// value: Explicit Off/Low/Medium/High level, represented by 0..3.
// Exception: Returns the original readback or publication error.
esp_err_t persist_parallax_policy(uint32_t value)
{
    if (value > 3U) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::CodyParallaxObservation observation{};
    const auto read_error = snapshots.readCodyParallaxObservation(observation);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kParallaxKey, value);
    observation.valid = error == ESP_OK;
    observation.last_error = error;
    if (error == ESP_OK) observation.level = static_cast<uint8_t>(value);
    const auto publication = snapshots.reportCodyParallaxObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Parallax preference publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// storage_error: Settings store initialization result.
// Exception: Returns the original read, validation or publication error.
esp_err_t load_character_theme(esp_err_t storage_error)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    uint32_t stored = 0;
    auto error = storage_error == ESP_OK ? store.getUInt32(kCharacterThemeKey, &stored) : storage_error;
    if (error == ESP_ERR_NVS_NOT_FOUND) error = save_preference_uint32(kCharacterThemeKey, 0U);
    if (error == ESP_OK && stored >= 8U) error = ESP_ERR_INVALID_RESPONSE;
    LUNASHELL::CodyCharacterThemeObservation observation{};
    observation.last_error = error;
    observation.valid = error == ESP_OK;
    if (error == ESP_OK) observation.theme = static_cast<uint8_t>(stored);
    else ESP_LOGE(kTag, "Character theme read failed (%s)", esp_err_to_name(error));
    const auto publication = LUNASHELL::DeviceSnapshotService::instance().reportCodyCharacterThemeObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Character theme publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// value: Explicit theme identity 0..7.
// Exception: Returns the original readback or publication error.
esp_err_t persist_character_theme(uint32_t value)
{
    if (value >= 8U) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    LUNASHELL::CodyCharacterThemeObservation observation{};
    const auto read_error = snapshots.readCodyCharacterThemeObservation(observation);
    if (read_error != ESP_OK) return read_error;
    if (observation.valid && observation.last_error == ESP_OK && observation.theme == value) return ESP_OK;
    const auto error = save_preference_uint32(kCharacterThemeKey, value);
    observation.valid = error == ESP_OK;
    observation.last_error = error;
    if (error == ESP_OK) observation.theme = static_cast<uint8_t>(value);
    const auto publication = snapshots.reportCodyCharacterThemeObservation(observation);
    if (publication != ESP_OK) ESP_LOGE(kTag, "Character theme publication failed (%s)", esp_err_to_name(publication));
    return error != ESP_OK ? error : publication;
}

// preference: One of the two stable audio settings identities.
// value: Requested NVS value before narrowing or codec application.
bool valid_audio_preference(PreferenceId preference, uint32_t value)
{
    return preference == PreferenceId::PlaybackVolume ? value <= 100U :
        preference == PreferenceId::MicrophoneGain && value <= 30U && value % 3U == 0;
}

// preference: Playback volume or shared microphone gain.
// value: Receives a strictly validated NVS value, including verified first-boot default.
// Exception: Returns the original storage/readback error or invalid-record error.
esp_err_t load_audio_preference(PreferenceId preference, uint32_t &value)
{
    const bool output = preference == PreferenceId::PlaybackVolume;
    if (!output && preference != PreferenceId::MicrophoneGain) return ESP_ERR_INVALID_ARG;
    const char *key = output ? kPlaybackVolumeKey : kMicrophoneGainKey;
    const uint32_t default_value = output ? 90U : 24U;
    auto &store = LUNASHELL::UserSettingsService::instance();
    uint32_t stored = 0;
    auto error = store.getUInt32(key, &stored);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "First boot: key '%s' absent; persisting default=%lu", key,
            static_cast<unsigned long>(default_value));
        error = save_preference_uint32(key, default_value);
        if (error == ESP_OK) stored = default_value;
    }
    if (error == ESP_OK && !valid_audio_preference(preference, stored)) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Audio preference '%s' read rejected (%s)", key, esp_err_to_name(error));
        return error;
    }
    value = stored;
    return ESP_OK;
}

// preference: Validated audio setting identity.
// requested: Validated value from NVS or an explicit user selection.
// applied: Receives the value accepted and reported by the driver, not a physical register measurement.
// Exception: Returns original driver/readback failure or a mismatched reported result.
esp_err_t apply_audio_preference(PreferenceId preference, uint32_t requested, uint8_t &applied)
{
    if (!valid_audio_preference(preference, requested)) return ESP_ERR_INVALID_ARG;
    uint8_t confirmed = 0;
    const auto error = preference == PreferenceId::PlaybackVolume ?
        LUNASHELL_audio_set_user_output_volume(static_cast<uint8_t>(requested), &confirmed) :
        LUNASHELL_audio_set_user_input_gain(static_cast<uint8_t>(requested), &confirmed);
    if (error != ESP_OK) return error;
    if (confirmed != requested) {
        ESP_LOGE(kTag, "Audio preference %u driver readback mismatch: requested=%lu reported=%u",
            static_cast<unsigned>(preference), static_cast<unsigned long>(requested), confirmed);
        return ESP_ERR_INVALID_RESPONSE;
    }
    applied = confirmed;
    return ESP_OK;
}

// preferences: Receives the independent persisted setting status.
// previous: Last canonical projection when a valid prior projection was obtained, otherwise null.
// preference: One stable audio setting identity.
// storage_error: Original settings-store initialization result.
void load_audio_preference_pending(LUNASHELL::CodyPreferencesSnapshot &preferences,
    const LUNASHELL::CodyPreferencesSnapshot *previous, PreferenceId preference, esp_err_t storage_error)
{
    const bool output = preference == PreferenceId::PlaybackVolume;
    auto &value = output ? preferences.playback_volume_percent : preferences.microphone_gain_db;
    auto &valid = output ? preferences.playback_volume_valid : preferences.microphone_gain_valid;
    auto &error = output ? preferences.playback_volume_error : preferences.microphone_gain_error;
    uint32_t stored = 0;
    error = storage_error == ESP_OK ? load_audio_preference(preference, stored) : storage_error;
    valid = false;
    if (error != ESP_OK) return;
    value = static_cast<uint8_t>(stored);
    error = ESP_ERR_NOT_FINISHED;
    if (previous != nullptr) {
        const auto old_value = output ? previous->playback_volume_percent : previous->microphone_gain_db;
        const auto old_valid = output ? previous->playback_volume_valid : previous->microphone_gain_valid;
        const auto old_error = output ? previous->playback_volume_error : previous->microphone_gain_error;
        if (old_value == value) {
            valid = old_valid;
            error = old_error;
        }
    }
}

// preference: One canonical audio setting.
// value: Exact setting chosen by the user, validated by the dispatcher.
// Exception: Preserves the original snapshot, persistence, codec or publication error.
esp_err_t persist_audio_preference(PreferenceId preference, uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    auto error = snapshots.readCodyOptionsObservation(observed);
    if (error != ESP_OK) return error;
    const bool output = preference == PreferenceId::PlaybackVolume;
    auto &prefs = observed.preferences;
    auto &valid = output ? prefs.playback_volume_valid : prefs.microphone_gain_valid;
    auto &reported_error = output ? prefs.playback_volume_error : prefs.microphone_gain_error;
    auto &reported_value = output ? prefs.playback_volume_percent : prefs.microphone_gain_db;
    error = save_preference_uint32(output ? kPlaybackVolumeKey : kMicrophoneGainKey, value);
    uint8_t applied = 0;
    if (error == ESP_OK) error = apply_audio_preference(preference, value, applied);
    valid = error == ESP_OK;
    reported_error = error;
    if (error == ESP_OK) reported_value = applied;
    else ESP_LOGE(kTag, "Audio preference %u save/apply failed (%s)",
        static_cast<unsigned>(preference), esp_err_to_name(error));
    const auto publication_error = snapshots.reportCodyPreferences(prefs);
    if (publication_error != ESP_OK) ESP_LOGE(kTag, "Audio preference publication failed (%s)", esp_err_to_name(publication_error));
    if (output) LUNASHELL_audio_update_cody_output_policy(error != ESP_OK ? error : publication_error);
    return error != ESP_OK ? error : publication_error;
}

// value: Explicit zero/one choice; the dispatcher rejects other values.
// Exception: Preserves real snapshot, persistence/readback and publication failures.
esp_err_t persist_screensaver(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto read_error = snapshots.readCodyOptionsObservation(observed);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kScreensaverKey, value);
    observed.preferences.screensaver_valid = error == ESP_OK;
    observed.preferences.screensaver_error = error;
    if (error == ESP_OK) observed.preferences.screensaver_enabled = value == 1U;
    const auto publication_error = snapshots.reportCodyPreferences(observed.preferences);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Screensaver preference publication failed (%s)", esp_err_to_name(publication_error));
    }
    return error != ESP_OK ? error : publication_error;
}

// value: Explicit zero/one counter-mode choice.
// Exception: Returns the original snapshot, storage or publication error.
esp_err_t persist_counter_mode(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto read_error = snapshots.readCodyOptionsObservation(observed);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kCounterModeKey, value);
    observed.preferences.counter_mode_valid = error == ESP_OK;
    observed.preferences.counter_mode_error = error;
    if (error == ESP_OK) observed.preferences.counter_mode_enabled = value == 1U;
    const auto publication_error = snapshots.reportCodyPreferences(observed.preferences);
    if (publication_error != ESP_OK) ESP_LOGE(kTag, "Counter mode publication failed (%s)", esp_err_to_name(publication_error));
    return error != ESP_OK ? error : publication_error;
}

// value: Explicit zero/one weekly usage alarm choice.
// Exception: Returns the original snapshot, storage or publication error.
esp_err_t persist_weekly_alarm(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto read_error = snapshots.readCodyOptionsObservation(observed);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kWeeklyAlarmKey, value);
    observed.preferences.weekly_alarm_valid = error == ESP_OK;
    observed.preferences.weekly_alarm_error = error;
    if (error == ESP_OK) observed.preferences.weekly_alarm_enabled = value == 1U;
    const auto publication_error = snapshots.reportCodyPreferences(observed.preferences);
    if (publication_error != ESP_OK) ESP_LOGE(kTag, "Weekly alarm publication failed (%s)", esp_err_to_name(publication_error));
    return error != ESP_OK ? error : publication_error;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
// value: Explicit zero/one synth-voice preference.
// Exception: Returns the original snapshot, persistence or publication failure.
esp_err_t persist_synth_voice(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto read_error = snapshots.readCodyOptionsObservation(observed);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kSynthVoiceKey, value);
    if (error == ESP_OK && value == 0U) LUNASHELL::cancel_codex_speech();
    observed.preferences.synth_voice_valid = error == ESP_OK;
    observed.preferences.synth_voice_error = error;
    if (error == ESP_OK) observed.preferences.synth_voice_enabled = value == 1U;
    const auto publication_error = snapshots.reportCodyPreferences(observed.preferences);
    if (publication_error != ESP_OK) ESP_LOGE(kTag, "Synth voice preference publication failed (%s)", esp_err_to_name(publication_error));
    const auto clear_error = error == ESP_OK && value == 0U ? LUNASHELL_codex_clear_reply_speech() : ESP_OK;
    if (clear_error != ESP_OK) ESP_LOGE(kTag, "Synth voice reply cleanup failed (%s)", esp_err_to_name(clear_error));
    return error != ESP_OK ? error : publication_error != ESP_OK ? publication_error : clear_error;
}
#endif

// preference: Exact perimeter setting selected by the user.
// value: Validated zero/one value to save and read back.
// Exception: Returns the original projection, storage or publication error.
esp_err_t persist_ring_preference(PreferenceId preference, uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    auto error = snapshots.readCodyOptionsObservation(observed);
    if (error != ESP_OK) return error;
    auto &preferences = observed.preferences;
    const bool quota = preference == PreferenceId::AssistantQuotaRing;
    error = save_preference_uint32(quota ? kQuotaRingKey : kBatteryRingKey, value);
    if (quota) {
        preferences.assistant_quota_ring_valid = error == ESP_OK;
        preferences.assistant_quota_ring_error = error;
        if (error == ESP_OK) preferences.assistant_quota_ring_enabled = value == 1U;
    } else {
        preferences.battery_ring_valid = error == ESP_OK;
        preferences.battery_ring_error = error;
        if (error == ESP_OK) preferences.battery_ring_enabled = value == 1U;
    }
    const auto publication_error = snapshots.reportCodyPreferences(preferences);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Ring preference publication failed (%s)", esp_err_to_name(publication_error));
    }
    return error != ESP_OK ? error : publication_error;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// value: Exact 0/1 choice validated by the preference dispatcher.
// Exception: Returns original snapshot, write, readback or publication errors.
esp_err_t persist_dictation(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observation;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const esp_err_t projection_error = snapshots.readCodyOptionsObservation(observation);
    if (projection_error != ESP_OK) {
        ESP_LOGE(kTag, "Dictation preference projection failed (%s)", esp_err_to_name(projection_error));
        return projection_error;
    }
    const esp_err_t error = save_preference_uint32(kDictationKey, value);
    auto &preferences = observation.preferences;
    preferences.dictation_valid = error == ESP_OK;
    preferences.dictation_error = error;
    if (error == ESP_OK) {
        preferences.dictation_enabled = value == 1U;
    }
    const esp_err_t publication_error = snapshots.reportCodyPreferences(preferences);
    if (publication_error != ESP_OK) {
        ESP_LOGE(kTag, "Dictation preference publication failed (%s)", esp_err_to_name(publication_error));
    }
    return error != ESP_OK ? error : publication_error;
}
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// value: Explicit zero/one choice for wake-word activation.
// Exception: Returns the original snapshot, storage or publication failure.
esp_err_t persist_sofia(uint32_t value)
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    LUNASHELL::CodyOptionsObservation observation{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto read_error = snapshots.readCodyOptionsObservation(observation);
    if (read_error != ESP_OK) return read_error;
    const auto error = save_preference_uint32(kSofiaKey, value);
    observation.preferences.sofia_valid = error == ESP_OK;
    observation.preferences.sofia_error = error;
    if (error == ESP_OK) observation.preferences.sofia_enabled = value == 1U;
    const auto publication_error = snapshots.reportCodyPreferences(observation.preferences);
    if (publication_error != ESP_OK)
        ESP_LOGE(kTag, "Sofia preference publication failed (%s)", esp_err_to_name(publication_error));
    return error != ESP_OK ? error : publication_error;
}
#endif
}

// No parameters: exact persisted settings are applied before Cody/audio workers start.
// Exception: Returns the primary storage/policy error; logs any independent publication failure.
static esp_err_t load_companion_preferences_locked()
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    const esp_err_t storage_error = store.isInitialized() ? ESP_OK : store.initialize();
    (void)load_idle_policy(storage_error);
    (void)load_event_cody(storage_error);
    (void)load_parallax_policy(storage_error);
    (void)load_character_theme(storage_error);
    esp_err_t error = storage_error;
    LUNASHELL::CodyPreferencesSnapshot preferences;
    preferences.last_attempt_at_us = esp_timer_get_time();
    preferences.screensaver_error = storage_error == ESP_OK
        ? load_switch(kScreensaverKey, false, preferences.screensaver_enabled) : storage_error;
    preferences.screensaver_valid = preferences.screensaver_error == ESP_OK;
    preferences.counter_mode_error = storage_error == ESP_OK
        ? load_switch(kCounterModeKey, true, preferences.counter_mode_enabled) : storage_error;
    preferences.counter_mode_valid = preferences.counter_mode_error == ESP_OK;
    preferences.weekly_alarm_error = storage_error == ESP_OK
        ? load_switch(kWeeklyAlarmKey, false, preferences.weekly_alarm_enabled) : storage_error;
    preferences.weekly_alarm_valid = preferences.weekly_alarm_error == ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    preferences.sofia_error = storage_error == ESP_OK
        ? load_switch(kSofiaKey, true, preferences.sofia_enabled) : storage_error;
    preferences.sofia_valid = preferences.sofia_error == ESP_OK;
#else
    preferences.sofia_error = ESP_ERR_NOT_SUPPORTED;
#endif
    if (!preferences.counter_mode_valid)
        ESP_LOGE(kTag, "Optional counter mode unavailable (%s)", esp_err_to_name(preferences.counter_mode_error));
    preferences.assistant_quota_ring_error = storage_error == ESP_OK
        ? load_switch(kQuotaRingKey, true, preferences.assistant_quota_ring_enabled) : storage_error;
    preferences.assistant_quota_ring_valid = preferences.assistant_quota_ring_error == ESP_OK;
    preferences.battery_ring_error = storage_error == ESP_OK
        ? load_switch(kBatteryRingKey, true, preferences.battery_ring_enabled) : storage_error;
    preferences.battery_ring_valid = preferences.battery_ring_error == ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    preferences.auto_time_error = storage_error == ESP_OK
        ? load_switch(LUNASHELL::kAutoTimePreferenceKey, true, preferences.auto_time_enabled) : storage_error;
    preferences.auto_time_valid = preferences.auto_time_error == ESP_OK;
#else
    preferences.auto_time_error = ESP_ERR_NOT_SUPPORTED;
#endif
    if (!preferences.assistant_quota_ring_valid || !preferences.battery_ring_valid) {
        ESP_LOGE(kTag, "Optional ring preferences unavailable: quota=%s battery=%s",
            esp_err_to_name(preferences.assistant_quota_ring_error), esp_err_to_name(preferences.battery_ring_error));
    }
    if (!preferences.screensaver_valid) {
        ESP_LOGE(kTag, "Optional screensaver preference unavailable (%s)",
            esp_err_to_name(preferences.screensaver_error));
    }
    LUNASHELL::CodyOptionsObservation previous;
    const esp_err_t projection_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(previous);
    if (projection_error == ESP_OK) {
        // A failed sounds/wake reload preserves independent profile and mode
        // observations when their storage reads have not been attempted.
        preferences.user_name = previous.preferences.user_name;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        preferences.dictation_enabled = previous.preferences.dictation_enabled;
#endif
    } else {
        preferences.user_name.last_error = projection_error;
        preferences.user_name.health = LUNASHELL::DeviceHealth::Degraded;
        ESP_LOGE(kTag, "Optional preferred-name prior projection unavailable (%s)", esp_err_to_name(projection_error));
    }
    const auto *previous_audio = projection_error == ESP_OK && previous.preferences.last_attempt_at_us > 0
        ? &previous.preferences : nullptr;
    load_audio_preference_pending(preferences, previous_audio, PreferenceId::PlaybackVolume, storage_error);
    load_audio_preference_pending(preferences, previous_audio, PreferenceId::MicrophoneGain, storage_error);
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
    preferences.synth_voice_error = storage_error == ESP_OK
        ? load_switch(kSynthVoiceKey, false, preferences.synth_voice_enabled) : storage_error;
    preferences.synth_voice_valid = preferences.synth_voice_error == ESP_OK;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    // An optional preference failure cannot stop the companion or admit capture.
    preferences.dictation_error = storage_error == ESP_OK
        ? load_switch(kDictationKey, true, preferences.dictation_enabled) : storage_error;
    preferences.dictation_valid = preferences.dictation_error == ESP_OK;
#else
    preferences.dictation_error = ESP_ERR_NOT_SUPPORTED;
#endif
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

// output_only: True restores just playback before startup sound admission.
// Exception: Returns the first NVS, driver/readback or canonical publication error.
static esp_err_t reconcile_audio_preferences_locked(bool output_only)
{
    LUNASHELL::CodyOptionsObservation observed{};
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const auto projection_error = snapshots.readCodyOptionsObservation(observed);
    if (projection_error != ESP_OK) {
        LUNASHELL_audio_update_cody_output_policy(projection_error);
        return projection_error;
    }
    esp_err_t first_error = ESP_OK;
    for (const auto preference : {PreferenceId::PlaybackVolume, PreferenceId::MicrophoneGain}) {
        const bool output = preference == PreferenceId::PlaybackVolume;
        if (output_only && !output) continue;
        auto &prefs = observed.preferences;
        auto &valid = output ? prefs.playback_volume_valid : prefs.microphone_gain_valid;
        auto &reported_error = output ? prefs.playback_volume_error : prefs.microphone_gain_error;
        auto &reported_value = output ? prefs.playback_volume_percent : prefs.microphone_gain_db;
        uint32_t stored = 0;
        auto error = load_audio_preference(preference, stored);
        uint8_t reported = 0;
        if (error == ESP_OK) error = apply_audio_preference(preference, stored, reported);
        valid = error == ESP_OK;
        reported_error = error;
        if (valid) reported_value = reported;
        else ESP_LOGW(kTag, "Audio preference %u reconciliation failed (%s)",
            static_cast<unsigned>(preference), esp_err_to_name(error));
        if (first_error == ESP_OK && error != ESP_OK) first_error = error;
    }
    const auto publication_error = snapshots.reportCodyPreferences(observed.preferences);
    if (publication_error != ESP_OK) ESP_LOGE(kTag, "Audio reconciliation publication failed (%s)", esp_err_to_name(publication_error));
    const auto output_error = observed.preferences.playback_volume_error;
    LUNASHELL_audio_update_cody_output_policy(output_error != ESP_OK ? output_error : publication_error);
    return first_error != ESP_OK ? first_error : publication_error;
}

// No parameters: reapply both stored preferences through the existing codec owner on a worker.
// Exception: Returns the first NVS, driver/readback or canonical publication error.
esp_err_t reconcile_audio_preferences()
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    return reconcile_audio_preferences_locked(false);
}

// No parameters: restore saved playback policy on the startup task before its optional cue.
// Exception: Returns the original NVS, driver/readback or canonical publication error.
esp_err_t reconcile_audio_output_preference()
{
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    return reconcile_audio_preferences_locked(true);
}

// preference: Stable identity of the choice pressed on screen 10.
// value: Boolean/mode 0/1, manual angle 0/180, brightness percent or local quiet minute 0..1439.
// Exception: Returns the original validation, persistence, policy or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value)
{
    if (preference == PreferenceId::IdleMode || preference == PreferenceId::IdleDelay)
        return persist_idle_policy(preference, value);
    if (preference == PreferenceId::EventCody) return persist_event_cody(value);
    if (preference == PreferenceId::Parallax) return persist_parallax_policy(value);
    if (preference == PreferenceId::CharacterTheme) return persist_character_theme(value);
    if (preference == PreferenceId::SynthEnabled) {
        if (value > 1U) return ESP_ERR_INVALID_ARG;
#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
        return persist_synth_voice(value);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (preference == PreferenceId::PlaybackVolume || preference == PreferenceId::MicrophoneGain) {
        if (!valid_audio_preference(preference, value)) {
            ESP_LOGE(kTag, "Audio preference %u rejected value=%lu (%s)", static_cast<unsigned>(preference),
                static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        return persist_audio_preference(preference, value);
    }
    if (preference == PreferenceId::QuietEnabled || preference == PreferenceId::QuietStart ||
        preference == PreferenceId::QuietEnd) {
        const bool enabled_change = preference == PreferenceId::QuietEnabled;
        if (value > (enabled_change ? 1U : 1439U)) {
            ESP_LOGE(kTag, "Quiet preference %u rejected value=%lu (%s)",
                static_cast<unsigned>(preference), static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        LUNASHELL::ClockPreferences updated{};
        bool initialized = false;
        const int enabled = enabled_change ? static_cast<int>(value) : -1;
        const int start = preference == PreferenceId::QuietStart ? static_cast<int>(value) : -1;
        const int end = preference == PreferenceId::QuietEnd ? static_cast<int>(value) : -1;
        const auto error = LUNASHELL::apply_remote_clock_preferences(LUNASHELL::ClockRemoteOperation::Configure,
            LUNASHELL::kClockOffsetUnchanged, enabled, start, end, updated, initialized);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Quiet preference %u save failed (%s)",
                static_cast<unsigned>(preference), esp_err_to_name(error));
        }
        return error;
    }
    if (preference == PreferenceId::AutoTime) {
        if (value > 1U) {
            ESP_LOGE(kTag, "Auto time rejected value=%lu (%s)", static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
        std::lock_guard<std::mutex> guard(s_preferences_mutex);
        LUNASHELL::CodyOptionsObservation observation{};
        auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
        auto error = snapshots.readCodyOptionsObservation(observation);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Auto time projection failed (%s)", esp_err_to_name(error));
            return error;
        }
        error = save_preference_uint32(LUNASHELL::kAutoTimePreferenceKey, value);
        if (error == ESP_OK) error = LUNASHELL_time_apply_auto_sync(value == 1U);
        if (error != ESP_OK) ESP_LOGE(kTag, "Auto time apply failed (%s)", esp_err_to_name(error));
        observation.preferences.auto_time_valid = error == ESP_OK;
        observation.preferences.auto_time_error = error;
        if (error == ESP_OK) observation.preferences.auto_time_enabled = value == 1U;
        const auto publication_error = snapshots.reportCodyPreferences(observation.preferences);
        if (publication_error != ESP_OK) ESP_LOGE(kTag, "Auto time publication failed (%s)", esp_err_to_name(publication_error));
        return error != ESP_OK ? error : publication_error;
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (preference == PreferenceId::AssistantQuotaRing || preference == PreferenceId::BatteryRing) {
        if (value > 1U) {
            ESP_LOGE(kTag, "Ring preference rejected value=%lu (%s)",
                static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        return persist_ring_preference(preference, value);
    }
    if (preference == PreferenceId::CounterMode) {
        if (value > 1U) return ESP_ERR_INVALID_ARG;
        return persist_counter_mode(value);
    }
    if (preference == PreferenceId::WeeklyAlarm) {
        if (value > 1U) return ESP_ERR_INVALID_ARG;
        return persist_weekly_alarm(value);
    }
    if (preference == PreferenceId::Sofia) {
        if (value > 1U) return ESP_ERR_INVALID_ARG;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        return persist_sofia(value);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (preference == PreferenceId::Screensaver) {
        if (value > 1U) {
            ESP_LOGE(kTag, "Screensaver preference rejected value=%lu (%s)",
                static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        return persist_screensaver(value);
    }
    if (preference == PreferenceId::Brightness) {
        return LUNASHELL_display_set_brightness_percent(value);
    }
    if (preference == PreferenceId::AutoRotate && value <= 1U) {
        return LUNASHELL_display_set_auto_rotation_enabled(value == 1U);
    }
    if (preference == PreferenceId::Orientation && (value == 0U || value == 180U)) {
        return LUNASHELL_display_set_rotation_degrees(value);
    }
    if (preference == PreferenceId::Dictation) {
        if (value > 1U) {
            ESP_LOGE(kTag, "Dictation preference rejected value=%lu (%s)",
                static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        return persist_dictation(value);
#else
        ESP_LOGE(kTag, "Dictation preference unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    std::lock_guard<std::mutex> guard(s_preferences_mutex);
    if ((preference != PreferenceId::ShakeWake && preference != PreferenceId::Sounds) || value > 1U) {
        ESP_LOGE(kTag, "Preference %u rejected value=%lu (%s)", static_cast<unsigned>(preference),
            static_cast<unsigned long>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = ESP_ERR_INVALID_ARG;
    switch (preference) {
    case PreferenceId::ShakeWake:
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        error = save_preference_uint32(kShakeKey, value);
#else
        error = ESP_ERR_NOT_SUPPORTED;
#endif
        break;
    case PreferenceId::Sounds:
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        error = save_preference_uint32(kSoundsKey, value);
#else
        error = ESP_ERR_NOT_SUPPORTED;
#endif
        break;
    default:
        return ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        return load_companion_preferences_locked();
    }
    ESP_LOGE(kTag, "Preference %u save=%lu failed (%s)", static_cast<unsigned>(preference),
        static_cast<unsigned long>(value), esp_err_to_name(error));
    LUNASHELL::CodyOptionsObservation observation;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    const esp_err_t read_error = snapshots.readCodyOptionsObservation(observation);
    if (read_error == ESP_OK) {
        // A rejected write does not change either applied runtime gate.
        // Report the failed attempt while retaining their confirmed values.
        observation.preferences.last_attempt_at_us = esp_timer_get_time();
        observation.preferences.last_error = error;
        observation.preferences.health = LUNASHELL::DeviceHealth::Degraded;
        const esp_err_t publication_error = snapshots.reportCodyPreferences(observation.preferences);
        if (publication_error != ESP_OK) {
            ESP_LOGE(kTag, "Preference failure publication failed (%s)", esp_err_to_name(publication_error));
        }
    } else {
        ESP_LOGE(kTag, "Preference failure projection failed (%s)", esp_err_to_name(read_error));
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
    error = store.getBool(kBirthIntroKey, &loaded.birth_intro_pending);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        // Existing configured installations must not acquire a first-run intro
        // merely because a firmware update introduced this separate key.
        const bool default_pending = loaded.persisted_first_boot;
        ESP_LOGI(kTag, "Birth intro: key '%s' absent; persisting first-boot default=%u",
            kBirthIntroKey, default_pending);
        error = persist_birth_intro_pending(default_pending);
        if (error == ESP_OK) loaded.birth_intro_pending = default_pending;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Birth intro read/default persistence '%s' failed (%s)",
            kBirthIntroKey, esp_err_to_name(error));
        return error;
    }
    error = store.getBool(kFirstBootMusicKey, &loaded.first_boot_music_pending);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        const bool default_pending = loaded.persisted_first_boot && loaded.birth_intro_pending;
        ESP_LOGI(kTag, "First-boot music: key '%s' absent; first-boot default=%u",
            kFirstBootMusicKey, default_pending);
        error = persist_first_boot_music_pending(default_pending);
        if (error == ESP_OK) loaded.first_boot_music_pending = default_pending;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot music read/default '%s' failed (%s)",
            kFirstBootMusicKey, esp_err_to_name(error));
        return error;
    }
    uint32_t tour_step = 0;
    error = store.getUInt32(kOnboardingTourKey, &tour_step);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        const uint32_t default_step = loaded.persisted_first_boot ? 0U : 5U;
        ESP_LOGI(kTag, "Onboarding tour: key '%s' absent; first-boot default=%lu",
            kOnboardingTourKey, static_cast<unsigned long>(default_step));
        error = persist_onboarding_tour_step(default_step);
        if (error == ESP_OK) tour_step = default_step;
    }
    if (error == ESP_OK && tour_step > 6U) error = ESP_ERR_INVALID_ARG;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Onboarding tour read/default '%s' failed (%s)",
            kOnboardingTourKey, esp_err_to_name(error));
        return error;
    }
    loaded.onboarding_tour_step = static_cast<uint8_t>(tour_step);
    error = store.getBool(kSetupProgressKey, &loaded.setup_progress_pending);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Setup progress: key '%s' absent; persisting default=1", kSetupProgressKey);
        error = persist_setup_progress_pending(true);
        if (error == ESP_OK) loaded.setup_progress_pending = true;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Setup progress read/default persistence '%s' failed (%s)",
            kSetupProgressKey, esp_err_to_name(error));
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

// pending: Whether a fully retired introduction remains outstanding.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t persist_birth_intro_pending(bool pending)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    auto error = store.setBool(kBirthIntroKey, pending);
    bool stored = !pending;
    if (error == ESP_OK) error = store.getBool(kBirthIntroKey, &stored);
    if (error == ESP_OK && stored != pending) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Birth intro preference '%s'=%u failed (%s)",
            kBirthIntroKey, pending, esp_err_to_name(error));
    }
    return error;
}

// pending: Whether the one-time first-boot melody remains to be attempted.
// Exception: Returns the original storage error or a readback mismatch.
esp_err_t persist_first_boot_music_pending(bool pending)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    auto error = store.setBool(kFirstBootMusicKey, pending);
    bool stored = !pending;
    if (error == ESP_OK) error = store.getBool(kFirstBootMusicKey, &stored);
    if (error == ESP_OK && stored != pending) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "First-boot music preference '%s'=%u failed (%s)",
            kFirstBootMusicKey, pending, esp_err_to_name(error));
    }
    return error;
}

// step: Guide pages 0..4 or 6; 5 retains its existing completed/skipped meaning.
// Exception: Returns the original storage error or a readback mismatch.
esp_err_t persist_onboarding_tour_step(uint32_t step)
{
    if (step > 6U) {
        ESP_LOGE(kTag, "Onboarding tour step rejected: %lu", static_cast<unsigned long>(step));
        return ESP_ERR_INVALID_ARG;
    }
    auto &store = LUNASHELL::UserSettingsService::instance();
    auto error = store.setUInt32(kOnboardingTourKey, step);
    uint32_t stored = ~step;
    if (error == ESP_OK) error = store.getUInt32(kOnboardingTourKey, &stored);
    if (error == ESP_OK && stored != step) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Onboarding tour preference '%s'=%lu failed (%s)",
            kOnboardingTourKey, static_cast<unsigned long>(step), esp_err_to_name(error));
    }
    return error;
}

// pending: Whether setup still requires a confirmed Wi-Fi and Codex connection.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t persist_setup_progress_pending(bool pending)
{
    auto &store = LUNASHELL::UserSettingsService::instance();
    auto error = store.setBool(kSetupProgressKey, pending);
    bool stored = !pending;
    if (error == ESP_OK) error = store.getBool(kSetupProgressKey, &stored);
    if (error == ESP_OK && stored != pending) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Setup progress preference '%s'=%u failed (%s)",
            kSetupProgressKey, pending, esp_err_to_name(error));
    }
    return error;
}
} // namespace CODY
