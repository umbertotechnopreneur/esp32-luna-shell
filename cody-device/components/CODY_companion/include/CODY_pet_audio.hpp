#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace CODY {

// Stable semantic identities; these are independent of Codex events.
enum class PetCue : uint32_t {
    BootDing = 0x50450001U,
    BirthRise = 0x50450002U,
    BirthGreeting = 0x50450003U,
    Tap = 0x50450004U,
    Jump = 0x50450005U,
    Surprise = 0x50450006U,
};

struct PetAudioClip {
    const int16_t *samples = nullptr;
    size_t sample_count = 0;
};

// cue: Stable pet sound identity.
// clip: Receives immutable, aligned 16 kHz mono PCM retained for firmware lifetime.
// Exception: Returns an unknown identity or invalid embedded asset geometry.
esp_err_t pet_audio_clip(PetCue cue, PetAudioClip &clip);

} // namespace CODY
