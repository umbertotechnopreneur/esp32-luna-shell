/* Copyright (c) 2026 Umberto Giacobbi */
#include "CODY_settings_service.hpp"
#include "CODY_settings.hpp"
#include "CODY_settings_state.hpp"
#include "LUNASHELL_audio_controls.h"

#include <cstddef>
#include <limits>
#include <mutex>

namespace CODY {
namespace {
std::mutex s_model_mutex;
SettingsModel s_model{};
uint32_t s_next_sequence = 0;
void (*s_wake_worker)() = nullptr;

// operation: Stable lifecycle identity, independent of receipt array order.
// Returns an out-of-range index for unsupported identities.
constexpr std::size_t receipt_index(SettingsLifecycleOperation operation)
{
    switch (operation) {
    case SettingsLifecycleOperation::ConsumeFirstBootMusic: return 0;
    case SettingsLifecycleOperation::CompleteBirthIntro: return 1;
    case SettingsLifecycleOperation::CompleteSetup: return 2;
    default: return 3;
    }
}

// state: Current receipt disposition; a terminal result remains readable until retry.
constexpr bool in_flight(SettingsCommandState state)
{
    return state == SettingsCommandState::Queued || state == SettingsCommandState::Running;
}

// status: Metadata for one canonical setting, independent from other settings' writes.
// error: Original confirmation result, retained even when a newer intent failed.
// new_intent: A newer explicit request supersedes older automatic completion attempts.
void record_storage_status(SettingsStorageStatus &status, esp_err_t error, bool new_intent = false)
{
    status.valid = error == ESP_OK;
    status.error = error;
    if (error == ESP_OK || new_intent) {
        ++status.revision;
        if (status.revision == 0) ++status.revision;
    }
}

// operation: Validated lifecycle operation whose field owns the intent revision.
// model: Canonical settings model protected by the caller's model lock.
// Returns null only for an unsupported operation, never a fallback field.
SettingsStorageStatus *lifecycle_storage_status(SettingsLifecycleOperation operation, SettingsModel &model)
{
    switch (operation) {
    case SettingsLifecycleOperation::ConsumeFirstBootMusic: return &model.storage.first_boot_music;
    case SettingsLifecycleOperation::CompleteBirthIntro: return &model.storage.birth_intro;
    case SettingsLifecycleOperation::CompleteSetup: return &model.storage.setup_progress;
    default: return nullptr;
    }
}
} // namespace

namespace SettingsState {
// settings: Complete successfully loaded and validated storage projection.
void publish_loaded(const OnboardingSettings &settings)
{
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    s_model.onboarding = settings;
    record_storage_status(s_model.storage.first_boot, ESP_OK);
    record_storage_status(s_model.storage.birth_intro, ESP_OK);
    record_storage_status(s_model.storage.first_boot_music, ESP_OK);
    record_storage_status(s_model.storage.tour_step, ESP_OK);
    record_storage_status(s_model.storage.setup_progress, ESP_OK);
    s_model.valid = true;
    s_model.last_error = ESP_OK;
    ++s_model.revision;
}

// error: Original initialization/read/default-write failure; never replaced by defaults.
void publish_load_failure(esp_err_t error)
{
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    s_model.valid = false;
    s_model.last_error = error;
    ++s_model.revision;
}

// field: Persisted field whose explicit write and readback just completed.
// value: Exact requested boolean or validated tour identity.
// error: Original write/readback result; failed writes leave confirmed values unchanged.
void publish_write(Field field, uint32_t value, esp_err_t error)
{
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    s_model.last_error = error;
    SettingsStorageStatus *status = nullptr;
    switch (field) {
    case Field::FirstBoot: status = &s_model.storage.first_boot; break;
    case Field::BirthIntro: status = &s_model.storage.birth_intro; break;
    case Field::FirstBootMusic: status = &s_model.storage.first_boot_music; break;
    case Field::TourStep: status = &s_model.storage.tour_step; break;
    case Field::SetupProgress: status = &s_model.storage.setup_progress; break;
    }
    // An explicit Replay may have reached NVS even when its readback fails.
    // It supersedes automatic completion of the preceding playback in either case.
    if (status != nullptr) record_storage_status(*status, error, field == Field::BirthIntro && value != 0);
    if (error == ESP_OK) {
        auto &settings = s_model.onboarding;
        switch (field) {
        case Field::FirstBoot:
            settings.persisted_first_boot = value != 0;
            settings.effective_first_boot = settings.persisted_first_boot || settings.force_first_boot;
            break;
        case Field::BirthIntro: settings.birth_intro_pending = value != 0; break;
        case Field::FirstBootMusic: settings.first_boot_music_pending = value != 0; break;
        case Field::TourStep: settings.onboarding_tour_step = static_cast<uint8_t>(value); break;
        case Field::SetupProgress: settings.setup_progress_pending = value != 0; break;
        }
    }
    ++s_model.revision;
}
} // namespace SettingsState

// result: Receives the optional output-policy result separately from required settings.
// Exception: Returns the original required storage, preference or publication failure.
esp_err_t bootstrap_device_settings(SettingsBootstrapResult &result)
{
    result = {};
    LUNASHELL_audio_begin_cody_output_policy();
    OnboardingSettings onboarding{};
    auto error = load_onboarding_settings(onboarding);
    if (error == ESP_OK) error = load_companion_preferences();
    if (error != ESP_OK) return error;
    result.audio_output_error = reconcile_audio_output_preference();
    return ESP_OK;
}

// model: Receives one coherent confirmed-value and command-state projection.
// Exception: Returns the original initialization error until the model is valid.
esp_err_t read_settings_model(SettingsModel &model)
{
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    model = s_model;
    return model.valid ? ESP_OK : model.last_error != ESP_OK ? model.last_error : ESP_ERR_INVALID_STATE;
}

// wake: Existing serialized action worker's notification callback; never a new task.
// Exception: Rejects a null callback or an attempt to replace its registered owner.
esp_err_t register_settings_lifecycle_worker(void (*wake)())
{
    if (wake == nullptr) return ESP_ERR_INVALID_ARG;
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    if (s_wake_worker != nullptr && s_wake_worker != wake) return ESP_ERR_INVALID_STATE;
    s_wake_worker = wake;
    return ESP_OK;
}

// operation: Explicit one-shot consumption or completion to persist asynchronously.
// sequence: Receives the nonzero admission identity on success, otherwise zero.
// expected_revision: Original field intent retained across retries; zero captures the current revision.
// Exception: Rejects unavailable settings, missing worker, duplicates in flight or exhausted IDs.
esp_err_t request_settings_lifecycle(SettingsLifecycleOperation operation, uint32_t &sequence,
    uint32_t expected_revision)
{
    sequence = 0;
    const std::size_t index = receipt_index(operation);
    if (index >= s_model.lifecycle.size()) return ESP_ERR_INVALID_ARG;
    void (*wake)() = nullptr;
    {
        const std::lock_guard<std::mutex> guard(s_model_mutex);
        if (!s_model.valid) return s_model.last_error != ESP_OK ? s_model.last_error : ESP_ERR_INVALID_STATE;
        if (s_wake_worker == nullptr) return ESP_ERR_INVALID_STATE;
        auto &receipt = s_model.lifecycle[index];
        if (in_flight(receipt.state)) return ESP_ERR_NOT_FINISHED;
        if (s_next_sequence == std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
        sequence = ++s_next_sequence;
        const auto *status = lifecycle_storage_status(operation, s_model);
        receipt = {sequence, operation, SettingsCommandState::Queued, ESP_ERR_NOT_FINISHED,
            expected_revision != 0 ? expected_revision : status->revision};
        ++s_model.revision;
        wake = s_wake_worker;
    }
    wake();
    return ESP_OK;
}

// operation: Lifecycle domain whose result the caller owns.
// sequence: Exact successful admission identity returned to that caller.
// receipt: Receives only the matching command's current state and original error.
// Exception: Rejects invalid identities or an already replaced receipt.
esp_err_t read_settings_lifecycle_receipt(SettingsLifecycleOperation operation, uint32_t sequence,
    SettingsLifecycleReceipt &receipt)
{
    const std::size_t index = receipt_index(operation);
    if (sequence == 0 || index >= s_model.lifecycle.size()) return ESP_ERR_INVALID_ARG;
    const std::lock_guard<std::mutex> guard(s_model_mutex);
    const auto &observed = s_model.lifecycle[index];
    if (observed.sequence != sequence || observed.operation != operation) return ESP_ERR_NOT_FOUND;
    receipt = observed;
    return ESP_OK;
}

// No parameters: called only by the registered serialized action worker.
// Returns true after processing one queued lifecycle command; false means no work.
// Storage waits never hold the model mutex or execute on the display worker.
bool process_settings_lifecycle_command()
{
    SettingsLifecycleReceipt command{};
    {
        const std::lock_guard<std::mutex> guard(s_model_mutex);
        for (auto &receipt : s_model.lifecycle) {
            if (receipt.state != SettingsCommandState::Queued) continue;
            const auto *status = lifecycle_storage_status(receipt.operation, s_model);
            if (status == nullptr || status->revision != receipt.expected_revision) {
                receipt.state = SettingsCommandState::Superseded;
                receipt.error = ESP_ERR_INVALID_STATE;
                ++s_model.revision;
                return true;
            }
            receipt.state = SettingsCommandState::Running;
            command = receipt;
            ++s_model.revision;
            break;
        }
    }
    if (command.sequence == 0) return false;
    // The action worker serializes every runtime writer of these fields. Bootstrap
    // finishes before worker registration, so no newer intent can pass this guard
    // and then overtake the write after the short model lock has been released.
    esp_err_t error = ESP_ERR_INVALID_ARG;
    switch (command.operation) {
    case SettingsLifecycleOperation::ConsumeFirstBootMusic:
        error = persist_first_boot_music_pending(false);
        break;
    case SettingsLifecycleOperation::CompleteBirthIntro:
        error = persist_birth_intro_pending(false);
        break;
    case SettingsLifecycleOperation::CompleteSetup:
        error = persist_setup_progress_pending(false);
        break;
    default: break;
    }
    {
        const std::lock_guard<std::mutex> guard(s_model_mutex);
        auto &receipt = s_model.lifecycle[receipt_index(command.operation)];
        // In-flight receipts cannot be replaced, including when a UI route retires.
        receipt.error = error;
        receipt.state = error == ESP_OK ? SettingsCommandState::Succeeded : SettingsCommandState::Failed;
        ++s_model.revision;
    }
    return true;
}

} // namespace CODY
