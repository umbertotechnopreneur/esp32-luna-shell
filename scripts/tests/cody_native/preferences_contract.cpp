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
#include "CODY_settings_service.hpp"
#include "CODY_settings_view.hpp"
#include "CODY_startup_model.hpp"
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
    unsigned settings_worker_wakes = 0;
    bool observe_settings_during_write = false;
    unsigned in_write_settings_reads = 0;
    unsigned options_reads = 0;
    unsigned idle_reads = 0;
    unsigned event_visual_reads = 0;
    unsigned parallax_reads = 0;
    unsigned character_theme_reads = 0;
    unsigned clock_reads = 0;
    esp_err_t clock_error = ESP_OK;
    bool require_output_policy_before_storage = false;
    unsigned output_policy_begins = 0;
    LUNASHELL::CodyOptionsObservation observation;
    LUNASHELL::CodyIdleObservation idle;
    LUNASHELL::CodyEventVisualObservation event_visual;
    LUNASHELL::CodyParallaxObservation parallax;
    LUNASHELL::CodyCharacterThemeObservation character_theme;
    LUNASHELL::ClockSnapshot clock;
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

// No parameters: notification admission never executes a persistence operation inline.
void wake_settings_worker()
{
    ++fixture.settings_worker_wakes;
}

// No parameters: each lifecycle scenario begins with explicit, already persisted first-run keys.
void reset_onboarding_baseline()
{
    reset_existing();
    fixture.keys["cody_firstboot"] = {true, 1U};
    fixture.keys["cody_birth"] = {true, 1U};
    fixture.keys["cody_firstmusic"] = {true, 1U};
    fixture.keys["cody_tour_step"] = {true, static_cast<uint32_t>(CODY::OnboardingTourStep::Buttons)};
    fixture.keys["cody_setup"] = {true, 1U};
    CODY::OnboardingSettings settings{};
    expect(CODY::load_onboarding_settings(settings) == ESP_OK, "onboarding baseline must load");
}

// No parameters: persisted progress remains canonical and invalid storage never gets repaired implicitly.
void check_onboarding_settings_model()
{
    reset_existing();
    CODY::OnboardingSettings loaded{};
    expect(CODY::load_onboarding_settings(loaded) == ESP_OK, "missing onboarding keys must be initialized");
    CODY::SettingsModel model{};
    expect(CODY::read_settings_model(model) == ESP_OK && model.valid && model.revision != 0,
        "successful load must publish a coherent settings model");
    expect(model.onboarding.first_boot_music_pending && model.onboarding.birth_intro_pending &&
        model.onboarding.setup_progress_pending && model.storage.first_boot_music.valid,
        "new installation must retain independent confirmed first-run markers");
    for (const char *key : {"cody_firstboot", "cody_birth", "cody_firstmusic", "cody_tour_step", "cody_setup"}) {
        const auto &stored = fixture.keys.at(key);
        expect(stored.writes == 1U && stored.reads == 2U,
            "every absent onboarding default must be written once and read back");
    }

    reset_onboarding_baseline();
    expect(CODY::persist_onboarding_tour_step(static_cast<uint32_t>(CODY::OnboardingTourStep::Thanks)) == ESP_OK,
        "Thanks must retain storage identity six");
    expect(CODY::read_settings_model(model) == ESP_OK && model.onboarding.onboarding_tour_step == 6U,
        "tour writes outside the lifecycle mailbox must publish confirmed progress");
    expect(CODY::persist_onboarding_tour_step(static_cast<uint32_t>(CODY::OnboardingTourStep::Completed)) == ESP_OK,
        "completion must retain storage identity five");
    expect(CODY::read_settings_model(model) == ESP_OK && model.onboarding.onboarding_tour_step == 5U,
        "completion and Thanks must remain distinct");

    reset_onboarding_baseline();
    fixture.keys["cody_tour_step"].value = 7U;
    expect(CODY::load_onboarding_settings(loaded) == ESP_ERR_INVALID_ARG,
        "unknown stored tour identity must be rejected");
    expect(fixture.keys["cody_tour_step"].writes == 0U && fixture.keys["cody_tour_step"].value == 7U,
        "invalid progress must not be clamped or rewritten");
    expect(CODY::read_settings_model(model) == ESP_ERR_INVALID_ARG && !model.valid,
        "failed reload must not expose the preceding model as current");
    uint32_t sequence = 123U;
    expect(CODY::request_settings_lifecycle(CODY::SettingsLifecycleOperation::CompleteSetup, sequence) ==
        ESP_ERR_INVALID_ARG && sequence == 0,
        "invalid canonical settings must reject lifecycle commands with the original load error");
}

// No parameters: receipt identity separates admission from a confirmed one-shot music marker.
void check_lifecycle_admission_and_identity()
{
    using Operation = CODY::SettingsLifecycleOperation;
    using State = CODY::SettingsCommandState;
    reset_onboarding_baseline();
    expect(CODY::register_settings_lifecycle_worker(wake_settings_worker) == ESP_OK,
        "the existing action worker must be registerable without another task");
    uint32_t music_sequence = 0;
    expect(CODY::request_settings_lifecycle(Operation::ConsumeFirstBootMusic, music_sequence) == ESP_OK &&
        music_sequence != 0, "music-marker admission needs an exact nonzero sequence");
    expect(fixture.settings_worker_wakes == 1U && fixture.keys["cody_firstmusic"].writes == 0U,
        "display-side admission must only notify, never write NVS inline");
    CODY::SettingsLifecycleReceipt receipt{};
    expect(CODY::read_settings_lifecycle_receipt(Operation::ConsumeFirstBootMusic, music_sequence, receipt) ==
        ESP_OK && receipt.state == State::Queued, "queued marker must not look like confirmed consumption");
    uint32_t duplicate = 123U;
    expect(CODY::request_settings_lifecycle(Operation::ConsumeFirstBootMusic, duplicate) == ESP_ERR_NOT_FINISHED &&
        duplicate == 0 && fixture.settings_worker_wakes == 1U,
        "a duplicate in-flight marker must not be admitted or notified twice");

    uint32_t setup_sequence = 0;
    expect(CODY::request_settings_lifecycle(Operation::CompleteSetup, setup_sequence) == ESP_OK &&
        setup_sequence != music_sequence, "independent queued domains need distinct receipt identities");
    expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteSetup, music_sequence, receipt) == ESP_ERR_NOT_FOUND,
        "a sequence from another lifecycle operation must not authorize completion");
    expect(CODY::process_settings_lifecycle_command(), "the worker must drain the admitted music marker");
    expect(CODY::read_settings_lifecycle_receipt(Operation::ConsumeFirstBootMusic, music_sequence, receipt) == ESP_OK &&
        receipt.state == State::Succeeded && receipt.error == ESP_OK,
        "music may be authorized only after worker write and exact readback");
    CODY::SettingsModel model{};
    expect(CODY::read_settings_model(model) == ESP_OK && !model.onboarding.first_boot_music_pending &&
        model.onboarding.setup_progress_pending, "a completed music write must not prematurely complete setup");
    expect(CODY::process_settings_lifecycle_command(), "the worker must drain the independent setup marker");
    expect(CODY::read_settings_lifecycle_receipt(Operation::ConsumeFirstBootMusic, music_sequence, receipt) == ESP_OK &&
        receipt.state == State::Succeeded, "processing setup must retain the music receipt");
    expect(!CODY::process_settings_lifecycle_command() && fixture.keys["cody_firstmusic"].writes == 1U &&
        fixture.keys["cody_setup"].writes == 1U, "an extra worker wake must never replay completed writes");
}

// No parameters: write, readback and mismatch failures stay visible until an explicit exact retry succeeds.
void check_lifecycle_failures_and_retry()
{
    using Operation = CODY::SettingsLifecycleOperation;
    using State = CODY::SettingsCommandState;
    enum class Failure { Write, Readback, Mismatch };
    for (const auto failure : {Failure::Write, Failure::Readback, Failure::Mismatch}) {
        reset_onboarding_baseline();
        CODY::SettingsModel baseline{};
        expect(CODY::read_settings_model(baseline) == ESP_OK, "retry baseline must be confirmed");
        const auto original_revision = baseline.storage.birth_intro.revision;
        fixture.observe_settings_during_write = true;
        auto &stored = fixture.keys["cody_birth"];
        if (failure == Failure::Write) stored.write_error = kStorageFailure;
        if (failure == Failure::Readback) stored.readback_error = kStorageFailure;
        if (failure == Failure::Mismatch) stored.mismatch_readback = true;
        const auto expected_error = failure == Failure::Mismatch ? ESP_ERR_INVALID_RESPONSE : kStorageFailure;
        uint32_t first = 0;
        expect(CODY::request_settings_lifecycle(Operation::CompleteBirthIntro, first, original_revision) == ESP_OK,
            "intro completion must be asynchronously admitted");
        expect(CODY::process_settings_lifecycle_command(), "intro completion must run on its worker");
        CODY::SettingsLifecycleReceipt receipt{};
        expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteBirthIntro, first, receipt) == ESP_OK &&
            receipt.state == State::Failed && receipt.error == expected_error,
            "failed lifecycle completion must retain its exact storage cause");
        CODY::SettingsModel model{};
        expect(CODY::read_settings_model(model) == ESP_OK && model.valid && model.onboarding.birth_intro_pending &&
            !model.storage.birth_intro.valid && model.storage.birth_intro.error == expected_error &&
            model.storage.birth_intro.revision == original_revision,
            "an optional marker failure must retain usable settings without claiming confirmed completion");
        expect(fixture.in_write_settings_reads == 1U,
            "storage execution must allow a concurrent/reentrant read of the settings model");
        expect(!CODY::process_settings_lifecycle_command() && stored.writes == 1U,
            "a failed marker must not retry just because the worker wakes again");

        uint32_t setup = 0;
        expect(CODY::request_settings_lifecycle(Operation::CompleteSetup, setup) == ESP_OK &&
            CODY::process_settings_lifecycle_command(), "an independent completion may proceed after intro failure");
        expect(CODY::read_settings_model(model) == ESP_OK && !model.onboarding.setup_progress_pending &&
            !model.storage.birth_intro.valid && model.storage.birth_intro.error == expected_error,
            "another successful marker must not erase the intro's uncertainty");

        stored.write_error = ESP_OK;
        stored.readback_error = ESP_OK;
        stored.mismatch_readback = false;
        uint32_t retry = 0;
        expect(CODY::request_settings_lifecycle(Operation::CompleteBirthIntro, retry, original_revision) == ESP_OK && retry != first,
            "explicit Retry must receive a new receipt identity");
        expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteBirthIntro, first, receipt) == ESP_ERR_NOT_FOUND,
            "the preceding failed receipt cannot be mistaken for the retry result");
        expect(CODY::process_settings_lifecycle_command(), "explicit Retry must run once");
        expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteBirthIntro, retry, receipt) == ESP_OK &&
            receipt.state == State::Succeeded && receipt.error == ESP_OK,
            "successful Retry must confirm its own write/readback");
        expect(CODY::read_settings_model(model) == ESP_OK && !model.onboarding.birth_intro_pending &&
            model.storage.birth_intro.valid && model.storage.birth_intro.error == ESP_OK && stored.writes == 2U,
            "only confirmed Retry may clear the pending intro and its storage error");
    }
}

// No parameters: a newer Replay request supersedes completion retries, including uncertain Replay readback.
void check_lifecycle_preserves_newer_replay()
{
    using Operation = CODY::SettingsLifecycleOperation;
    using State = CODY::SettingsCommandState;
    enum class ReplayResult { Success, WriteFailure, ReadbackFailure };
    for (const auto outcome : {ReplayResult::Success, ReplayResult::WriteFailure, ReplayResult::ReadbackFailure}) {
        reset_onboarding_baseline();
        CODY::SettingsModel model{};
        expect(CODY::read_settings_model(model) == ESP_OK, "original birth intent must be readable");
        const auto original_revision = model.storage.birth_intro.revision;
        auto &stored = fixture.keys["cody_birth"];
        stored.write_error = kStorageFailure;
        uint32_t original = 0;
        expect(CODY::request_settings_lifecycle(Operation::CompleteBirthIntro, original, original_revision) == ESP_OK &&
            CODY::process_settings_lifecycle_command(), "the original completion must expose its failed save");
        stored.write_error = outcome == ReplayResult::WriteFailure ? kStorageFailure : ESP_OK;
        stored.readback_error = outcome == ReplayResult::ReadbackFailure ? kStorageFailure : ESP_OK;
        const auto replay_error = outcome == ReplayResult::Success ? ESP_OK : kStorageFailure;
        expect(CODY::persist_birth_intro_pending(true) == replay_error,
            "the newer explicit Replay keeps its real persistence outcome");
        expect(CODY::read_settings_model(model) == ESP_OK && model.storage.birth_intro.revision != original_revision,
            "even an unconfirmed Replay must supersede the older automatic intent");
        const unsigned writes_after_replay = stored.writes;
        stored.write_error = ESP_OK;
        stored.readback_error = ESP_OK;
        uint32_t retry = 0;
        expect(CODY::request_settings_lifecycle(Operation::CompleteBirthIntro, retry, original_revision) == ESP_OK &&
            CODY::process_settings_lifecycle_command(), "the old intent must receive its own superseded receipt");
        CODY::SettingsLifecycleReceipt receipt{};
        expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteBirthIntro, retry, receipt) == ESP_OK &&
            receipt.state == State::Superseded && receipt.error == ESP_ERR_INVALID_STATE &&
            receipt.expected_revision == original_revision, "supersession must be terminal and identity-specific");
        expect(stored.writes == writes_after_replay && stored.value == 1U,
            "an automatic retry must not overwrite a newer Replay, even when storage is healthy again");
    }

    reset_onboarding_baseline();
    CODY::SettingsModel model{};
    expect(CODY::read_settings_model(model) == ESP_OK, "queued-race baseline must load");
    uint32_t queued = 0;
    expect(CODY::request_settings_lifecycle(Operation::CompleteBirthIntro, queued,
        model.storage.birth_intro.revision) == ESP_OK, "completion must be admitted before the newer intent");
    expect(CODY::persist_birth_intro_pending(true) == ESP_OK,
        "an already running Replay may publish before the worker drains the completion queue");
    const unsigned writes_after_replay = fixture.keys["cody_birth"].writes;
    expect(CODY::process_settings_lifecycle_command(), "queued completion must be checked by the worker");
    CODY::SettingsLifecycleReceipt receipt{};
    expect(CODY::read_settings_lifecycle_receipt(Operation::CompleteBirthIntro, queued, receipt) == ESP_OK &&
        receipt.state == State::Superseded && fixture.keys["cody_birth"].writes == writes_after_replay,
        "checking only at admission would miss a Replay that finishes before actual execution");
}

// No parameters: bootstrap owns output-policy ordering while retaining optional codec failure separately.
void check_settings_bootstrap_policy_boundary()
{
    reset_existing();
    fixture.require_output_policy_before_storage = true;
    CODY::SettingsBootstrapResult bootstrap{};
    expect(CODY::bootstrap_device_settings(bootstrap) == ESP_OK,
        "optional codec failure must not erase successfully loaded required settings");
    expect(fixture.output_policy_begins == 1U && bootstrap.audio_output_error == ESP_ERR_NOT_SUPPORTED,
        "bootstrap must arm the output gate once and retain the unsupported host codec outcome");

    reset_existing();
    fixture.require_output_policy_before_storage = true;
    fixture.initialize_error = kStorageFailure;
    expect(CODY::bootstrap_device_settings(bootstrap) == kStorageFailure && fixture.output_policy_begins == 1U &&
        bootstrap.audio_output_error == ESP_ERR_NOT_FINISHED,
        "required initialization failure must stop dependent output reconciliation after arming the gate");
}

// No parameters: visible settings capture each selected source once, then project without more reads.
void check_bounded_settings_view()
{
    reset_existing();
    auto &preferences = fixture.observation.preferences;
    preferences.data_valid = true;
    preferences.data_stale = false;
    preferences.last_error = ESP_OK;
    preferences.sounds_enabled = true;
    fixture.idle.valid = true;
    fixture.idle.last_error = ESP_OK;
    fixture.idle.show_muse = true;
    fixture.idle.delay_seconds = 60;
    fixture.clock.preferences_valid = true;
    fixture.clock.quiet_hours_enabled = true;
    fixture.clock.quiet_start_minute = 1200;
    fixture.clock.quiet_end_minute = 420;
    const auto domains = static_cast<CODY::SettingsDomains>(
        CODY::preference_domains(CODY::PreferenceId::Sounds) |
        CODY::preference_domains(CODY::PreferenceId::IdleMode) |
        CODY::preference_domains(CODY::PreferenceId::IdleDelay) |
        CODY::preference_domains(CODY::PreferenceId::QuietEnabled));
    CODY::SettingsView view{};
    CODY::capture_settings_view(view, domains);
    expect(fixture.options_reads == 1U && fixture.idle_reads == 1U && fixture.clock_reads == 1U &&
        fixture.event_visual_reads == 0U && fixture.parallax_reads == 0U && fixture.character_theme_reads == 0U,
        "settings capture must read each requested domain once and skip unrelated domains");
    const auto sound = CODY::project_settings_preference(CODY::PreferenceId::Sounds, view);
    const auto idle_mode = CODY::project_settings_preference(CODY::PreferenceId::IdleMode, view);
    const auto idle_delay = CODY::project_settings_preference(CODY::PreferenceId::IdleDelay, view);
    const auto quiet = CODY::project_quiet_hours(view);
    expect(sound.available && sound.active && idle_mode.available && idle_mode.active &&
        idle_delay.available && idle_delay.value == 60U && quiet.available && quiet.enabled,
        "one capture must project each independent confirmed setting");
    for (unsigned pass = 0; pass < 5; ++pass) {
        expect(CODY::same_preference_presentation(sound,
            CODY::project_settings_preference(CODY::PreferenceId::Sounds, view)),
            "repeated projection must retain the same captured value");
        expect(CODY::same_quiet_hours_presentation(quiet, CODY::project_quiet_hours(view)),
            "quiet-hours projection must be pure");
    }
    expect(fixture.options_reads == 1U && fixture.idle_reads == 1U && fixture.clock_reads == 1U,
        "projection and equality must not poll snapshot owners again");

    fixture.clock_error = kStorageFailure;
    CODY::capture_settings_view(view, domains);
    expect(CODY::project_settings_preference(CODY::PreferenceId::Sounds, view).available,
        "clock failure must not hide a separately available sound setting");
    const auto failed_quiet = CODY::project_quiet_hours(view);
    expect(!failed_quiet.available && !failed_quiet.enabled && failed_quiet.start_minute == 0 &&
        failed_quiet.error == kStorageFailure,
        "failed clock capture must preserve its cause and discard preceding displayed values");
    expect(!CODY::preference_would_change(CODY::PreferenceId::QuietStart, 900U, view),
        "unavailable settings cannot establish a successful changed-value comparison");

    CODY::capture_settings_view(view, CODY::settings_domain(CODY::SettingsDomain::Options));
    expect(!CODY::project_settings_preference(CODY::PreferenceId::IdleDelay, view).available,
        "a domain omitted from a new capture must not retain an earlier valid projection");
}

// No parameters: confirmed external changes refresh the view without silently rewriting the editor's draft.
void check_quiet_settings_capture_and_draft()
{
    reset_existing();
    fixture.clock.preferences_valid = true;
    fixture.clock.quiet_hours_enabled = false;
    fixture.clock.quiet_start_minute = 1200;
    fixture.clock.quiet_end_minute = 420;
    CODY::SettingsView view{};
    const auto clock_domain = CODY::settings_domain(CODY::SettingsDomain::Clock);
    CODY::capture_settings_view(view, clock_domain);
    const auto previous = CODY::project_quiet_hours(view);
    CODY::QuietHoursDraft draft{previous.start_minute, true};
    draft.minute = 1230;

    fixture.clock.quiet_hours_enabled = true;
    fixture.clock.quiet_start_minute = 1260;
    expect(!CODY::project_quiet_hours(view).enabled,
        "a retained frame must not change behind the presentation owner's back");
    CODY::capture_settings_view(view, clock_domain);
    const auto current = CODY::project_quiet_hours(view);
    expect(current.available && current.enabled && current.start_minute == 1260U &&
        !CODY::same_quiet_hours_presentation(previous, current),
        "the next captured revision must reveal an external quiet-hours change");
    expect(draft.minute == 1230U && draft.editing_start,
        "refreshing confirmed settings must not mutate the UI-owned edit draft");
    expect(!CODY::preference_would_change(CODY::PreferenceId::QuietEnabled, 1U, view) &&
        CODY::preference_would_change(CODY::PreferenceId::QuietEnabled, 0U, view),
        "pre-write comparisons must use the newly captured enabled value");

    fixture.clock.preferences_valid = false;
    CODY::capture_settings_view(view, clock_domain);
    expect(!CODY::project_quiet_hours(view).available,
        "invalid preferences must prevent a stale quiet-hours action");
    fixture.clock.preferences_valid = true;
    fixture.clock.quiet_start_minute = 1440;
    CODY::capture_settings_view(view, clock_domain);
    expect(CODY::project_quiet_hours(view).error == ESP_ERR_INVALID_RESPONSE,
        "an out-of-range stored minute must not become a valid edit source");
}

// observation: Synthetic owner projection for model contracts; never reports real network traffic.
// now_us: Positive host timestamp for the authenticated READY observation.
void prepare_startup_ready(LUNASHELL::ConnectivityObservation &observation, int64_t now_us)
{
    observation = {};
    auto &wifi = observation.radios.wifi;
    wifi.data_valid = true;
    wifi.health = LUNASHELL::DeviceHealth::Ready;
    wifi.last_error = ESP_OK;
    wifi.connection_state_valid = true;
    wifi.credentials_configured = true;
    wifi.started = true;
    wifi.connected = true;
    wifi.ip_acquired = true;
    std::snprintf(wifi.ip_address, sizeof(wifi.ip_address), "%s", "192.0.2.10");
    auto &link = observation.radios.codex_link;
    link.data_valid = true;
    link.last_error = ESP_OK;
    link.retained_link_present = true;
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.ready_at_us = now_us;
}

// No parameters: retries, status changes and user departure cannot restart the one boot window.
void check_startup_absolute_window_and_cancel()
{
    using Phase = CODY::StartupConnectionPhase;
    constexpr int64_t started_us = 1'000'000;
    CODY::StartupConnectionModel model{};
    expect(!CODY::startup_connection_window_open(model, started_us),
        "startup wait must not open before its display owner admits the boot window");
    CODY::begin_startup_connection(model, -1);
    expect(model.started_us < 0, "an unavailable timestamp cannot admit a boot window");
    CODY::begin_startup_connection(model, started_us);
    expect(!CODY::startup_connection_window_open(model, started_us - 1),
        "a timestamp before admission must not extend the startup window");
    LUNASHELL::ConnectivityObservation observation{};
    prepare_startup_ready(observation, started_us);
    auto &wifi = observation.radios.wifi;
    wifi.connected = false;
    wifi.ip_acquired = false;
    wifi.ip_address[0] = '\0';
    wifi.reconnect_pending = true;
    for (unsigned retry = 1; retry <= 3; ++retry) {
        const auto now_us = started_us + static_cast<int64_t>(retry) * 9'000'000;
        CODY::begin_startup_connection(model, now_us);
        wifi.reconnect_attempt_count = retry;
        wifi.last_disconnect_reason = static_cast<uint16_t>(retry);
        const auto status = CODY::project_startup_connection(observation, ESP_OK, true, now_us);
        expect(status.phase == Phase::WifiRetry && status.disconnect_reason == retry &&
            CODY::startup_connection_should_wait(model, status, now_us),
            "a real retry may update status while the original boot window remains open");
        expect(model.started_us == started_us, "projecting a retry must not move the boot deadline");
    }
    const CODY::StartupConnectionStatus waiting{Phase::WifiRetry};
    const auto deadline_us = started_us + CODY::StartupConnectionModel::kWindowUs;
    expect(CODY::startup_connection_should_wait(model, waiting, deadline_us - 1) &&
        !CODY::startup_connection_should_wait(model, waiting, deadline_us) &&
        !CODY::startup_connection_should_wait(model, waiting, deadline_us + 10'000'000),
        "startup retries must stop waiting at exactly the absolute 30-second deadline");
    model.retired = true; // The display owner retires this lifetime on manual route departure.
    expect(!CODY::startup_connection_should_wait(model, waiting, started_us + 1),
        "manual departure must prevent startup replay even within the original time window");
    CODY::begin_startup_connection(model, deadline_us);
    expect(model.started_us == started_us && !CODY::startup_connection_should_wait(model, waiting, deadline_us + 1),
        "a later begin request cannot reopen or redate a retired display-owner lifetime");
    CODY::StartupConnectionModel cancelled_before_admission{};
    cancelled_before_admission.retired = true;
    CODY::begin_startup_connection(cancelled_before_admission, deadline_us);
    expect(cancelled_before_admission.started_us < 0,
        "a cancelled lifetime must reject even its first later admission request");
}

// No parameters: only a fresh authenticated bridge READY can finish a paired startup early.
void check_startup_ready_evidence()
{
    using Phase = CODY::StartupConnectionPhase;
    constexpr int64_t now_us = 12'000'000;
    CODY::StartupConnectionModel model{};
    CODY::begin_startup_connection(model, now_us - 1'000'000);
    LUNASHELL::ConnectivityObservation observation{};
    prepare_startup_ready(observation, now_us);
    const auto project = [&]() {
        return CODY::project_startup_connection(observation, ESP_OK, true, now_us);
    };
    auto status = project();
    expect(status.phase == Phase::Ready && !CODY::startup_connection_should_wait(model, status, now_us),
        "fresh authenticated READY must finish early while the time window is still open");
    auto &link = observation.radios.codex_link;
    link.ready_at_us = now_us - static_cast<int64_t>(LUNASHELL::codex_presence::kTransportTtlUs);
    status = project();
    expect(status.phase == Phase::PcWaiting && CODY::startup_connection_should_wait(model, status, now_us),
        "Connected with READY expired at the TTL boundary must not finish startup");
    link.ready_at_us = now_us + 1;
    expect(project().phase == Phase::PcWaiting, "a future READY timestamp must not establish current liveness");
    link.ready_at_us = 0;
    expect(project().phase == Phase::PcWaiting, "Connected without authenticated READY time must keep waiting");
    link.ready_at_us = now_us;
    link.data_stale = true;
    expect(project().phase == Phase::PcUnavailable, "a stale owner projection must not establish current READY");
    link.data_stale = false;
    link.last_error = kStorageFailure;
    status = project();
    expect(status.phase == Phase::Ready && status.error == kStorageFailure &&
        !CODY::startup_connection_should_wait(model, status, now_us),
        "an optional capability error must be retained without delaying fresh authenticated READY");
    link.ready_at_us = 0;
    status = project();
    expect(status.phase == Phase::PcUnavailable && status.error == kStorageFailure,
        "without fresh READY a bridge error must remain unavailable with its original cause");
    link.last_error = ESP_OK;
    link.ready_at_us = now_us;
    link.state = LUNASHELL::CodexLinkState::Discovering;
    expect(project().phase == Phase::PcReconnecting,
        "discovery admission alone must not be presented as a received peer announcement");
    observation.radios.wifi.data_stale = true;
    expect(project().phase == Phase::WifiUnavailable, "stale Wi-Fi must prevent an early ready transition");
}

// No parameters: missing setup cannot fabricate connection work, and repeated read errors remain bounded.
void check_startup_setup_and_read_failures()
{
    using Phase = CODY::StartupConnectionPhase;
    constexpr int64_t started_us = 1'000'000;
    CODY::StartupConnectionModel model{};
    CODY::begin_startup_connection(model, started_us);
    LUNASHELL::ConnectivityObservation observation{};
    prepare_startup_ready(observation, started_us);
    observation.radios.wifi.credentials_configured = false;
    auto status = CODY::project_startup_connection(observation, ESP_OK, true, started_us);
    expect(status.phase == Phase::NeedsSetup && !CODY::startup_connection_should_wait(model, status, started_us),
        "confirmed missing Wi-Fi credentials must go to setup without fabricating connection work");
    observation.radios.wifi.credentials_configured = true;
    auto &link = observation.radios.codex_link;
    link.retained_link_present = false;
    status = CODY::project_startup_connection(observation, ESP_OK, true, started_us);
    expect(status.phase == Phase::NeedsSetup && !CODY::startup_connection_should_wait(model, status, started_us),
        "missing retained and candidate pairing must not fabricate a PC reconnect");
    link.pending_pairing_present = true;
    link.state = LUNASHELL::CodexLinkState::Discovering;
    status = CODY::project_startup_connection(observation, ESP_OK, true, started_us);
    expect(status.phase == Phase::PcReconnecting && CODY::startup_connection_should_wait(model, status, started_us),
        "a real retained candidate may wait for owner discovery without claiming READY");
    link.pairing_repair_required = true;
    expect(CODY::project_startup_connection(observation, ESP_OK, true, started_us).phase == Phase::NeedsSetup,
        "pairing repair must require setup instead of presenting a reconnect attempt");
    expect(CODY::project_startup_connection(observation, ESP_OK, false, started_us).phase == Phase::Ready,
        "a build without PC integration must finish when the canonical Wi-Fi owner is ready");

    prepare_startup_ready(observation, started_us);
    for (const auto elapsed_us : {0LL, 29'999'999LL, 30'000'000LL, 31'000'000LL}) {
        const auto now_us = started_us + elapsed_us;
        status = CODY::project_startup_connection(observation, kTypeMismatch, true, now_us);
        expect(status.phase == Phase::Unavailable && status.error == kTypeMismatch,
            "failed snapshot reads must retain the original error and ignore old READY bytes");
        expect(CODY::startup_connection_should_wait(model, status, now_us) ==
            (elapsed_us < CODY::StartupConnectionModel::kWindowUs),
            "repeated read failures must not extend the absolute startup wait");
    }
}

// No parameters: first confirmed IP celebrates Wi-Fi independently of the pending PC handshake.
void check_startup_wifi_success_interval()
{
    using Phase = CODY::StartupConnectionPhase;
    constexpr int64_t started_us = 1'000'000;
    constexpr int64_t connected_us = started_us + 3'000'000;
    CODY::StartupConnectionModel model{};
    CODY::begin_startup_connection(model, started_us);
    LUNASHELL::ConnectivityObservation observation{};
    prepare_startup_ready(observation, connected_us);
    auto &wifi = observation.radios.wifi;
    auto &link = observation.radios.codex_link;
    link.state = LUNASHELL::CodexLinkState::Discovering;
    link.ready_at_us = 0;
    wifi.ip_acquired = false;
    wifi.ip_address[0] = '\0';
    auto status = CODY::project_startup_connection(observation, ESP_OK, true, started_us);
    expect(status.phase == Phase::WifiAddress && !status.wifi_ready &&
        !CODY::update_startup_wifi_success(model, status, started_us) && model.wifi_success_started_us < 0,
        "association without IP must neither celebrate Wi-Fi nor consume its one success reaction");
    wifi.ip_acquired = true;
    std::snprintf(wifi.ip_address, sizeof(wifi.ip_address), "%s", "192.0.2.10");
    status = CODY::project_startup_connection(observation, ESP_OK, true, connected_us);
    expect(status.phase == Phase::PcReconnecting && status.wifi_ready &&
        CODY::startup_connection_should_wait(model, status, connected_us) &&
        CODY::update_startup_wifi_success(model, status, connected_us),
        "first confirmed IP must start the Wi-Fi reaction while authenticated PC discovery still waits");
    expect(model.started_us == started_us && model.wifi_success_started_us == connected_us,
        "Wi-Fi success needs its own clock without moving the absolute startup deadline");
    const auto end_us = connected_us + CODY::StartupConnectionModel::kWifiSuccessUs;
    expect(CODY::StartupConnectionModel::kWifiSuccessUs == 1'200'000 &&
        CODY::update_startup_wifi_success(model, status, end_us - 1) &&
        !CODY::update_startup_wifi_success(model, status, end_us),
        "the Wi-Fi success reaction must end at exactly 1.2 seconds");
    expect(!CODY::update_startup_wifi_success(model, status, end_us + 1) &&
        model.wifi_success_started_us == connected_us,
        "subsequent connected snapshots must not replay an expired Wi-Fi reaction");
    auto without_wifi = status;
    without_wifi.wifi_ready = false;
    expect(!CODY::same_startup_connection_status(status, without_wifi),
        "presentation comparison must notice changed Wi-Fi evidence even when the PC phase matches");
}

// No parameters: loss or failed canonical reads cancel the reaction without consuming stale success again.
void check_startup_wifi_success_loss_and_error()
{
    constexpr int64_t connected_us = 4'000'000;
    for (unsigned failure = 0; failure < 4; ++failure) {
        CODY::StartupConnectionModel model{};
        CODY::begin_startup_connection(model, 1'000'000);
        LUNASHELL::ConnectivityObservation observation{};
        prepare_startup_ready(observation, connected_us);
        observation.radios.codex_link.state = LUNASHELL::CodexLinkState::Discovering;
        auto status = CODY::project_startup_connection(observation, ESP_OK, true, connected_us);
        expect(CODY::update_startup_wifi_success(model, status, connected_us),
            "each independent host scenario must first admit a real Wi-Fi success projection");
        esp_err_t read_error = ESP_OK;
        if (failure == 0) observation.radios.wifi.connected = false;
        else if (failure == 1) read_error = kTypeMismatch;
        else if (failure == 2) observation.radios.wifi.last_error = kStorageFailure;
        else observation.radios.wifi.data_stale = true;
        status = CODY::project_startup_connection(observation, read_error, true, connected_us + 200'000);
        expect(!status.wifi_ready && !CODY::update_startup_wifi_success(model, status, connected_us + 200'000) &&
            model.wifi_success_started_us == connected_us,
            "link loss, failed reads, owner errors or stale Wi-Fi must cancel and retain the consumed reaction");
        prepare_startup_ready(observation, connected_us + 400'000);
        status = CODY::project_startup_connection(observation, ESP_OK, true, connected_us + 400'000);
        expect(status.wifi_ready && !CODY::update_startup_wifi_success(model, status, connected_us + 400'000),
            "recovery before the original 1.2 seconds expires must not replay a cancelled success reaction");
    }
}

// No parameters: success cannot extend the boot deadline, delay required setup or survive route retirement.
void check_startup_wifi_success_deadline_and_retirement()
{
    constexpr int64_t started_us = 1'000'000;
    constexpr int64_t deadline_us = started_us + CODY::StartupConnectionModel::kWindowUs;
    LUNASHELL::ConnectivityObservation observation{};
    prepare_startup_ready(observation, deadline_us - 200'000);
    observation.radios.codex_link.state = LUNASHELL::CodexLinkState::Discovering;
    auto status = CODY::project_startup_connection(observation, ESP_OK, true, deadline_us - 200'000);
    CODY::StartupConnectionModel deadline_model{};
    CODY::begin_startup_connection(deadline_model, started_us);
    expect(CODY::update_startup_wifi_success(deadline_model, status, deadline_us - 200'000) &&
        CODY::update_startup_wifi_success(deadline_model, status, deadline_us - 1) &&
        !CODY::update_startup_wifi_success(deadline_model, status, deadline_us),
        "a late Wi-Fi reaction must be cut short by the original 30-second deadline");
    CODY::StartupConnectionModel already_expired{};
    CODY::begin_startup_connection(already_expired, started_us);
    expect(!CODY::update_startup_wifi_success(already_expired, status, deadline_us) &&
        already_expired.wifi_success_started_us < 0,
        "first IP at the deadline must not admit a new Wi-Fi reaction");
    CODY::StartupConnectionModel retired_model{};
    CODY::begin_startup_connection(retired_model, started_us);
    prepare_startup_ready(observation, started_us + 1);
    observation.radios.codex_link.state = LUNASHELL::CodexLinkState::Discovering;
    status = CODY::project_startup_connection(observation, ESP_OK, true, started_us + 1);
    expect(CODY::update_startup_wifi_success(retired_model, status, started_us + 1),
        "retirement scenario must begin with an admitted Wi-Fi reaction");
    retired_model.retired = true;
    expect(!CODY::update_startup_wifi_success(retired_model, status, started_us + 2),
        "manual route departure must immediately stop the Wi-Fi reaction");
    CODY::begin_startup_connection(retired_model, started_us + 3);
    expect(!CODY::update_startup_wifi_success(retired_model, status, started_us + 3),
        "a later begin request cannot replay a retired success reaction");
    CODY::StartupConnectionModel setup_model{};
    CODY::begin_startup_connection(setup_model, started_us);
    observation.radios.codex_link.retained_link_present = false;
    status = CODY::project_startup_connection(observation, ESP_OK, true, started_us + 1);
    expect(status.phase == CODY::StartupConnectionPhase::NeedsSetup && status.wifi_ready &&
        !CODY::update_startup_wifi_success(setup_model, status, started_us + 1) &&
        setup_model.wifi_success_started_us < 0,
        "confirmed Wi-Fi must not impose a success delay when pairing still needs explicit setup");
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
    if (fixture.require_output_policy_before_storage) {
        expect(fixture.output_policy_begins == 1U,
            "the settings facade must arm the output policy before initialization or storage reads");
    }
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
    if (fixture.observe_settings_during_write) {
        CODY::SettingsModel observed{};
        expect(CODY::read_settings_model(observed) == ESP_OK && observed.valid,
            "the settings mutex must not be held across a storage call");
        ++fixture.in_write_settings_reads;
    }
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
    ++fixture.options_reads;
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
        ++fixture.field##_reads; \
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

// observation: Receives the independently captured synthetic clock preferences.
// Exception: Returns the injected clock-domain error without borrowing old displayed values.
esp_err_t DeviceSnapshotService::readClockObservation(ClockSnapshot &observation) const
{
    ++fixture.clock_reads;
    if (fixture.clock_error != ESP_OK) return fixture.clock_error;
    observation = fixture.clock;
    return ESP_OK;
}

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

// No parameters: records the facade boundary only; no audio gate or playback success is emulated.
extern "C" void LUNASHELL_audio_begin_cody_output_policy(void)
{
    ++fixture.output_policy_begins;
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
    check_onboarding_settings_model();
    check_lifecycle_admission_and_identity();
    check_lifecycle_failures_and_retry();
    check_lifecycle_preserves_newer_replay();
    check_settings_bootstrap_policy_boundary();
    check_bounded_settings_view();
    check_quiet_settings_capture_and_draft();
    check_startup_absolute_window_and_cancel();
    check_startup_ready_evidence();
    check_startup_setup_and_read_failures();
    check_startup_wifi_success_interval();
    check_startup_wifi_success_loss_and_error();
    check_startup_wifi_success_deadline_and_retirement();
    std::puts("Real Cody settings host contracts passed; hardware was not accessed.");
    return EXIT_SUCCESS;
}
