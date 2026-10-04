#pragma once

#include "CODY_personality.hpp"

namespace CODY {

enum class PersonalityAudioState : uint8_t {
    Idle = 0, Dispatched = 1, Playing = 2, Completed = 3, DroppedBusy = 4,
    DroppedCooldown = 5, Suppressed = 6, Expired = 7, Failed = 8, Unavailable = 9,
};

struct PersonalityAudioStatus {
    PersonalityAudioState state = PersonalityAudioState::Idle;
    esp_err_t last_error = ESP_OK;
    uint32_t request_sequence = 0;
    PersonalityCue cue = PersonalityCue::None;
};

// No parameters: prepares the optional owner worker without starting sound or codec I/O.
// Exception: Returns the original preparation error, retained and canonically published by its owner.
esp_err_t prepare_personality_audio();
// cue: One newly visible symbol in a successfully retired frame, never an evaluated-only cue.
// status: Receives explicit dispatch/drop/suppression disposition; completion remains asynchronous.
// Exception: Returns an invalid cue or retained owner error; no deferred retry is created.
esp_err_t notify_personality_audio(PersonalityCue cue, PersonalityAudioStatus &status);
// suspended: Cancels pending symbols when their scene hides or display parks.
// Exception: Returns the original retained owner error.
esp_err_t set_personality_audio_suspended(bool suspended);
// enabled: Explicit global sound policy, independent of symbol visibility.
// Exception: Returns the original retained owner error.
esp_err_t set_personality_audio_enabled(bool enabled);
// status: Receives the coherent retained completion/outcome and original error.
// Exception: Returns the original retained owner error.
esp_err_t get_personality_audio_status(PersonalityAudioStatus &status);

} // namespace CODY
