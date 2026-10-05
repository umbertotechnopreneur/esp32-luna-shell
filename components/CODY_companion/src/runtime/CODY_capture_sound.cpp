#include "CODY_capture_sound.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LUNASHELL_clock.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_capture_sound";
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t kSampleCount = 3200;
constexpr int kVolumePercent = 65;
constexpr uint32_t kWorkerStackBytes = 4096;
std::atomic<TaskHandle_t> s_worker{nullptr};
std::atomic<bool> s_pending{false};

// index: Current PCM sample inside the bounded original cue.
// start: First sample of one mechanical click.
// duration: Decay window in samples.
// peak: Maximum requested PCM amplitude before the noise filter.
constexpr int32_t click_envelope(size_t index, size_t start, size_t duration, int32_t peak)
{
    if (index < start || index >= start + duration) {
        return 0;
    }
    const size_t elapsed = index - start;
    const int32_t attack = elapsed < 40U ? static_cast<int32_t>(elapsed) : 40;
    const int32_t remaining = static_cast<int32_t>(duration - elapsed);
    return peak * attack * remaining / (40 * static_cast<int32_t>(duration));
}

// No parameters: synthesize a soft two-click mechanical shutter in firmware flash.
constexpr std::array<int16_t, kSampleCount> make_shutter()
{
    std::array<int16_t, kSampleCount> samples{};
    uint32_t noise_state = 0x53485554U;
    int32_t rounded_noise = 0;
    int32_t slow_noise = 0;
    for (size_t index = 0; index < samples.size(); ++index) {
        noise_state ^= noise_state << 13U;
        noise_state ^= noise_state >> 17U;
        noise_state ^= noise_state << 5U;
        const int32_t noise = static_cast<int32_t>(noise_state & 0x0FFFU) - 2048;
        rounded_noise = (rounded_noise * 2 + noise) / 3;
        slow_noise = (slow_noise * 15 + rounded_noise) / 16;
        const int32_t band_noise = rounded_noise - slow_noise;
        const int32_t envelope = click_envelope(index, 0U, 700U, 5700) +
            click_envelope(index, 1020U, 1640U, 4200);
        samples[index] = static_cast<int16_t>(band_noise * envelope / 2048);
    }
    return samples;
}

constexpr auto kOriginalShutter = make_shutter();
static_assert(kOriginalShutter.front() == 0 && kOriginalShutter.back() == 0,
              "The shutter cue must start and finish at silence");
static_assert(kSampleCount * 1000U / kSampleRateHz == 200U,
              "The shutter cue must remain 200 ms of mono PCM16");

// context: Unused task argument; this worker alone makes blocking audio calls.
void capture_sound_worker(void *context)
{
    (void)context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_pending.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }
        LUNASHELL::ClockSnapshot clock;
        const esp_err_t clock_error =
            LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
        if (clock_error != ESP_OK) {
            ESP_LOGE(kTag, "Shutter clock policy unavailable (%s)", esp_err_to_name(clock_error));
            continue;
        }
        if (LUNASHELL::clock_cue_policy(clock) != LUNASHELL::ClockCuePolicy::Allowed) {
            ESP_LOGI(kTag, "Shutter suppressed by quiet/time policy");
            continue;
        }
        // The platform audio mutex serializes this cue with voice and typewriter.
        const esp_err_t playback_error = LUNASHELL_audio_play_pcm16(
            kOriginalShutter.data(), kOriginalShutter.size(), kSampleRateHz, kVolumePercent);
        if (playback_error != ESP_OK) {
            ESP_LOGE(kTag, "Shutter playback failed (%s)", esp_err_to_name(playback_error));
        }
    }
}
#endif

} // namespace

// No parameters: prepare one dormant audio worker for the original shutter cue.
// Exception: Returns ESP_ERR_NOT_SUPPORTED outside Debug or the real worker allocation error.
esp_err_t prepare_capture_sound()
{
#if !CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    ESP_LOGE(kTag, "Shutter preparation rejected: Debug only (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#elif CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (s_worker.load(std::memory_order_acquire) != nullptr) {
        return ESP_OK;
    }
    TaskHandle_t worker = nullptr;
    if (xTaskCreateWithCaps(capture_sound_worker, "CODY_shutter", kWorkerStackBytes,
                            nullptr, 2, &worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "Shutter worker allocation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    s_worker.store(worker, std::memory_order_release);
    return ESP_OK;
#else
    ESP_LOGI(kTag, "Shutter audio unavailable in this selected build profile");
    return ESP_OK;
#endif
}

// No parameters: queue one shutter after a successfully retired capture frame.
// Exception: Returns the real notification or unavailable-route error.
esp_err_t notify_capture_sound()
{
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    const TaskHandle_t worker = s_worker.load(std::memory_order_acquire);
    if (worker == nullptr) {
        ESP_LOGE(kTag, "Shutter worker is unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_pending.store(true, std::memory_order_release);
    if (xTaskNotifyGive(worker) != pdPASS) {
        s_pending.store(false, std::memory_order_release);
        ESP_LOGE(kTag, "Shutter notification failed (%s)", esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    return ESP_OK;
#else
    ESP_LOGE(kTag, "Shutter audio rejected by selected output route (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace CODY
