/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
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
