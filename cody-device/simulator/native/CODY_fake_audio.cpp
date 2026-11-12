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

// Host audio output owner. The real Cody cue modules are linked unchanged.
#include "CODY_personality_audio.hpp"
#include "CODY_typewriter_audio.hpp"
#include "LUNASHELL_platform.h"
#include "esp_log.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <mutex>

#if defined(_WIN32)
#define CODY_AUDIO_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_AUDIO_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
using Playback = esp_err_t (*)(const int16_t *, size_t, uint32_t, int);
Playback s_playback = nullptr;
const int16_t *s_tap = nullptr;
size_t s_tap_count = 0;
uint32_t s_tap_rate = 0;
bool s_typewriter_suspended = false;
bool s_personality_suspended = false;
lunashell_personality_audio_policy_t s_policy{};
std::mutex s_status_mutex;
std::array<lunashell_audio_cue_status_t, 16> s_status_records{};
uint32_t s_next_sequence = 0;
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

// request: Real PCM asset and channel passed by Cody's unified cue selector.
// status: Receives synthetic Windows admission, never physical-device evidence.
// Exception: Returns invalid arguments or the original backend failure.
extern "C" esp_err_t LUNASHELL_audio_submit(const lunashell_audio_request_t *request,
    lunashell_audio_cue_status_t *status)
{
    if (!request || !status || request->sample_count == 0 || request->sample_rate_hz == 0 ||
        request->cue_id == 0 || !s_playback) {
        ESP_LOGE("CodyFakeAudio", "submit rejected: invalid PCM/backend/status");
        return ESP_ERR_INVALID_ARG;
    }
    // The Windows callback accepts one PCM clip, not synthesized voices or sequences.
    if (request->wave != LUNASHELL_AUDIO_WAVE_PCM || request->channel != LUNASHELL_AUDIO_PERSONALITY ||
        request->repetitions != 1 || request->gap_ms != 0) {
        *status = {LUNASHELL_CUE_UNAVAILABLE, ESP_ERR_NOT_SUPPORTED, 0, request->cue_id};
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!request->samples) {
        *status = {LUNASHELL_CUE_FAILED, ESP_ERR_INVALID_ARG, 0, request->cue_id};
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_policy.enabled || s_personality_suspended) {
        *status = {LUNASHELL_CUE_SUPPRESSED, ESP_OK, 0, request->cue_id};
        return ESP_OK;
    }
    std::lock_guard<std::mutex> guard(s_status_mutex);
    if (s_next_sequence == std::numeric_limits<uint32_t>::max()) {
        *status = {LUNASHELL_CUE_FAILED, ESP_ERR_INVALID_STATE, 0, request->cue_id};
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = s_playback(request->samples, request->sample_count, request->sample_rate_hz, 1);
    if (error != ESP_OK) {
        *status = {LUNASHELL_CUE_FAILED, error, 0, request->cue_id};
        return error;
    }
    const uint32_t sequence = ++s_next_sequence;
    auto &record = s_status_records[(sequence - 1U) % s_status_records.size()];
    // Windows reports queue acceptance only; no audible completion is inferred.
    record = {LUNASHELL_CUE_DISPATCHED, ESP_OK, sequence, request->cue_id};
    *status = record;
    return ESP_OK;
}

// suspended: Windows scene visibility gate for symbol/whistle requests.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    s_personality_suspended = suspended;
    return ESP_OK;
}

// sequence: Exact simulated request sequence.
// cue_id: Identity issued with that sequence.
// status: Receives the fake owner's retained admission result.
// Exception: Returns invalid identity, missing request or retained backend failure.
extern "C" esp_err_t LUNASHELL_audio_get_cue_status(uint32_t sequence, uint32_t cue_id,
    lunashell_audio_cue_status_t *status)
{
    if (!status || sequence == 0 || cue_id == 0) {
        ESP_LOGE("CodyFakeAudio", "get cue status rejected: invalid identity or destination");
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> guard(s_status_mutex);
    const auto &record = s_status_records[(sequence - 1U) % s_status_records.size()];
    if (sequence != record.request_sequence || cue_id != record.cue_id) return ESP_ERR_NOT_FOUND;
    *status = record;
    return record.last_error;
}

// playback: Windows callback; copies PCM immediately and queues PC playback.
// Exception: Returns the original real-module preparation failure.
CODY_AUDIO_EXPORT esp_err_t CodySimAudio_prepare(Playback playback)
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
CODY_AUDIO_EXPORT esp_err_t CodySimAudio_personality(uint32_t cue)
{
    CODY::PersonalityAudioStatus status{};
    return CODY::notify_personality_audio(static_cast<CODY::PersonalityCue>(cue), status);
}

// No parameters: executes Cody's actual authored whistle request.
// Exception: Returns the original Cody/Windows admission failure.
CODY_AUDIO_EXPORT esp_err_t CodySimAudio_whistle()
{
    CODY::PersonalityAudioStatus status{};
    return CODY::notify_idle_whistle_audio(status);
}

// No parameters: requests one physical-touch feedback click from the fake owner.
// Exception: Returns the original owner admission failure.
CODY_AUDIO_EXPORT esp_err_t CodySimAudio_touch()
{
    return LUNASHELL_audio_notify_touch();
}

// count: Newly visible glyph count from a completed preview frame.
// Exception: Returns the unchanged Cody module's original admission failure.
CODY_AUDIO_EXPORT esp_err_t CodySimAudio_typewriter(uint32_t count)
{
    return CODY::notify_typewriter_audio(count);
}
