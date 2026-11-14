#include "CODY_whatsapp_audio.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace CODY {
namespace {
// The shared application-audio owner admits IDs in the 0x4343 namespace.
constexpr uint32_t kCue = 0x4343FF06U;
constexpr uint32_t kRate = 16000;
constexpr size_t kLength = 3840;

// No parameters: Author a short, original two-note message chime in PCM16.
// The rounded triangle has a quick attack and a separate decay for each note.
constexpr std::array<int16_t, kLength> make_whatsapp_cue()
{
    std::array<int16_t, kLength> result{};
    uint32_t phase = 0;
    for (size_t index = 0; index < kLength; ++index) {
        const bool second = index >= 1280;
        const size_t local = second ? index - 1280 : index;
        const size_t duration = second ? 2560 : 1280;
        if (local >= duration) continue;
        phase = (phase + (second ? 1000U : 800U)) % kRate;
        const int32_t triangle = phase < kRate / 2 ?
            static_cast<int32_t>(phase) - 4000 : 12000 - static_cast<int32_t>(phase);
        const int32_t attack = local < 64 ? static_cast<int32_t>(local) : 64;
        const int32_t remaining = static_cast<int32_t>(duration - local - 1);
        result[index] = static_cast<int16_t>(static_cast<int64_t>(triangle) * attack * remaining /
            (64 * static_cast<int32_t>(duration - 1)));
    }
    return result;
}

constexpr auto kSamples = make_whatsapp_cue();
static_assert(kSamples.front() == 0 && kSamples.back() == 0, "Chime boundaries must be silent");
} // namespace

// status: Receives the actual nonblocking result, including quiet-hour suppression.
// Exception: Preserves the shared audio owner's admission or policy failure.
esp_err_t notify_whatsapp_audio(lunashell_audio_cue_status_t &status)
{
    const lunashell_audio_request_t request{kSamples.data(), kSamples.size(), kRate, kCue,
        LUNASHELL_AUDIO_CODEX, LUNASHELL_CODEX_CUE_NOTICE, 1, 0, LUNASHELL_AUDIO_WAVE_PCM,
        0, 1000, 0, 0, 0, 0, false, 0, 0};
    return LUNASHELL_audio_submit(&request, &status);
}

} // namespace CODY
