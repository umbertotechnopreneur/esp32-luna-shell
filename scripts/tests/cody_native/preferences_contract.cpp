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
#include "LUNASHELL_clock.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {

constexpr esp_err_t kStorageFailure = 0x1110;
constexpr esp_err_t kTypeMismatch = 0x1103;

struct StoredValue {
    bool present = false;
    uint32_t value = 0;
    esp_err_t read_error = ESP_OK;
    esp_err_t write_error = ESP_OK;
    esp_err_t readback_error = ESP_OK;
    bool mismatch_readback = false;
    bool awaiting_readback = false;
    unsigned reads = 0;
    unsigned writes = 0;
};

struct Fixture {
    std::map<std::string, StoredValue> keys;
    bool initialized = false;
    esp_err_t initialize_error = ESP_OK;
    unsigned initialize_calls = 0;
    bool sounds_enabled = true;
    bool shake_enabled = true;
    unsigned sound_calls = 0;
    unsigned shake_calls = 0;
    esp_err_t sound_error = ESP_OK;
    esp_err_t shake_error = ESP_OK;
    unsigned display_calls = 0;
    esp_err_t display_error = ESP_OK;
    esp_err_t projection_error = ESP_OK;
    esp_err_t publication_error = ESP_OK;
    unsigned publication_calls = 0;
    LUNASHELL::CodyOptionsObservation observation;
    LUNASHELL::CodyIdleObservation idle;
    LUNASHELL::CodyEventVisualObservation event_visual;
    LUNASHELL::CodyParallaxObservation parallax;
    LUNASHELL::CodyCharacterThemeObservation character_theme;
    LUNASHELL::CodyPreferencesSnapshot attempted_publication;
    std::string preferred_name;
    int64_t time_us = 1'000'000;
};

Fixture fixture;

// condition: Required result from the actual firmware settings implementation.
// message: Focused failure explanation, never a claim about physical hardware.
// Exception: Exits the host executable with failure if the contract is violated.
void expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Preferences contract failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

// sounds: Persisted host value before firmware settings are loaded.
// shake: Persisted host value before firmware settings are loaded.
void reset_existing(uint32_t sounds = 1U, uint32_t shake = 1U)
{
    fixture = Fixture{};
    fixture.keys["cody_sounds"] = {true, sounds};
    fixture.keys["cody_shake"] = {true, shake};
}

// error: Expected original failure from storage, a runtime owner or publication.
// Exception: Exits if an unsuccessful operation is exposed as a current preference.
void expect_invalid(esp_err_t error)
{
    const auto &preferences = fixture.attempted_publication;
    expect(!preferences.data_valid, "failure must invalidate the published preferences");
    expect(preferences.health == LUNASHELL::DeviceHealth::Degraded, "failure must be degraded");
    expect(preferences.last_error == error, "original failure must be published unchanged");
    expect(preferences.last_attempt_at_us > 0, "failed attempt needs its own timestamp");
}

// error: Original failed save or readback result after a confirmed baseline.
// Exception: Exits if the failed mutation loses its cause or the applied settings.
void expect_failed_save(esp_err_t error)
{
    const auto &preferences = fixture.attempted_publication;
    expect(preferences.data_valid && preferences.sounds_enabled && preferences.shake_to_wake,
        "failed save must retain the previously confirmed runtime values");
    expect(preferences.health == LUNASHELL::DeviceHealth::Degraded,
        "failed save must publish degraded health");
    expect(preferences.last_error == error && preferences.last_attempt_at_us > 0,
        "failed save must publish its original error and attempt timestamp");
}

// No parameters: a persisted Off survives a new boot without rewriting either existing key.
void check_persistent_sounds_off()
{
    reset_existing(0U);
    expect(CODY::load_companion_preferences() == ESP_OK, "persisted Sounds Off must load");
    expect(!fixture.sounds_enabled && fixture.sound_calls == 1U, "Off must reach the audio owner");
    expect(fixture.observation.preferences.data_valid &&
        !fixture.observation.preferences.sounds_enabled, "canonical preference must contain confirmed Off");
    expect(fixture.keys["cody_sounds"].writes == 0U && fixture.keys["cody_shake"].writes == 0U,
        "existing keys must not be defaulted or rewritten");

    // A simulated new process retains the store, but resets its owner admission state.
    fixture.initialized = false;
    fixture.sounds_enabled = true;
    fixture.sound_calls = 0;
    expect(CODY::load_companion_preferences() == ESP_OK, "Off must reload after a simulated restart");
    expect(!fixture.sounds_enabled && fixture.sound_calls == 1U, "restart must reapply stored Off");
    expect(fixture.keys["cody_sounds"].writes == 0U, "restart must not overwrite the stored choice");
}

// No parameters: defaults require a genuinely missing key and an exact readback.
void check_absence_and_strict_reads()
{
    reset_existing();
    fixture.keys.clear();
    expect(CODY::load_companion_preferences() == ESP_OK, "absent keys may receive authored defaults");
    struct DefaultPreference {
        const char *key;
        uint32_t value;
    };
    for (const auto &preference : {DefaultPreference{"cody_shake", 0U},
                                   DefaultPreference{"cody_sounds", 1U}}) {
        const auto &stored = fixture.keys.at(preference.key);
        expect(stored.present && stored.value == preference.value && stored.writes == 1U && stored.reads == 2U,
            "default must be persisted once and read back exactly");
    }
    expect(!fixture.shake_enabled && fixture.sounds_enabled,
        "default shake Off and sounds On must reach their runtime owners");

    for (const esp_err_t error : {kStorageFailure, kTypeMismatch}) {
        reset_existing();
        fixture.keys["cody_sounds"].read_error = error;
        expect(CODY::load_companion_preferences() == error, "read failure must remain distinct from absence");
        expect(fixture.keys["cody_sounds"].writes == 0U && fixture.sound_calls == 0U,
            "read failure must not default, overwrite or apply a guessed value");
        expect_invalid(error);
    }

    reset_existing(2U);
    expect(CODY::load_companion_preferences() == ESP_ERR_INVALID_ARG, "stored value 2 must not become true");
    expect(fixture.keys["cody_sounds"].value == 2U && fixture.keys["cody_sounds"].writes == 0U,
        "non-boolean storage must be preserved for explicit repair");
    expect(fixture.sound_calls == 0U, "invalid stored boolean must not reach the audio owner");
    expect_invalid(ESP_ERR_INVALID_ARG);

    reset_existing();
    auto &absent = fixture.keys["cody_sounds"];
    absent.present = false;
    absent.mismatch_readback = true;
    expect(CODY::load_companion_preferences() == ESP_ERR_INVALID_RESPONSE, "default mismatch must fail");
    expect(absent.writes == 1U && fixture.sound_calls == 0U, "failed default readback must not be applied");
    expect_invalid(ESP_ERR_INVALID_RESPONSE);
}

// No parameters: save errors retain the previous runtime gate until an explicit successful Retry.
void check_failed_save_and_explicit_retry()
{
    for (const bool readback_failure : {false, true}) {
        reset_existing();
        expect(CODY::load_companion_preferences() == ESP_OK, "baseline preferences must load");
        const unsigned prior_sound_calls = fixture.sound_calls;
        auto &stored = fixture.keys["cody_sounds"];
        if (readback_failure) {
            stored.readback_error = kStorageFailure;
        } else {
            stored.write_error = kStorageFailure;
        }
        expect(CODY::persist_companion_preference(CODY::PreferenceId::Sounds, 0U) == kStorageFailure,
            "save must return the actual write or readback failure");
        expect(fixture.sounds_enabled && fixture.sound_calls == prior_sound_calls,
            "unconfirmed Off must not claim a runtime change");
        expect(stored.value == (readback_failure ? 0U : 1U), "fixture must expose partial persistence accurately");
        expect_failed_save(kStorageFailure);

        // Reading the UI projection cannot retry storage or apply a failed operation.
        LUNASHELL::CodyOptionsObservation observed;
        expect(LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(observed) == ESP_OK,
            "failed result must remain available to the UI");
        expect(stored.writes == 1U && fixture.sound_calls == prior_sound_calls,
            "failure must not cause an automatic write or hidden retry");

        stored.write_error = ESP_OK;
        stored.readback_error = ESP_OK;
        expect(CODY::persist_companion_preference(CODY::PreferenceId::Sounds, 0U) == ESP_OK,
            "explicit Retry of the same target should recover");
        expect(stored.writes == 2U && !fixture.sounds_enabled,
            "only explicit Retry may confirm and apply the persisted Off");
        expect(fixture.observation.preferences.data_valid &&
            fixture.observation.preferences.last_error == ESP_OK &&
            !fixture.observation.preferences.sounds_enabled, "successful Retry must publish confirmed Off");
    }

    reset_existing();
    expect(CODY::load_companion_preferences() == ESP_OK, "readback-mismatch baseline must load");
    fixture.keys["cody_sounds"].mismatch_readback = true;
    expect(CODY::persist_companion_preference(CODY::PreferenceId::Sounds, 0U) == ESP_ERR_INVALID_RESPONSE,
        "mismatched save readback must fail");
    expect(fixture.sounds_enabled, "mismatched readback must not apply Off");
    expect_failed_save(ESP_ERR_INVALID_RESPONSE);
}

// No parameters: independent owner/publication failures preserve their actual causes.
void check_owner_and_publication_failures()
{
    reset_existing();
    fixture.initialize_error = kStorageFailure;
    expect(CODY::load_companion_preferences() == kStorageFailure, "initialization error must be retained");
    expect(fixture.keys["cody_sounds"].reads == 0U && fixture.sound_calls == 0U,
        "failed initialization must stop dependent reads and application");
    expect_invalid(kStorageFailure);

    reset_existing();
    fixture.sound_error = ESP_ERR_INVALID_STATE;
    expect(CODY::load_companion_preferences() == ESP_ERR_INVALID_STATE, "audio owner failure must be returned");
    expect_invalid(ESP_ERR_INVALID_STATE);

    reset_existing();
    fixture.publication_error = ESP_ERR_TIMEOUT;
    expect(CODY::load_companion_preferences() == ESP_ERR_TIMEOUT, "publication failure alone must be returned");
    expect(!fixture.observation.preferences.data_valid, "failed publication must not create a current projection");

    reset_existing();
    fixture.keys["cody_sounds"].read_error = kStorageFailure;
    fixture.publication_error = ESP_ERR_TIMEOUT;
    expect(CODY::load_companion_preferences() == kStorageFailure,
        "independent publication failure must not replace the primary storage error");
    expect_invalid(kStorageFailure);

    reset_existing();
    expect(CODY::persist_companion_preference(CODY::PreferenceId::Sounds, 2U) == ESP_ERR_INVALID_ARG,
        "invalid user boolean must be rejected before persistence");
    expect(CODY::persist_companion_preference(CODY::PreferenceId::Orientation, 90U) == ESP_ERR_INVALID_ARG,
        "unsupported manual angle must be rejected before the display owner");
    expect(fixture.keys["cody_sounds"].writes == 0U && fixture.display_calls == 0U,
        "invalid input must not mutate storage or the display owner");
}


} // namespace

namespace LUNASHELL {

// No parameters: host-owned service interface; no physical NVS handle is opened.
UserSettingsService &UserSettingsService::instance()
{
    static UserSettingsService service;
    return service;
}

// No parameters: the host fixture owns no physical resource to release.
UserSettingsService::~UserSettingsService() = default;

// No parameters: initialization exposes only the explicitly injected host outcome.
esp_err_t UserSettingsService::initialize()
{
    ++fixture.initialize_calls;
    fixture.initialized = fixture.initialize_error == ESP_OK;
    return fixture.initialize_error;
}

// No parameters: reports the fixture lifecycle without initializing anything.
bool UserSettingsService::isInitialized() const
{
    return fixture.initialized;
}

// key: Only the existing profile key is represented by this explicitly synthetic fixture.
// value: Receives its stored printable name; the real owner still validates it.
// Exception: Returns invalid argument for another key or null destination.
esp_err_t UserSettingsService::getString(const char *key, std::string *value) const
{
    if (key == nullptr || value == nullptr || std::string(key) != "cody_user_name") {
        return ESP_ERR_INVALID_ARG;
    }
    *value = fixture.preferred_name;
    return ESP_OK;
}

// key: Existing profile key, separate from the mode and boolean storage fixtures.
// value: Exact synthetic profile value requested by the real owner.
// Exception: Returns invalid argument for another key or null value.
esp_err_t UserSettingsService::setString(const char *key, const char *value)
{
    if (key == nullptr || value == nullptr || std::string(key) != "cody_user_name") {
        return ESP_ERR_INVALID_ARG;
    }
    fixture.preferred_name = value;
    return ESP_OK;
}

// key: Actual key requested by the firmware implementation.
// value: Assigned only after the configured storage read succeeds.
// Exception: Returns injected storage/readback failures or genuine fixture absence.
esp_err_t UserSettingsService::getUInt32(const char *key, uint32_t *value) const
{
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &stored = fixture.keys[key];
    ++stored.reads;
    if (stored.read_error != ESP_OK) {
        return stored.read_error;
    }
    if (!stored.present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (stored.awaiting_readback) {
        stored.awaiting_readback = false;
        if (stored.readback_error != ESP_OK) {
            return stored.readback_error;
        }
        if (stored.mismatch_readback) {
            *value = stored.value ^ 1U;
            return ESP_OK;
        }
    }
    *value = stored.value;
    return ESP_OK;
}

// key: Actual persistence identity selected by the firmware implementation.
// value: Exact requested value, retained only after a successful fake write.
// Exception: Returns the injected write failure without modifying the previous value.
esp_err_t UserSettingsService::setUInt32(const char *key, uint32_t value)
{
    if (key == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &stored = fixture.keys[key];
    ++stored.writes;
    if (stored.write_error != ESP_OK) {
        return stored.write_error;
    }
    stored.value = value;
    stored.present = true;
    stored.awaiting_readback = true;
    return ESP_OK;
}

// key: Historical first-boot key, needed when linking the complete real source file.
// value: Receives only a successfully read strict fixture boolean.
// Exception: Returns original storage errors or a non-boolean value error.
esp_err_t UserSettingsService::getBool(const char *key, bool *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t stored = 0;
    const esp_err_t error = getUInt32(key, &stored);
    if (error != ESP_OK) {
        return error;
    }
    if (stored > 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    *value = stored == 1U;
    return ESP_OK;
}

// key: Historical first-boot key, needed by the complete real settings source.
// value: Exact boolean requested by the firmware implementation.
esp_err_t UserSettingsService::setBool(const char *key, bool value)
{
    return setUInt32(key, value ? 1U : 0U);
}

// No parameters: host projection service, never firmware acquisition or hardware.
DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService service;
    return service;
}

// observation: Receives the fake canonical projection only when its read succeeds.
// Exception: Returns the injected projection error without changing the destination.
esp_err_t DeviceSnapshotService::readCodyOptionsObservation(CodyOptionsObservation &observation) const
{
    if (fixture.projection_error != ESP_OK) {
        return fixture.projection_error;
    }
    observation = fixture.observation;
    return ESP_OK;
}

// preferences: Actual attempted publication from the real firmware settings owner.
// Exception: Returns the injected publication failure without changing canonical fixture state.
esp_err_t DeviceSnapshotService::reportCodyPreferences(const CodyPreferencesSnapshot &preferences)
{
    ++fixture.publication_calls;
    fixture.attempted_publication = preferences;
    if (fixture.publication_error != ESP_OK) {
        return fixture.publication_error;
    }
    fixture.observation.preferences = preferences;
    return ESP_OK;
}

// The host projection preserves each separately published observation and the
// same injected read/publication failures as the existing options projection.
#define HOST_OBSERVATION(Name, Type, field) \
    esp_err_t DeviceSnapshotService::read##Name(Type &observation) const \
    { \
        if (fixture.projection_error != ESP_OK) return fixture.projection_error; \
        observation = fixture.field; \
        return ESP_OK; \
    } \
    esp_err_t DeviceSnapshotService::report##Name(const Type &observation) \
    { \
        if (fixture.publication_error != ESP_OK) return fixture.publication_error; \
        fixture.field = observation; \
        return ESP_OK; \
    }

HOST_OBSERVATION(CodyIdleObservation, CodyIdleObservation, idle)
HOST_OBSERVATION(CodyEventVisualObservation, CodyEventVisualObservation, event_visual)
HOST_OBSERVATION(CodyParallaxObservation, CodyParallaxObservation, parallax)
HOST_OBSERVATION(CodyCharacterThemeObservation, CodyCharacterThemeObservation, character_theme)
#undef HOST_OBSERVATION

// Clock persistence is outside this host contract. Fail closed instead of
// fabricating a successful durable clock update.
esp_err_t apply_remote_clock_preferences(ClockRemoteOperation, int, int, int, int,
                                         ClockPreferences &, bool &)
{
    return ESP_ERR_NOT_SUPPORTED;
}

} // namespace LUNASHELL

// No parameters: deterministic synthetic time for host publication ordering.
extern "C" int64_t esp_timer_get_time(void)
{
    fixture.time_us += 100;
    return fixture.time_us;
}

// enabled: Actual application request made by the real settings implementation.
// Exception: Returns the injected owner error without applying the requested gate.
extern "C" esp_err_t LUNASHELL_audio_set_user_sounds_enabled(bool enabled)
{
    ++fixture.sound_calls;
    if (fixture.sound_error == ESP_OK) {
        fixture.sounds_enabled = enabled;
    }
    return fixture.sound_error;
}

// Hardware gain and output-volume operations are outside these sound-gate
// tests. Do not report a codec readback that the fixture did not perform.
extern "C" esp_err_t LUNASHELL_audio_set_user_output_volume(uint8_t, uint8_t *)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_set_user_input_gain(uint8_t, uint8_t *)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" void LUNASHELL_audio_update_cody_output_policy(esp_err_t)
{
    // The host has no audio startup gate.
}

// enabled: Actual wake policy request made by the real settings implementation.
// Exception: Returns the injected owner error without applying its requested policy.
extern "C" esp_err_t LUNASHELL_power_manager_set_shake_wake_enabled(bool enabled)
{
    ++fixture.shake_calls;
    if (fixture.shake_error == ESP_OK) {
        fixture.shake_enabled = enabled;
    }
    return fixture.shake_error;
}

// enabled: Actual auto-rotate request; no display or sensor is accessed by this fixture.
// Exception: Returns the explicitly injected display-owner result.
extern "C" esp_err_t LUNASHELL_display_set_auto_rotation_enabled(bool enabled)
{
    (void)enabled;
    ++fixture.display_calls;
    return fixture.display_error;
}

// rotation_degrees: Exact manual angle passed by the real settings implementation.
// Exception: Returns the explicitly injected display-owner result.
extern "C" esp_err_t LUNASHELL_display_set_rotation_degrees(uint32_t rotation_degrees)
{
    (void)rotation_degrees;
    ++fixture.display_calls;
    return fixture.display_error;
}

// percent: Exact brightness request from the firmware settings dispatcher.
// Exception: Returns the injected display-owner result.
extern "C" esp_err_t LUNASHELL_display_set_brightness_percent(uint32_t percent)
{
    (void)percent;
    ++fixture.display_calls;
    return fixture.display_error;
}

// No parameters: exercise real firmware preference code against explicit host failure injection.
int main()
{
    check_persistent_sounds_off();
    check_absence_and_strict_reads();
    check_failed_save_and_explicit_retry();
    check_owner_and_publication_failures();
    std::puts("Real Cody settings host contracts passed; hardware was not accessed.");
    return EXIT_SUCCESS;
}
