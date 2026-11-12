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
 * Original file notice:
 *
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 *
 * VBWR E */


#include "LUNASHELL_platform.h"
#include "LUNASHELL_audio_controls.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"
#include <atomic>

namespace {
std::atomic<uint32_t> s_user_sounds_state{1U};
std::atomic_bool s_cody_output_policy_armed{false};
std::atomic<esp_err_t> s_cody_output_policy_error{ESP_OK};
}

// No parameters: preserve the startup gate even on a build without audio hardware.
extern "C" void LUNASHELL_audio_begin_cody_output_policy(void)
{
    s_cody_output_policy_error.store(ESP_ERR_NOT_FINISHED, std::memory_order_relaxed);
    s_cody_output_policy_armed.store(true, std::memory_order_release);
}

// error: Original output reconciliation or publication result.
extern "C" void LUNASHELL_audio_update_cody_output_policy(esp_err_t error)
{
    if (!s_cody_output_policy_armed.load(std::memory_order_acquire) ||
        s_cody_output_policy_error.load(std::memory_order_acquire) == ESP_OK) return;
    s_cody_output_policy_error.store(error, std::memory_order_release);
}

// enabled: Explicit admission preference, independent of whether this profile has an audio device.
// No worker, codec, microphone, volume or persistence operation is attempted.
extern "C" esp_err_t LUNASHELL_audio_set_user_sounds_enabled(bool enabled)
{
    uint32_t observed = s_user_sounds_state.load(std::memory_order_acquire);
    for (;;) {
        if (((observed & 1U) != 0U) == enabled) {
            return ESP_OK;
        }
        const uint32_t revision = (observed + 2U) & ~1U;
        const uint32_t desired = revision | (enabled ? 1U : 0U);
        if (s_user_sounds_state.compare_exchange_weak(observed, desired, std::memory_order_acq_rel)) {
            return ESP_OK;
        }
    }
}

// No parameters: observes the explicit gate without claiming that an audio route exists.
extern "C" bool LUNASHELL_audio_user_sounds_enabled(void)
{
    return (s_user_sounds_state.load(std::memory_order_acquire) & 1U) != 0U;
}

// No parameters: revision invalidating pending logical cues after any preference change.
extern "C" uint32_t LUNASHELL_audio_user_sounds_revision(void)
{
    return s_user_sounds_state.load(std::memory_order_acquire);
}

// samples: Borrowed mono PCM16 for a route excluded by this build.
// sample_count: Exact authored sample count.
// sample_rate_hz: Authored PCM clock.
// volume_percent: Requested codec volume.
// user_sounds_revision: Logical request revision, invalidated by Sounds Off/On.
// completed: Always false; an unavailable route or deliberate suppression cannot complete playback.
// Exception: Returns invalid arguments or actual route unavailability when playback was requested.
extern "C" esp_err_t LUNASHELL_audio_play_user_cue_pcm16(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, int volume_percent, uint32_t user_sounds_revision, bool *completed)
{
    if (completed != nullptr) {
        *completed = false;
    }
    const bool invalid = completed == nullptr || samples == nullptr || sample_count == 0 ||
        sample_rate_hz < 50 || volume_percent < 0 || volume_percent > 100;
    if (!invalid && ((user_sounds_revision & 1U) == 0U ||
            user_sounds_revision != s_user_sounds_state.load(std::memory_order_acquire))) {
        return ESP_OK;
    }
    const esp_err_t error = invalid ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
    ESP_LOGE("LUNASHELL_audio", "User cue rejected by absent audio route (%s)", esp_err_to_name(error));
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, sample_rate_hz, volume_percent, error);
    if (publication_error != ESP_OK) {
        ESP_LOGE("LUNASHELL_audio", "User cue failure publication failed (%s)", esp_err_to_name(publication_error));
    }
    return error;
}

// samples: Borrowed PCM for a route absent from this build profile.
// sample_count: Number of mono PCM16 samples.
// sample_rate_hz: Requested codec rate.
// volume_percent: Requested output volume.
extern "C" esp_err_t LUNASHELL_audio_set_startup_cue(const int16_t *samples,
    size_t sample_count, uint32_t sample_rate_hz, int volume_percent)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)volume_percent;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" uint8_t LUNASHELL_audio_input_channel_count(void)
{
    return 0;
}

extern "C" esp_err_t LUNASHELL_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)volume_percent;
    LUNASHELL_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    (void)gain_db;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_capture_pcm16_channels(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count,
    uint32_t sample_rate_hz,
    float gain_db
)
{
    (void)samples;
    (void)frame_count;
    (void)channel_count;
    (void)sample_rate_hz;
    (void)gain_db;
    return ESP_ERR_NOT_SUPPORTED;
}

// percent: Requested output volume for an unavailable output route.
// applied: Output is unchanged because no codec readback exists.
// Exception: Returns invalid arguments or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_set_user_output_volume(uint8_t percent, uint8_t *applied)
{
    return applied == nullptr || percent > 100 ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// applied: Output is unchanged because no codec readback exists.
// Exception: Returns invalid argument or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_read_user_output_volume(uint8_t *applied)
{
    return applied == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// gain_db: Requested ES7210 gain for an unavailable input route.
// applied: Output is unchanged because no codec readback exists.
// Exception: Returns invalid arguments or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_set_user_input_gain(uint8_t gain_db, uint8_t *applied)
{
    return applied == nullptr || gain_db > 30 || gain_db % 3 != 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// applied: Output is unchanged because no codec readback exists.
// Exception: Returns invalid argument or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_read_user_input_gain(uint8_t *applied)
{
    return applied == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// samples: Caller-owned destination for a route absent from this build.
// frame_count: Requested frames per channel.
// channel_count: Requested microphone topology.
// sample_rate_hz: Requested transport rate.
// Exception: Returns the selected-route error without fabricating samples.
extern "C" esp_err_t LUNASHELL_audio_capture_user_levels_pcm16(int16_t *samples, size_t frame_count,
    uint8_t channel_count, uint32_t sample_rate_hz)
{
    (void)samples;
    (void)frame_count;
    (void)channel_count;
    (void)sample_rate_hz;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_set_standby(bool standby)
{
    (void)standby;
    return ESP_ERR_NOT_SUPPORTED;
}

// samples: Immutable PCM for a route absent from this selected profile.
// sample_count: Authored mono sample count.
// sample_rate_hz: Authored PCM rate.
// Exception: Reports the absent output route without claiming playback.
extern "C" esp_err_t LUNASHELL_audio_prepare_typewriter(const int16_t *samples,
    size_t sample_count, uint32_t sample_rate_hz)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    ESP_LOGE("LUNASHELL_audio", "Typewriter preparation unavailable for the selected profile (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// newly_visible_characters: Retired-frame progress on a profile without audio.
// Exception: A positive request retains the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_notify_typewriter(uint32_t newly_visible_characters)
{
    if (newly_visible_characters == 0) {
        return ESP_OK; // No selected-route operation was requested.
    }
    ESP_LOGE("LUNASHELL_audio", "Typewriter notification unavailable for the selected profile (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// suspended: Requested cue gate for a profile without an audio worker.
// Exception: Returns the selected-route error without a fictitious worker.
extern "C" esp_err_t LUNASHELL_audio_set_typewriter_suspended(bool suspended)
{
    (void)suspended;
    ESP_LOGE("LUNASHELL_audio", "Typewriter suspension unavailable for the selected profile (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// policy: Build-authored settings for an explicitly unavailable selected output route.
// Exception: Returns rejected policy or selected-route error and publishes that optional failure.
extern "C" esp_err_t LUNASHELL_audio_prepare_personality(const lunashell_personality_audio_policy_t *policy)
{
    if (policy == nullptr || policy->cooldown_us < 0 || policy->request_expiry_us <= 0) {
        ESP_LOGE("LUNASHELL_audio", "Personality preparation policy rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
            false, 0, 16000, 0, ESP_ERR_INVALID_ARG);
        if (publication_error != ESP_OK) {
            ESP_LOGE("LUNASHELL_audio", "Personality policy failure publication failed (%s)",
                esp_err_to_name(publication_error));
        }
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGE("LUNASHELL_audio", "Personality audio unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, 16000, 0, ESP_ERR_NOT_SUPPORTED);
    if (publication_error != ESP_OK) {
        ESP_LOGE("LUNASHELL_audio", "Personality failure publication failed (%s)", esp_err_to_name(publication_error));
    }
    return ESP_ERR_NOT_SUPPORTED;
}

// request: PCM or generated cue requested without an output device.
// status: Receives actual route unavailability without claiming admission.
// Exception: Returns invalid arguments or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_submit(const lunashell_audio_request_t *request,
    lunashell_audio_cue_status_t *status)
{
    if (request == nullptr || status == nullptr) return ESP_ERR_INVALID_ARG;
    *status = {LUNASHELL_CUE_UNAVAILABLE, ESP_ERR_NOT_SUPPORTED, 0, request->cue_id};
    ESP_LOGE("LUNASHELL_audio", "Cue output unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// request_sequence: Nonzero request identity.
// cue_id: Nonzero application identity.
// status: Receives actual route unavailability; no request was admitted.
// Exception: Returns invalid arguments or the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_get_cue_status(uint32_t request_sequence, uint32_t cue_id,
    lunashell_audio_cue_status_t *status)
{
    if (request_sequence == 0 || cue_id == 0 || status == nullptr) return ESP_ERR_INVALID_ARG;
    *status = {LUNASHELL_CUE_UNAVAILABLE, ESP_ERR_NOT_SUPPORTED, request_sequence, cue_id};
    return ESP_ERR_NOT_SUPPORTED;
}

// request_sequence: Requested nonzero audio identity.
// cue_id: Stable identity associated with that request.
// Exception: Returns invalid input or unavailable output; no request was admitted.
extern "C" esp_err_t LUNASHELL_audio_cancel_cue(uint32_t request_sequence, uint32_t cue_id)
{
    return request_sequence == 0 || cue_id == 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// request_sequence: Requested nonzero audio identity.
// cue_id: Stable identity associated with that request.
// Exception: Returns invalid input or unavailable output; no pinned record exists.
extern "C" esp_err_t LUNASHELL_audio_ack_cue(uint32_t request_sequence, uint32_t cue_id)
{
    return request_sequence == 0 || cue_id == 0 ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
}

// suspended: Requested gate for a route absent from this build.
// Exception: Returns the selected-route error without claiming a worker exists.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    (void)suspended;
    ESP_LOGE("LUNASHELL_audio", "Personality suspension unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// suspended: Requested presentation gate on a build without an audio owner.
// Exception: Returns the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_set_codex_suspended(bool suspended)
{
    (void)suspended;
    ESP_LOGE("LUNASHELL_audio", "Codex suspension unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Requested sound preference on a build without an audio owner.
// Exception: Returns the selected-route error.
extern "C" esp_err_t LUNASHELL_audio_set_codex_enabled(bool enabled)
{
    (void)enabled;
    ESP_LOGE("LUNASHELL_audio", "Codex audio preference unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}
