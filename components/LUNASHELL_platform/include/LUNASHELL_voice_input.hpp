#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "LUNASHELL_voice_poll_policy.hpp"

namespace LUNASHELL {

enum class VoiceInputPhase : uint8_t {
    Idle, Listening, Sending, Cancelling, Received, Delivered, Cancelled, Unconfirmed, Failed,
    Preparing, StartCountdown, SendCountdown
};
enum class VoiceInputTrigger : uint8_t { WakeWord, DoubleTap, StandbyWake };
struct VoiceInputObservation {
    VoiceInputPhase phase = VoiceInputPhase::Idle;
    VoiceInputTrigger trigger = VoiceInputTrigger::WakeWord;
    uint8_t countdown_seconds = 0;
    uint32_t captured_samples = 0;
    uint32_t acknowledged_samples = 0;
    int64_t started_at_us = 0;
    int64_t capturing_started_at_us = 0;
    int64_t changed_at_us = 0;
    esp_err_t error = ESP_OK;
};
static_assert(sizeof(VoiceInputObservation) <= 48, "Voice projection must stay within its bounded caller budget");

// trigger: Explicit recognition or acquired double-tap event.
// Returns the original admission/allocation/publication error; duplicate capture is rejected.
esp_err_t requestVoiceInput(VoiceInputTrigger trigger);
// expected_started_at_us: Original session identity from the completely retired Preparing screen.
// accepted: True only when that still-current session enters the three-second start countdown.
// Exception: Stale/terminal requests return ESP_OK with accepted=false; publication errors stay real.
esp_err_t beginVoiceInputCountdown(int64_t expected_started_at_us, bool &accepted);
// samples: Mono enhanced PCM owned by the existing AFE fetch worker.
// count: Actual int16 samples in this successful result.
// speech: Actual AFE VAD result, without a second microphone owner.
// error: Original acquisition/inference error, if any.
void feedVoiceInput(const int16_t *samples, size_t count, bool speech, esp_err_t error);
// link_key: Retained 32-byte key, borrowed only by the serialized CCP worker.
// device_id: Confirmed 12-character device identity.
// peer_ip: Fresh authenticated READY source.
// presence_nonce: Fresh authenticated CCP discovery nonce, binding START against replay.
// connected: Whether that retained peer currently proves liveness.
// Returns whether this owner still needs the bounded 5-ms audio service cadence (at least one tick).
bool tickVoiceInputTransport(const uint8_t *link_key, const char *device_id,
                            const char *peer_ip, const char *presence_nonce, bool connected);
// No parameters: retires the active utterance as an explicit local cancellation.
esp_err_t cancelVoiceInput();
// expected_started_at_us: Session identity from the displayed canonical observation.
// accepted: True only after the current captured audio was admitted for sending.
// Exception: Stale/already-ended/no-PCM intents are ESP_OK with accepted=false;
// real publication failures retain their error and are returned unchanged.
esp_err_t finishVoiceInput(int64_t expected_started_at_us, bool &accepted);
// expected_started_at_us: Session identity from the displayed canonical observation.
// accepted: True only after local cancellation or protocol CANCEL was admitted.
// Exception: Stale/post-END intents are ESP_OK with accepted=false; real owner
// errors are returned. Admission never claims remote audio was already discarded.
esp_err_t cancelVoiceInput(int64_t expected_started_at_us, bool &accepted);

// No parameters: the acquisition owner ends active input before pausing the microphone.
// Exception: Returns the original canonical publication failure.
esp_err_t pauseVoiceInput();
}
