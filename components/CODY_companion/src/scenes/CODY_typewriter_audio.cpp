#include "CODY_typewriter_audio.hpp"
#include "LUNASHELL_platform.h"

#include <array>
#include <cstddef>

namespace CODY {
namespace {
constexpr uint32_t kTapSampleRateHz = 16000;
constexpr size_t kTapSampleCount = 320;

// No parameters: creates an original rounded noise impulse, with no imported recording.
constexpr std::array<int16_t, kTapSampleCount> make_tap()
{
    std::array<int16_t, kTapSampleCount> samples{};
    uint32_t noise_state = 0x4c554e41U;
    int32_t rounded_noise = 0;
    int32_t slow_noise = 0;
    for (size_t index = 0; index < samples.size(); ++index) {
        noise_state ^= noise_state << 13U;
        noise_state ^= noise_state >> 17U;
        noise_state ^= noise_state << 5U;
        const int32_t noise = static_cast<int32_t>(noise_state & 0x7ffU) - 1024;
        rounded_noise = (rounded_noise * 2 + noise) / 3;
        slow_noise = (slow_noise * 7 + rounded_noise) / 8;
        const int32_t band_noise = rounded_noise - slow_noise;
        const int32_t attack = index < 24U ? static_cast<int32_t>(index) : 24;
        const int32_t remaining = static_cast<int32_t>(samples.size() - index - 1U);
        const int32_t attacked_noise = band_noise * attack / 24;
        samples[index] = static_cast<int16_t>(attacked_noise * remaining * remaining / (319 * 319));
    }
    return samples;
}

constexpr auto kOriginalTap = make_tap();
static_assert(sizeof(kOriginalTap) <= 800U, "The original tap must remain a tiny flash asset");
static_assert(kTapSampleCount * 1000U / kTapSampleRateHz == 20U,
              "The original tap is exactly 20 ms of mono PCM16 at 16 kHz");
static_assert(kOriginalTap.front() == 0 && kOriginalTap.back() == 0,
              "The tap must start and end at silence");
} // namespace

// No parameters: the audio owner registers this immutable asset and a dormant worker.
// Exception: Returns the exact preparation error, without attempting audible playback.
esp_err_t prepare_typewriter_audio()
{
    return LUNASHELL_audio_prepare_typewriter(kOriginalTap.data(), kOriginalTap.size(), kTapSampleRateHz);
}

// newly_visible_characters: Actual revealed glyphs; duplicate or hidden frames pass zero.
// Exception: Returns admission or retained asynchronous playback failures.
esp_err_t notify_typewriter_audio(uint32_t newly_visible_characters)
{
    return LUNASHELL_audio_notify_typewriter(newly_visible_characters);
}

// suspended: Drops pending progress rather than replaying it when the scene returns.
// Exception: Returns the audio owner's notification result.
esp_err_t set_typewriter_audio_suspended(bool suspended)
{
    return LUNASHELL_audio_set_typewriter_suspended(suspended);
}
} // namespace CODY
