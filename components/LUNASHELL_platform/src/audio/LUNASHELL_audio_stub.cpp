/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

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

extern "C" esp_err_t LUNASHELL_audio_capture_voice_pcm16(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count
)
{
    (void)samples;
    (void)frame_count;
    (void)channel_count;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_audio_prepare_voice_route(void)
{
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

// No parameters: report the explicitly unavailable selected output route.
// Exception: Returns the selected-route error and publishes that real optional failure.
extern "C" esp_err_t LUNASHELL_audio_prepare_personality(void)
{
    ESP_LOGE("LUNASHELL_audio", "Personality audio unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
        false, 0, 16000, 0, ESP_ERR_NOT_SUPPORTED);
    if (publication_error != ESP_OK) {
        ESP_LOGE("LUNASHELL_audio", "Personality failure publication failed (%s)", esp_err_to_name(publication_error));
    }
    return ESP_ERR_NOT_SUPPORTED;
}

// status: Receives truthful unavailability; no fictional worker or playback exists.
// Exception: Returns an invalid pointer or selected-route error.
extern "C" esp_err_t LUNASHELL_audio_get_personality_status(lunashell_audio_cue_status_t *status)
{
    if (status == nullptr) {
        ESP_LOGE("LUNASHELL_audio", "Personality status pointer rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    *status = {LUNASHELL_CUE_UNAVAILABLE, ESP_ERR_NOT_SUPPORTED, 0, 0};
    return ESP_ERR_NOT_SUPPORTED;
}

// samples: Immutable PCM requested on a build without audio output.
// sample_count: Exact mono sample count.
// sample_rate_hz: Authored rate.
// cue_id: Stable symbol identity for the rejected operation.
// status: Receives the unavailable disposition.
// Exception: Returns selected-route or invalid output pointer error.
extern "C" esp_err_t LUNASHELL_audio_notify_personality(const int16_t *samples, size_t sample_count,
    uint32_t sample_rate_hz, uint32_t cue_id, lunashell_audio_cue_status_t *status)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    const esp_err_t error = LUNASHELL_audio_get_personality_status(status);
    if (status != nullptr) {
        status->cue_id = cue_id;
    }
    ESP_LOGE("LUNASHELL_audio", "Personality dispatch unavailable (%s)", esp_err_to_name(error));
    return error;
}

// suspended: Requested gate for a route absent from this build.
// Exception: Returns the selected-route error without claiming a worker exists.
extern "C" esp_err_t LUNASHELL_audio_set_personality_suspended(bool suspended)
{
    (void)suspended;
    ESP_LOGE("LUNASHELL_audio", "Personality suspension unavailable (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}
