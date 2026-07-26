/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"

extern "C" esp_err_t qsee_audio_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" uint8_t qsee_audio_input_channel_count(void)
{
    return 0;
}

extern "C" esp_err_t qsee_audio_play_pcm16(
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
    qsee_hardware_report_speaker(false, ESP_ERR_NOT_SUPPORTED);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_audio_capture_pcm16(
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

extern "C" esp_err_t qsee_audio_capture_pcm16_channels(
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

extern "C" esp_err_t qsee_audio_capture_voice_pcm16(
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

extern "C" esp_err_t qsee_audio_prepare_voice_route(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_audio_set_standby(bool standby)
{
    (void)standby;
    return ESP_ERR_NOT_SUPPORTED;
}
