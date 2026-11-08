#include "LUNASHELL_acquisition_audio.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_snapshot_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace LUNASHELL::snapshot_detail::audio {
namespace {
const char *TAG = "LUNASHELL_snapshot";
bool s_microphone_ready;
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
// Codec reads copy out of internal I2S DMA into this acquisition-owned PSRAM block.
EXT_RAM_BSS_ATTR int16_t s_microphone_samples[1024];
struct AudioSpectrumBands {
    uint8_t value[2][LUNASHELL::kAudioSpectrumBandCount] = {};
};
constexpr size_t kAudioSpectrumFrameCount = 256;
constexpr float kAudioSpectrumFullScale = 32768.0F;
// The ES7210's normal ambient signal is well below -60 dBFS; keep its measured
// bands visible instead of quantising every quiet, valid capture to zero.
constexpr float kAudioSpectrumFloor = 0.00001F;
constexpr float kAudioSpectrumDisplayFloorDbfs = -90.0F;
constexpr float kAudioSpectrumTwoPi = 6.28318530718F;
constexpr uint16_t kAudioSpectrumBins[LUNASHELL::kAudioSpectrumBandCount] = {
    1, 2, 3, 4, 6, 8, 11, 16,
    23, 32, 45, 64, 90, 110, 120, 127,
};

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// component: Canonical component observation whose validity is checked.
bool component_has_current_data(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}
#endif

// Project the measured microphone channels onto the compact DFT bank.
// spectrum: Caller-owned measured channel spectrum output.
// channel_count: Confirmed codec topology used to index captured PCM.
void build_audio_spectrum(AudioSpectrumBands &spectrum, size_t channel_count)
{
    for (size_t band = 0; band < LUNASHELL::kAudioSpectrumBandCount; ++band) {
        const float coefficient = 2.0F * std::cos(
            kAudioSpectrumTwoPi *
            static_cast<float>(kAudioSpectrumBins[band]) /
            static_cast<float>(kAudioSpectrumFrameCount)
        );
        for (size_t channel = 0; channel < channel_count; ++channel) {
            float previous = 0.0F;
            float previous_previous = 0.0F;
            for (size_t frame = 0; frame < kAudioSpectrumFrameCount; ++frame) {
                const float current = static_cast<float>(
                    s_microphone_samples[frame * channel_count + channel]
                ) + coefficient * previous - previous_previous;
                previous_previous = previous;
                previous = current;
            }
            const float power = std::max(
                0.0F,
                previous * previous +
                    previous_previous * previous_previous -
                    coefficient * previous * previous_previous
            );
            const float normalized =
                2.0F * std::sqrt(power) /
                (static_cast<float>(kAudioSpectrumFrameCount) *
                 kAudioSpectrumFullScale);
            const float dbfs = 20.0F * std::log10(
                std::max(normalized, kAudioSpectrumFloor)
            );
            const float intensity = std::clamp(
                (dbfs - kAudioSpectrumDisplayFloorDbfs) *
                    (100.0F / -kAudioSpectrumDisplayFloorDbfs),
                0.0F,
                100.0F
            );
            spectrum.value[channel][band] =
                static_cast<uint8_t>(intensity + 0.5F);
        }
    }
}

// Reuse canonical AFE measurements while its owner has the capture route.
// phase: Acquisition phase used for diagnostic logging and retry policy.
// spectrum_requested: Whether the caller holds an explicit spectrum capture lease.
// Exception: Returns original hardware or canonical publication failures.
esp_err_t capture_microphone(const char *phase, bool spectrum_requested)
{
    const size_t channel_count = LUNASHELL_audio_input_channel_count();
    const int64_t attempted_at_us = esp_timer_get_time();
    if (channel_count == 0U || channel_count > 2U) {
        const esp_err_t channel_error =
            channel_count == 0U
                ? ESP_ERR_NOT_SUPPORTED
                : ESP_ERR_INVALID_RESPONSE;
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us, channel_error, spectrum_requested](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    channel_error,
                    attempted_at_us
                );
                if (spectrum_requested) {
                    mark_sample_failure(
                        snapshot.audio.spectrum,
                        channel_error,
                        attempted_at_us
                    );
                }
            }
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            channel_error,
            publication_error
        );
    }
    const size_t frame_count =
        sizeof(s_microphone_samples) / sizeof(s_microphone_samples[0]) / channel_count;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    if (LUNASHELL_voice_afe_capture_active()) {
        // The AFE owner publishes its real levels. A refresh reads that same
        // canonical sample without retimestamping it or reopening the codec.
        if (spectrum_requested) {
            const esp_err_t publication_error = update_snapshot([attempted_at_us](
                LUNASHELL::DeviceSnapshot &snapshot) {
                mark_sample_failure(snapshot.audio.spectrum, ESP_ERR_NOT_ALLOWED, attempted_at_us);
            });
            return sample_publication_result("Microphone spectrum", phase,
                                             ESP_ERR_NOT_ALLOWED, publication_error);
        }
        LUNASHELL::MotionPowerObservation observation;
        const esp_err_t read_error =
            LUNASHELL::DeviceSnapshotService::instance().readMotionPowerObservation(observation);
        if (read_error != ESP_OK) {
            return read_error;
        }
        const auto &microphone = observation.audio.microphone;
        if (component_has_current_data(microphone)) {
            return ESP_OK;
        }
        const esp_err_t sample_error = microphone.last_error != ESP_OK
            ? microphone.last_error : ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "Canonical AFE microphone sample unavailable (%s)", esp_err_to_name(sample_error));
        return sample_error;
    }
#endif

    if (!s_microphone_ready) {
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us, spectrum_requested](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    ESP_ERR_NOT_SUPPORTED,
                    attempted_at_us
                );
                if (spectrum_requested) {
                    mark_sample_failure(
                        snapshot.audio.spectrum,
                        ESP_ERR_NOT_SUPPORTED,
                        attempted_at_us
                    );
                }
            }
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            ESP_ERR_NOT_SUPPORTED,
            publication_error
        );
    }
    const esp_err_t error = LUNASHELL_audio_capture_pcm16_channels(
        s_microphone_samples,
        frame_count,
        channel_count,
        22050,
        24.0F
    );

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us, spectrum_requested](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.audio.microphone,
                    error,
                    attempted_at_us
                );
                if (spectrum_requested) {
                    mark_sample_failure(
                        snapshot.audio.spectrum,
                        error,
                        attempted_at_us
                    );
                }
            }
        );
        ESP_LOGW(
            TAG,
            "Microphones ES7210: %s capture failed (%s)",
            phase,
            esp_err_to_name(error)
        );
        return sample_publication_result(
            "Microphones ES7210",
            phase,
            error,
            publication_error
        );
    }

    uint32_t absolute_sum[2] = {};
    uint32_t peak[2] = {};
    AudioSpectrumBands spectrum = {};
    for (size_t index = 0; index < frame_count; ++index) {
        for (size_t channel = 0; channel < channel_count; ++channel) {
            const int16_t raw =
                s_microphone_samples[index * channel_count + channel];
            const uint32_t value =
                raw < 0
                    ? static_cast<uint32_t>(-static_cast<int32_t>(raw))
                    : static_cast<uint32_t>(raw);
            absolute_sum[channel] += value;
            if (value > peak[channel]) {
                peak[channel] = value;
            }
        }
    }
    if (spectrum_requested) {
        build_audio_spectrum(spectrum, channel_count);
    }
    const uint32_t mean =
        (absolute_sum[0] + absolute_sum[1]) / (frame_count * channel_count);
    const uint32_t aggregate_peak = peak[0] > peak[1] ? peak[0] : peak[1];
    const uint32_t channel_mean0 = absolute_sum[0] / frame_count;
    const uint32_t channel_mean1 = absolute_sum[1] / frame_count;
    const uint32_t channel_peak0 = peak[0];
    const uint32_t channel_peak1 = peak[1];
    const esp_err_t publication_error = update_snapshot(
        [&spectrum, mean, aggregate_peak, channel_count, channel_mean0,
         channel_mean1, channel_peak0, channel_peak1, spectrum_requested,
         attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_success(
                snapshot.audio.microphone,
                attempted_at_us
            );
            snapshot.audio.channel_count =
                static_cast<uint8_t>(channel_count);
            snapshot.audio.sample_rate_hz = 22050;
            snapshot.audio.microphone_mean_level = mean;
            snapshot.audio.microphone_peak_level = aggregate_peak;
            snapshot.audio.channel_mean_level[0] = channel_mean0;
            snapshot.audio.channel_mean_level[1] = channel_mean1;
            snapshot.audio.channel_peak_level[0] = channel_peak0;
            snapshot.audio.channel_peak_level[1] = channel_peak1;
            if (spectrum_requested) {
                std::memcpy(
                    snapshot.audio.channel_spectrum,
                    spectrum.value,
                    sizeof(snapshot.audio.channel_spectrum)
                );
                ++snapshot.audio.spectrum_sequence;
                mark_sample_success(
                    snapshot.audio.spectrum,
                    attempted_at_us
                );
            }
        }
    );
    if (high_frequency_sample_phase(phase)) {
        ESP_LOGD(
            TAG,
            "Microphones ES7210 %s: channels=%u ch0=%lu/%lu ch1=%lu/%lu",
            phase,
            static_cast<unsigned>(channel_count),
            static_cast<unsigned long>(absolute_sum[0] / frame_count),
            static_cast<unsigned long>(peak[0]),
            static_cast<unsigned long>(absolute_sum[1] / frame_count),
            static_cast<unsigned long>(peak[1])
        );
    } else {
        ESP_LOGI(
            TAG,
            "Microphones ES7210 %s: channels=%u ch0=%lu/%lu ch1=%lu/%lu",
            phase,
            static_cast<unsigned>(channel_count),
            static_cast<unsigned long>(absolute_sum[0] / frame_count),
            static_cast<unsigned long>(peak[0]),
            static_cast<unsigned long>(absolute_sum[1] / frame_count),
            static_cast<unsigned long>(peak[1])
        );
    }
    return sample_publication_result(
        "Microphones ES7210",
        phase,
        ESP_OK,
        publication_error
    );
}

#endif
}  // namespace

// Exception: Returns original codec initialization and canonical publication failures.
esp_err_t initialize()
{
    const esp_err_t error = LUNASHELL_audio_init();
    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot([error](DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, error);
            set_component(snapshot.audio.spectrum, false, error);
            set_component(snapshot.audio.speaker, false, error);
        });
        if (error == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGI(TAG, "Audio codecs: disabled by board hardware selection");
        }
        return sample_publication_result("Audio codecs", "initialization", error, publication_error);
    }
    s_microphone_ready = LUNASHELL_audio_input_channel_count() > 0;
    if (!s_microphone_ready) {
        const esp_err_t publication_error = update_snapshot([](DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, ESP_ERR_NOT_SUPPORTED);
            set_component(snapshot.audio.spectrum, false, ESP_ERR_NOT_SUPPORTED);
        });
        ESP_LOGI(TAG, "Microphones: disabled by board hardware selection");
        return publication_error;
    }
    return ESP_OK;
}

// Readiness belongs to the acquisition owner; scheduler consumers never reopen the codec.
bool microphone_ready()
{
    return s_microphone_ready;
}

// phase: Acquisition phase used for diagnostic logging.
// spectrum_requested: Whether the caller holds an explicit spectrum capture lease.
// Exception: Returns original topology, capture or canonical publication failures.
esp_err_t sample_microphone(const char *phase, bool spectrum_requested)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE
    return capture_microphone(phase, spectrum_requested);
#else
    (void)phase;
    (void)spectrum_requested;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace LUNASHELL::snapshot_detail::audio
