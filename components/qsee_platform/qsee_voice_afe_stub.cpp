/*
 * Voice AFE build-off implementation.
 */

#include "qsee_platform.h"

extern "C" esp_err_t qsee_voice_afe_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" bool qsee_voice_afe_running(void)
{
    return false;
}

extern "C" bool qsee_voice_afe_capture_active(void)
{
    return false;
}

extern "C" esp_err_t qsee_voice_afe_set_standby(bool standby)
{
    (void)standby;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_voice_afe_set_performance_suspended(bool suspended)
{
    (void)suspended;
    return ESP_OK;
}

extern "C" esp_err_t qsee_voice_afe_set_diagnostic_suspended(bool suspended)
{
    (void)suspended;
    return ESP_OK;
}

extern "C" esp_err_t qsee_voice_afe_push_speaker_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz
)
{
    (void)samples;
    (void)sample_count;
    (void)sample_rate_hz;
    return ESP_ERR_NOT_SUPPORTED;
}
