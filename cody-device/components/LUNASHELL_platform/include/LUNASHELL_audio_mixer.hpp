#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_log.h"

namespace LUNASHELL {

constexpr size_t kAudioMixerMaxVoices = 4;
constexpr size_t kAudioMixerMaxBlockFrames = 512;

enum class AudioMixerWave : uint8_t { Pcm, Triangle, Square, Saw, Noise };

// PCM storage belongs to the producer and must stay immutable and readable until
// the corresponding AudioMixerVoice becomes inactive. Other waveforms need no PCM.
// Durations and envelopes use the source clock; delay uses the transport clock.
struct AudioVoiceSource {
    AudioMixerWave wave = AudioMixerWave::Pcm;
    const int16_t *pcm = nullptr;
    uint32_t pcm_frames = 0;
    uint32_t source_rate_hz = 16000;
    uint32_t duration_frames = 0;
    uint32_t frequency_hz = 440;
    uint32_t delay_frames = 0;
    uint32_t attack_frames = 0;
    uint32_t release_frames = 0;
    uint32_t noise_seed = 1;
    uint16_t gain = 1000;
};

// A single output owner serializes initialization, rendering and cancellation.
// The rational position retains fractional input samples across output blocks.
struct AudioMixerVoice {
    AudioVoiceSource source{};
    uint64_t position = 0;
    uint32_t transport_rate_hz = 0;
    uint32_t delay_remaining = 0;
    uint32_t phase = 0;
    uint32_t phase_remainder = 0;
    bool active = false;
};

// voice: Caller-owned slot; unchanged when the source is rejected.
// source: Immutable description of one borrowed PCM clip or generated voice.
// transport_rate_hz: Clock for output frames and start delay, from 8 to 48 kHz.
// Exception: Returns ESP_ERR_INVALID_STATE for an active slot or ESP_ERR_INVALID_ARG for invalid source data.
inline esp_err_t initialize_audio_voice(AudioMixerVoice &voice, const AudioVoiceSource &source,
    uint32_t transport_rate_hz)
{
    if (voice.active) {
        ESP_LOGE("LUNASHELL_mixer", "Cannot replace an active voice without cancelling it");
        return ESP_ERR_INVALID_STATE;
    }
    const bool generated_tone = source.wave == AudioMixerWave::Triangle ||
        source.wave == AudioMixerWave::Square || source.wave == AudioMixerWave::Saw;
    const bool known_wave = source.wave == AudioMixerWave::Pcm || generated_tone ||
        source.wave == AudioMixerWave::Noise;
    if (!known_wave || source.source_rate_hz < 8000 || source.source_rate_hz > 48000 ||
        transport_rate_hz < 8000 || transport_rate_hz > 48000 || source.duration_frames == 0 ||
        source.gain > 1000 || source.attack_frames > source.duration_frames ||
        source.release_frames > source.duration_frames ||
        (source.wave == AudioMixerWave::Pcm &&
            (source.pcm == nullptr || source.pcm_frames < source.duration_frames)) ||
        (generated_tone && (source.frequency_hz == 0 || source.frequency_hz > transport_rate_hz / 2))) {
        ESP_LOGE("LUNASHELL_mixer", "Voice rejected: wave=%u source=%lu transport=%lu duration=%lu gain=%u",
            static_cast<unsigned>(source.wave), static_cast<unsigned long>(source.source_rate_hz),
            static_cast<unsigned long>(transport_rate_hz), static_cast<unsigned long>(source.duration_frames),
            static_cast<unsigned>(source.gain));
        return ESP_ERR_INVALID_ARG;
    }
    voice = {source, 0, transport_rate_hz, source.delay_frames, 0, 0, true};
    return ESP_OK;
}

// seed: Immutable authored seed.
// frame: Source-frame index, stable across output block boundaries.
// Exception: None; returns a deterministic noise value.
inline uint32_t audio_mixer_noise(uint32_t seed, uint32_t frame)
{
    uint32_t value = seed ^ (frame * 0x9e3779b9U);
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

// voice: Valid active owner slot; advances by exactly one output frame.
// Exception: None; initialization and output-block validation establish bounds.
inline int32_t audio_mixer_next_sample(AudioMixerVoice &voice)
{
    if (voice.delay_remaining != 0) {
        --voice.delay_remaining;
        return 0;
    }
    const auto &source = voice.source;
    const uint32_t index = static_cast<uint32_t>(voice.position / voice.transport_rate_hz);
    const uint32_t fraction = static_cast<uint32_t>(voice.position % voice.transport_rate_hz);
    int32_t sample = 0;
    switch (source.wave) {
    case AudioMixerWave::Pcm: {
        const uint32_t next = std::min(index + (index < source.duration_frames - 1U ? 1U : 0U),
            source.duration_frames - 1U);
        const int64_t first_weight = voice.transport_rate_hz - fraction;
        sample = static_cast<int32_t>((static_cast<int64_t>(source.pcm[index]) * first_weight +
            static_cast<int64_t>(source.pcm[next]) * fraction) / voice.transport_rate_hz);
        break;
    }
    case AudioMixerWave::Square: sample = voice.phase < 32768U ? 16000 : -16000; break;
    case AudioMixerWave::Saw: sample = static_cast<int32_t>(voice.phase / 2U) - 16384; break;
    case AudioMixerWave::Triangle:
        sample = voice.phase < 32768U ? static_cast<int32_t>(voice.phase) - 16384 :
            49151 - static_cast<int32_t>(voice.phase);
        break;
    case AudioMixerWave::Noise:
        sample = static_cast<int32_t>(audio_mixer_noise(source.noise_seed, index) & 0x7fffU) - 16384;
        break;
    }
    uint32_t envelope = 65535U;
    if (source.attack_frames != 0 && index < source.attack_frames) {
        envelope = static_cast<uint32_t>(static_cast<uint64_t>(index) * 65535U / source.attack_frames);
    }
    const uint32_t remaining = source.duration_frames - 1U - index;
    if (source.release_frames != 0 && remaining < source.release_frames) {
        envelope = std::min(envelope, static_cast<uint32_t>(
            static_cast<uint64_t>(remaining) * 65535U / source.release_frames));
    }
    const int32_t gained = sample * static_cast<int32_t>(source.gain) / 1000;
    const int32_t output = static_cast<int32_t>(static_cast<int64_t>(gained) * envelope / 65535);
    if (source.wave == AudioMixerWave::Triangle || source.wave == AudioMixerWave::Square ||
        source.wave == AudioMixerWave::Saw) {
        const uint32_t numerator = source.frequency_hz * 65536U;
        voice.phase = (voice.phase + numerator / voice.transport_rate_hz) & 0xffffU;
        voice.phase_remainder += numerator % voice.transport_rate_hz;
        if (voice.phase_remainder >= voice.transport_rate_hz) {
            voice.phase = (voice.phase + 1U) & 0xffffU;
            voice.phase_remainder -= voice.transport_rate_hz;
        }
    }
    voice.position += source.source_rate_hz;
    if (voice.position >= static_cast<uint64_t>(source.duration_frames) * voice.transport_rate_hz) {
        voice.active = false;
    }
    return output;
}

// voices: Caller-owned array of independently initialized slots; PCM stays borrowed until inactive.
// voice_count: Number of slots, from one through four, including inactive slots.
// transport_rate_hz: Common output clock, matching every active slot.
// destination: Caller-owned mono PCM16 output, disjoint from borrowed source PCM.
// frame_count: Output frames to render, from one through 512.
// Exception: Returns ESP_ERR_INVALID_ARG without modifying voices or destination on invalid input.
// PCM conversion is linear interpolation; it is not an anti-aliasing downsampler.
inline esp_err_t render_audio_mix(AudioMixerVoice *voices, size_t voice_count, uint32_t transport_rate_hz,
    int16_t *destination, size_t frame_count)
{
    if (voices == nullptr || destination == nullptr || voice_count == 0 ||
        voice_count > kAudioMixerMaxVoices || frame_count == 0 || frame_count > kAudioMixerMaxBlockFrames ||
        transport_rate_hz < 8000 || transport_rate_hz > 48000) {
        ESP_LOGE("LUNASHELL_mixer", "Output rejected: voices=%u frames=%u rate=%lu",
            static_cast<unsigned>(voice_count), static_cast<unsigned>(frame_count),
            static_cast<unsigned long>(transport_rate_hz));
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < voice_count; ++index) {
        if (voices[index].active && (voices[index].transport_rate_hz != transport_rate_hz ||
            voices[index].position >= static_cast<uint64_t>(voices[index].source.duration_frames) * transport_rate_hz)) {
            ESP_LOGE("LUNASHELL_mixer", "Voice %u clock or position rejected", static_cast<unsigned>(index));
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t sum = 0;
        for (size_t index = 0; index < voice_count; ++index) {
            if (voices[index].active) sum += audio_mixer_next_sample(voices[index]);
        }
        destination[frame] = static_cast<int16_t>(std::clamp<int32_t>(sum, -32768, 32767));
    }
    return ESP_OK;
}

} // namespace LUNASHELL
