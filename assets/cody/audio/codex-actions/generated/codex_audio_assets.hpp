#pragma once
#include <cstddef>
#include <cstdint>
namespace CODY {
enum class CodexAudioCue : uint32_t {
    None = 0,
    Working = 0x43430001U,
    Reading = 0x43430002U,
    Editing = 0x43430003U,
    Running = 0x43430004U,
    ResponseReady = 0x43430005U,
    Paused = 0x43430006U,
    Interrupted = 0x43430007U,
    SessionEnded = 0x43430008U,
    ConnectionLost = 0x43430009U,
    ConnectionRestored = 0x4343000AU,
};
namespace CodexAudioAssets {
struct Clip {
    CodexAudioCue cue;
    const int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
};
inline constexpr size_t clip_count = 10;
extern const Clip clips[clip_count];
} // namespace CodexAudioAssets
} // namespace CODY
