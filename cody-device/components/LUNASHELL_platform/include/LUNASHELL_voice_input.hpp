#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "LUNASHELL_voice_poll_policy.hpp"

namespace LUNASHELL {

enum class VoiceInputPhase : uint8_t {
    Idle, Listening, Sending, Cancelling, Received, Delivered, Cancelled, Unconfirmed, Failed,
    Preparing, StartCountdown, SendCountdown, AwaitingPc
};

// phase: Owner-published phase that still needs capture or delivery service.
constexpr bool voice_input_phase_active(VoiceInputPhase phase)
{
    using Phase = VoiceInputPhase;
    return phase == Phase::Preparing || phase == Phase::StartCountdown || phase == Phase::AwaitingPc ||
        phase == Phase::Listening || phase == Phase::SendCountdown || phase == Phase::Sending ||
        phase == Phase::Cancelling || phase == Phase::Received;
}

// phase: Owner-published phase eligible for an explicit pre-END cancellation.
constexpr bool voice_input_phase_cancellable(VoiceInputPhase phase)
{
    using Phase = VoiceInputPhase;
    return phase == Phase::Preparing || phase == Phase::StartCountdown || phase == Phase::AwaitingPc ||
        phase == Phase::Listening || phase == Phase::SendCountdown || phase == Phase::Sending;
}
enum class VoiceInputTrigger : uint8_t { WakeWord, DoubleTap, StandbyWake, DeviceButton };
enum class VoiceInputFailure : uint8_t { None, Microphone, Transport, PcRejected, Destination };

inline constexpr size_t kVoiceDestinationTitleCapacity = 23;

struct VoiceInputDestinationObservation {
    char chat_id[65]{}; // Authenticated SHA-256 hex digest of the followed chat.
    char title[kVoiceDestinationTitleCapacity]{};
};
static_assert(sizeof(VoiceInputDestinationObservation) <= 88, "Keep the destination projection bounded");
struct VoiceInputObservation {
    VoiceInputPhase phase = VoiceInputPhase::Idle;
    VoiceInputTrigger trigger = VoiceInputTrigger::WakeWord;
    uint8_t countdown_seconds = 0;
    VoiceInputFailure failure = VoiceInputFailure::None;
    uint32_t captured_samples = 0;
    uint32_t acknowledged_samples = 0;
    esp_err_t error = ESP_OK;
    int64_t started_at_us = 0;
    int64_t capturing_started_at_us = 0;
    int64_t changed_at_us = 0;
    char destination_title[kVoiceDestinationTitleCapacity]{}; // Frozen display title or empty while resolving the destination.
    bool end_sent = false; // The complete END datagram left Cody. Native admission is separate.
};
static_assert(sizeof(VoiceInputObservation) <= 64, "Voice projection must stay within its bounded caller budget");

// Admission uses a single current policy/route snapshot without copying radio or audio history.
struct VoiceInputAdmissionObservation {
    esp_err_t dictation_error = ESP_ERR_INVALID_STATE;
    bool dictation_valid = false;
    bool dictation_enabled = false;
    bool lan_ready = false;
};
static_assert(sizeof(VoiceInputAdmissionObservation) <= 8, "Voice admission must remain a scalar projection");

// trigger: Explicit recognition or acquired local touch control.
// started: Optional result, true only after this request successfully publishes Preparing.
// Exception: Returns original errors. An unavailable UI route, Dictation Off or a local
// touch without the LAN route is an intentional ESP_OK no-op with started=false.
esp_err_t requestVoiceInput(VoiceInputTrigger trigger, bool *started = nullptr);
// available: The display owner has retired a route that admits new dictation.
// Returns whether a voice session still needs its controls; it does not cancel that session.
bool setVoiceInputRouteAvailable(bool available);
// expected_started_at_us: Original identity from the retired Preparing screen showing the destination.
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
// refresh_destination: Receives a one-time request to advance the existing CCP discovery poll.
// Returns whether this owner still needs the bounded 5-ms audio service cadence (at least one tick).
bool tickVoiceInputTransport(const uint8_t *link_key, const char *device_id,
                            const char *peer_ip, const char *presence_nonce,
                            bool connected, bool &refresh_destination);
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
