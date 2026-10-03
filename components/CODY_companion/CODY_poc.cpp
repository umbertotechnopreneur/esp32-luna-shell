#include "CODY_poc.h"
#include "poc_settings.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstddef>
#include <cstdint>

extern "C" {
extern const uint8_t audio_ready_start[] asm("_binary_cody_audio_ready_start");
extern const uint8_t audio_ready_end[] asm("_binary_cody_audio_ready_end");
extern const uint8_t eyes_open_start[] asm("_binary_cody_eyes_open_start");
extern const uint8_t eyes_open_end[] asm("_binary_cody_eyes_open_end");
}
namespace {
constexpr char kTag[] = "CODY_poc";
int64_t s_boot_started_us = 0;
int64_t s_stage_started_us = 0;

// first: Aligned flash-owned little-endian PCM16 data.
// last: Linker end symbol for exactly that asset.
// samples: Receives borrowed samples without decoding or heap allocation.
// count: Receives the exact mono sample count.
esp_err_t pcm_view(const uint8_t *first, const uint8_t *last,
                   const int16_t *&samples, size_t &count)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(first);
    const uintptr_t end = reinterpret_cast<uintptr_t>(last);
    if (end <= start || start % alignof(int16_t) != 0 || (end - start) % 2 != 0) {
        ESP_LOGE(kTag, "Boot PCM bounds/alignment rejected: %s", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    samples = reinterpret_cast<const int16_t *>(first);
    count = (end - start) / sizeof(int16_t);
    return ESP_OK;
}
} // namespace

// No parameters: validate flash-owned boot sound; defer playback until trusted time is published.
extern "C" esp_err_t CODY_poc_prepare_boot(void)
{
    if (s_boot_started_us != 0) {
        ESP_LOGE(kTag, "Duplicate PoC boot preparation: %s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_boot_started_us = esp_timer_get_time();
    s_stage_started_us = s_boot_started_us;
    const int16_t *samples = nullptr;
    size_t count = 0;
    esp_err_t error = pcm_view(audio_ready_start, audio_ready_end, samples, count);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Audio-ready cue preparation failed: %s", esp_err_to_name(error));
    }
    // Codec initialization happens before the time service. Playing there would
    // bypass quiet hours; Cody's existing completed-boot stage owns both cues now.
    return error;
}

// stage: Real completed startup operation, logged to console before the optional SD mirror.
extern "C" void CODY_poc_boot_checkpoint(const char *stage)
{
    const int64_t now_us = esp_timer_get_time();
    if (CODY::PocData::execution_timings) {
        ESP_LOGI(kTag, "TIMING boot stage=\"%s\" stage_us=%lld elapsed_us=%lld", stage,
            static_cast<long long>(now_us - s_stage_started_us),
            static_cast<long long>(now_us - s_boot_started_us));
    }
    s_stage_started_us = now_us;
}

// No parameters: report published SD availability without changing saved log policy.
// Returns the original canonical snapshot error if this observation fails.
extern "C" esp_err_t CODY_poc_configure_diagnostics(void)
{
    LUNASHELL::MotionPowerObservation observation;
    const esp_err_t read_error =
        LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(observation);
    if (read_error != ESP_OK) {
        ESP_LOGE(kTag, "Diagnostics snapshot read failed: %s", esp_err_to_name(read_error));
        return read_error;
    }
    // Timing diagnostics are independent of the user's saved SD preference.
    // In particular, Release must not persistently turn logging off at boot.
    ESP_LOGI(kTag, "Diagnostics timings=%u; published SD availability=%u; saved storage log policy retained",
        static_cast<unsigned>(CODY::PocData::execution_timings),
        static_cast<unsigned>(observation.storage_available));
    return ESP_OK; // The snapshot read completed; no filesystem operation was attempted.
}

// No parameters: completed boot plays bounded cues only when canonical quiet policy permits.
extern "C" esp_err_t CODY_poc_play_eyes_open(void)
{
    LUNASHELL::ClockSnapshot clock;
    const esp_err_t observation_error =
        LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (observation_error != ESP_OK) {
        ESP_LOGE(kTag, "Boot cue clock observation failed (%s)", esp_err_to_name(observation_error));
        return observation_error;
    }
    const auto policy = LUNASHELL::clock_cue_policy(clock);
    if (policy != LUNASHELL::ClockCuePolicy::Allowed) {
        const char *reason = policy == LUNASHELL::ClockCuePolicy::QuietHours ? "quiet hours" :
            policy == LUNASHELL::ClockCuePolicy::TimeUnavailable ? "time unavailable/stale" : "settings unavailable";
        ESP_LOGI(kTag, "Boot cues deliberately suppressed: %s; saved codec volume unchanged", reason);
        CODY_poc_boot_checkpoint("boot cues suppressed by routine policy");
        return ESP_OK;
    }
    const int16_t *samples = nullptr;
    size_t count = 0;
    esp_err_t error = pcm_view(audio_ready_start, audio_ready_end, samples, count);
    if (error == ESP_OK) {
        error = LUNASHELL_audio_play_pcm16(samples, count,
            CODY::PocData::sample_rate_hz, CODY::PocData::ready_volume_percent);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Audio-ready cue failed: %s", esp_err_to_name(error));
        return error;
    }
    error = pcm_view(eyes_open_start, eyes_open_end, samples, count);
    if (error == ESP_OK) {
        error = LUNASHELL_audio_play_pcm16(samples, count,
            CODY::PocData::sample_rate_hz, CODY::PocData::eyes_open_volume_percent);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Eyes-open cue failed: %s", esp_err_to_name(error));
    }
    CODY_poc_boot_checkpoint("eyes-open audio cue");
    return error;
}
