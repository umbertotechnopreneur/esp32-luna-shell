#pragma once

#include <cstddef>
#include <cstdint>

namespace LUNASHELL {

struct AudioCueSequenceLayout {
    size_t clip_samples = 0;
    size_t gap_samples = 0;
    size_t total_samples = 0;
};

// source_samples: Immutable PCM; one clip may last two seconds, repeated clips at most 350 ms.
// source_rate_hz: Authored 16000 Hz source clock.
// transport_rate_hz: Existing duplex transport clock, never changed by a notice.
// repetitions: One ordinary clip or a bounded sequence of at most five identical clips.
// gap_ms: Silence between clips, at most 200 ms; no trailing silence is appended.
// layout: Receives validated transport lengths; unchanged on rejection.
// Returns false for unsupported format or a sequence exceeding 2500 ms.
inline bool prepare_audio_cue_sequence(size_t source_samples, uint32_t source_rate_hz,
    uint32_t transport_rate_hz, uint8_t repetitions, uint16_t gap_ms, AudioCueSequenceLayout &layout)
{
    const size_t source_limit = repetitions == 1 ? 32000 : 5600;
    if (source_samples == 0 || source_samples > source_limit || source_rate_hz != 16000 ||
        transport_rate_hz < 8000 || transport_rate_hz > 48000 || repetitions == 0 || repetitions > 5 ||
        gap_ms > 200 || (repetitions == 1 && gap_ms != 0)) { return false; }
    const uint64_t duration_units = source_samples * uint64_t{1000} * repetitions +
        uint64_t{gap_ms} * (repetitions - 1) * source_rate_hz;
    if (duration_units > uint64_t{2500} * source_rate_hz) { return false; }
    const size_t clip = source_samples * transport_rate_hz / source_rate_hz;
    const size_t gap = transport_rate_hz * gap_ms / 1000;
    if (clip == 0) { return false; }
    layout = {clip, gap, clip * repetitions + gap * (repetitions - 1)};
    return true;
}

// layout: Successfully prepared bounded timeline with a nonzero clip length.
// offset: Transport sample index strictly below total_samples.
// Returns a clip-local transport index, or SIZE_MAX for an authored inter-clip gap.
inline size_t audio_cue_sequence_clip_offset(const AudioCueSequenceLayout &layout, size_t offset)
{
    const size_t local = offset % (layout.clip_samples + layout.gap_samples);
    return local < layout.clip_samples ? local : SIZE_MAX;
}

} // namespace LUNASHELL
