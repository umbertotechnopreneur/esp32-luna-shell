/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_settings_model.hpp"

namespace CODY {

struct SettingsBootstrapResult {
    esp_err_t audio_output_error = ESP_ERR_NOT_FINISHED;
};

// result: Receives the optional output-policy result separately from required settings.
// Exception: Returns the original required storage, preference or publication failure.
esp_err_t bootstrap_device_settings(SettingsBootstrapResult &result);

// model: Receives one coherent confirmed-value and command-state projection.
// Exception: Returns the original initialization error until the model is valid.
esp_err_t read_settings_model(SettingsModel &model);

// wake: Existing serialized action worker's notification callback; never a new task.
// Exception: Rejects a null callback or an attempt to replace its registered owner.
esp_err_t register_settings_lifecycle_worker(void (*wake)());

// operation: Explicit one-shot consumption or completion to persist asynchronously.
// sequence: Receives the nonzero admission identity on success, otherwise zero.
// expected_revision: Original field intent retained across retries; zero captures the current revision.
// Exception: Rejects unavailable settings, missing worker, duplicates in flight or exhausted IDs.
// A newer field revision produces a Superseded receipt without writing storage.
esp_err_t request_settings_lifecycle(SettingsLifecycleOperation operation, uint32_t &sequence,
    uint32_t expected_revision = 0);

// operation: Lifecycle domain whose result the caller owns.
// sequence: Exact successful admission identity returned to that caller.
// receipt: Receives only the matching command's current state and original error.
// Exception: Rejects invalid identities or an already replaced receipt.
esp_err_t read_settings_lifecycle_receipt(SettingsLifecycleOperation operation, uint32_t sequence,
    SettingsLifecycleReceipt &receipt);

// No parameters: called only by the registered serialized action worker.
// Returns true after processing one queued lifecycle command; false means no work.
// Storage waits never hold the model mutex or execute on the display worker.
bool process_settings_lifecycle_command();

} // namespace CODY
