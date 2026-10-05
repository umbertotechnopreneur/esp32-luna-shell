#pragma once

#include "CODY_personality.hpp"
#include "CODY_response.hpp"

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
    IdleSceneId idle_scene = IdleSceneId::None;
};

// No parameters: passes immutable YAML policy to the optional owner without sound or codec I/O.
// Exception: Returns the original preparation error, retained and canonically published by its owner.
esp_err_t prepare_personality_audio();
// cue: Newly visible retired symbol; its YAML sfx_enabled gate and fixed asset eligibility both apply.
// status: Receives explicit dispatch/drop/suppression disposition; completion remains asynchronous.
// Exception: Returns an invalid cue or retained owner error; no deferred retry is created.
esp_err_t notify_personality_audio(PersonalityCue cue, PersonalityAudioStatus &status);
// status: Receives one whistle admission outcome; existing quiet/volume policy applies.
// Exception: Returns the original optional audio owner error without retrying.
esp_err_t notify_idle_whistle_audio(PersonalityAudioStatus &status);
// suspended: Cancels pending symbols when their scene hides or display parks.
// Exception: Returns the original retained owner error.
esp_err_t set_personality_audio_suspended(bool suspended);
// enabled: Explicit session gate, independent of visibility; cannot override YAML disabled policy.
// Exception: Returns the original retained owner error.
esp_err_t set_personality_audio_enabled(bool enabled);
// status: Receives the coherent retained completion/outcome and original error.
// Exception: Returns the original retained owner error.
esp_err_t get_personality_audio_status(PersonalityAudioStatus &status);

} // namespace CODY
