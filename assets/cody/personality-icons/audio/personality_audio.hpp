#pragma once
#include <cstddef>
#include <cstdint>
#include "CODY_personality.hpp"
namespace CODY::PersonalityAudioAssets {
struct Clip {
    PersonalityCue cue;
    const int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
    bool enabled;
};
inline constexpr size_t clip_count = 14;
extern const Clip clips[clip_count];
} // namespace CODY::PersonalityAudioAssets
