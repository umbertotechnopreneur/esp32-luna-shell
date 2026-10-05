// Windows-only audio output owner. The real Cody cue modules are linked unchanged.
#include "CODY_personality_audio.hpp"
#include "CODY_typewriter_audio.hpp"
#include "LUNASHELL_platform.h"
#include "esp_log.h"

#include <cstddef>
#include <cstdint>

namespace {
using Playback = esp_err_t (*)(const int16_t *, size_t, uint32_t, int);
Playback s_playback = nullptr;
const int16_t *s_tap = nullptr;
size_t s_tap_count = 0;
uint32_t s_tap_rate = 0;
bool s_typewriter_suspended = false;
bool s_personality_suspended = false;
lunashell_personality_audio_policy_t s_policy{};
lunashell_audio_cue_status_t s_status{};
}

// samples: Immutable PCM provided by the real Cody module.
// sample_count: Number of registered mono samples.
// sample_rate_hz: Authored sample rate.
// Exception: Returns invalid registration arguments.
extern "C" esp_err_t LUNASHELL_audio_prepare_typewriter(const int16_t *samples,
    size_t sample_count, uint32_t sample_rate_hz)
{
    if (!samples || sample_count == 0 || sample_rate_hz == 0) {
        ESP_LOGE("CodyFakeAudio", "prepare typewriter rejected: invalid PCM registration");
        return ESP_ERR_INVALID_ARG;
    }
    s_tap = samples;
    s_tap_count = sample_count;
    s_tap_rate = sample_rate_hz;
    return ESP_OK;
}

// count: Newly visible glyph count; zero requests no audio.
// Exception: Returns a missing backend or the original Windows admission failure.
extern "C" esp_err_t LUNASHELL_audio_notify_typewriter(uint32_t count)
{
    if (count == 0 || s_typewriter_suspended) {
        return ESP_OK;
    }
    return s_playback && s_tap ? s_playback(s_tap, s_tap_count, s_tap_rate, 1) : ESP_ERR_INVALID_STATE;
}

// No parameters: the same impulse/gain used by firmware physical-touch feedback.
// Exception: Returns an unavailable backend or its original admission failure.
extern "C" esp_err_t LUNASHELL_audio_notify_touch()
{
    return s_playback && s_tap ? s_playback(s_tap, s_tap_count, s_tap_rate, 8) : ESP_ERR_INVALID_STATE;
}

// suspended: Scene text gate; physical taps remain independent.
extern "C" esp_err_t LUNASHELL_audio_set_typewriter_suspended(bool suspended)
{
    s_typewriter_suspended = suspended;
    return ESP_OK;
}

// policy: Actual immutable Cody authoring policy.
// Exception: Returns invalid policy arguments.
extern "C" esp_err_t LUNASHELL_audio_prepare_personality(const lunashell_personality_audio_policy_t *policy)
{
    if (!policy) {
        ESP_LOGE("CodyFakeAudio", "prepare personality rejected: missing policy");
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    return ESP_OK;
}

// samples: Real flash asset passed by the unchanged Cody cue selector.
// sample_count: Exact mono sample count.
// sample_rate_hz: Authored sample rate.
// cue_id: Stable application identity, including the idle whistle.
// status: Receives synthetic Windows admission, never physical-device evidence.
// Exception: Returns invalid arguments or the original backend failure.
extern "C" esp_err_t LUNASHELL_audio_notify_personality(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, uint32_t cue_id, lunashell_audio_cue_status_t *status)
{
    if (!status || !samples || sample_count == 0 || sample_rate_hz == 0 || !s_playback) {
        ESP_LOGE("CodyFakeAudio", "notify personality cue=%u rejected: invalid PCM/backend/status", cue_id);
        return ESP_ERR_INVALID_ARG;
    }
    s_status.cue_id = cue_id;
    if (!s_policy.enabled || s_personality_suspended) {
        s_status.state = LUNASHELL_CUE_SUPPRESSED;
    } else {
        ++s_status.request_sequence;
        s_status.last_error = s_playback(samples, sample_count, sample_rate_hz, 1);
        s_status.state = s_status.last_error == ESP_OK ? LUNASHELL_CUE_DISPATCHED : LUNASHELL_CUE_FAILED;
    }
    *status = s_status;
    return s_status.last_error;
}

// suspended: Windows scene visibility gate for symbol/whistle requests.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    s_personality_suspended = suspended;
    return s_status.last_error;
}

// status: Receives the fake owner's retained admission result.
// Exception: Returns invalid destination or retained backend failure.
extern "C" esp_err_t LUNASHELL_audio_get_personality_status(lunashell_audio_cue_status_t *status)
{
    if (!status) {
        ESP_LOGE("CodyFakeAudio", "get personality status rejected: missing destination");
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_status;
    return s_status.last_error;
}

// playback: Windows callback; copies PCM immediately and queues PC playback.
// Exception: Returns the original real-module preparation failure.
extern "C" __declspec(dllexport) esp_err_t CodySimAudio_prepare(Playback playback)
{
    if (!playback) {
        ESP_LOGE("CodyFakeAudio", "prepare rejected: missing Windows playback callback");
        return ESP_ERR_INVALID_ARG;
    }
    s_playback = playback;
    const esp_err_t error = CODY::prepare_typewriter_audio();
    return error == ESP_OK ? CODY::prepare_personality_audio() : error;
}

// cue: Stable personality identity selected by the existing visual controller.
// Exception: Returns the unchanged Cody selector's original error.
extern "C" __declspec(dllexport) esp_err_t CodySimAudio_personality(uint32_t cue)
{
    CODY::PersonalityAudioStatus status{};
    return CODY::notify_personality_audio(static_cast<CODY::PersonalityCue>(cue), status);
}

// No parameters: executes Cody's actual authored whistle request.
// Exception: Returns the original Cody/Windows admission failure.
extern "C" __declspec(dllexport) esp_err_t CodySimAudio_whistle()
{
    CODY::PersonalityAudioStatus status{};
    return CODY::notify_idle_whistle_audio(status);
}

// No parameters: requests one physical-touch feedback click from the fake owner.
// Exception: Returns the original owner admission failure.
extern "C" __declspec(dllexport) esp_err_t CodySimAudio_touch()
{
    return LUNASHELL_audio_notify_touch();
}

// count: Newly visible glyph count from a completed preview frame.
// Exception: Returns the unchanged Cody module's original admission failure.
extern "C" __declspec(dllexport) esp_err_t CodySimAudio_typewriter(uint32_t count)
{
    return CODY::notify_typewriter_audio(count);
}
