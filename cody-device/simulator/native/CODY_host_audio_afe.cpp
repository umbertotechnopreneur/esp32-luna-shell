#include "LUNASHELL_platform.h"
#include "LUNASHELL_voice_input.hpp"
#include <mutex>

// No parameters: the explicit host AFE backend is unavailable, not a physical observation.
extern "C" bool LUNASHELL_voice_afe_running(void) { return false; }

// No parameters: no simulated AFE consumes microphone samples on the host.
extern "C" bool LUNASHELL_voice_afe_capture_active(void) { return false; }

// samples: Borrowed speaker reference PCM from the real audio owner.
// count: Number of source samples.
// rate: Source sample rate in Hz.
// Exception: No host AFE exists to accept a reference signal.
extern "C" esp_err_t LUNASHELL_voice_afe_push_speaker_pcm16(const int16_t *samples,
    size_t count, uint32_t rate)
{
    (void)samples; (void)count; (void)rate;
    return ESP_ERR_NOT_SUPPORTED;
}

namespace LUNASHELL {
// No parameters: the host has one process-lifetime capture exclusion owner.
std::mutex &voice_capture_guard()
{
    static std::mutex guard;
    return guard;
}

// available: Native UI route admission; no host microphone session exists to retain.
// Exception: None; the return value reports that no session needs its controls.
bool setVoiceInputRouteAvailable(bool available)
{
    (void)available;
    return false;
}

// trigger: Original native request identity.
// started: Optional output; no simulated host capture is admitted.
// Exception: Returns NOT_SUPPORTED and never fabricates microphone or LAN state.
esp_err_t requestVoiceInput(VoiceInputTrigger trigger, bool *started)
{
    (void)trigger;
    if (started) *started = false;
    return ESP_ERR_NOT_SUPPORTED;
}

// expected_started_at_us: Unavailable session identity from a native input gesture.
// accepted: Receives false because no host capture owner exists.
// Exception: Returns NOT_SUPPORTED without creating a countdown.
esp_err_t beginVoiceInputCountdown(int64_t expected_started_at_us, bool &accepted)
{
    (void)expected_started_at_us;
    accepted = false;
    return ESP_ERR_NOT_SUPPORTED;
}

// expected_started_at_us: Unavailable session identity from a native input gesture.
// accepted: Receives false; no audio is transmitted.
// Exception: Returns NOT_SUPPORTED without claiming delivery.
esp_err_t finishVoiceInput(int64_t expected_started_at_us, bool &accepted)
{
    (void)expected_started_at_us;
    accepted = false;
    return ESP_ERR_NOT_SUPPORTED;
}

// expected_started_at_us: Unavailable session identity from a native input gesture.
// accepted: Receives false; there is no host capture to cancel.
// Exception: Returns NOT_SUPPORTED without claiming cancellation.
esp_err_t cancelVoiceInput(int64_t expected_started_at_us, bool &accepted)
{
    (void)expected_started_at_us;
    accepted = false;
    return ESP_ERR_NOT_SUPPORTED;
}
}
