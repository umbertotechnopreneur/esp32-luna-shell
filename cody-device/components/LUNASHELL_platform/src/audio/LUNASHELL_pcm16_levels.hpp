#pragma once

#include <cstddef>
#include <cstdint>

namespace LUNASHELL::audio_detail {

struct Pcm16Levels {
    uint32_t mean = 0;
    uint32_t peak = 0;
    uint32_t channel_mean[2] = {};
    uint32_t channel_peak[2] = {};
};

// samples: Successfully acquired interleaved PCM16, owned by the caller.
// frame_count: Validated nonzero number of complete frames in samples.
// channel_count: Validated hardware topology, either one or two channels.
inline Pcm16Levels measure_pcm16_levels(const int16_t *samples, size_t frame_count,
                                      size_t channel_count)
{
    Pcm16Levels levels;
    uint64_t channel_sum[2] = {};
    // Capture owners validate the buffer and topology before this pure projection.
    // Widen before negation so the full-scale negative PCM16 value stays exact.
    for (size_t frame = 0; frame < frame_count; ++frame) {
        for (size_t channel = 0; channel < channel_count; ++channel) {
            const int32_t sample = samples[frame * channel_count + channel];
            const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
            channel_sum[channel] += magnitude;
            if (magnitude > levels.channel_peak[channel]) {
                levels.channel_peak[channel] = magnitude;
            }
        }
    }
    for (size_t channel = 0; channel < channel_count; ++channel) {
        levels.channel_mean[channel] = static_cast<uint32_t>(channel_sum[channel] / frame_count);
        if (levels.channel_peak[channel] > levels.peak) {
            levels.peak = levels.channel_peak[channel];
        }
    }
    // Divide the combined sum once; averaging truncated channel means changes it.
    levels.mean = static_cast<uint32_t>(
        (channel_sum[0] + channel_sum[1]) / (frame_count * channel_count));
    return levels;
}

}  // namespace LUNASHELL::audio_detail
