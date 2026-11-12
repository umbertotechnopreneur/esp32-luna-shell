#include "CODY_pet_audio.hpp"

#include "esp_log.h"

extern "C" {
extern const uint8_t cody_pet_boot_ding_start[] asm("_binary_cody_pet_boot_ding_start");
extern const uint8_t cody_pet_boot_ding_end[] asm("_binary_cody_pet_boot_ding_end");
extern const uint8_t cody_pet_birth_rise_start[] asm("_binary_cody_pet_birth_rise_start");
extern const uint8_t cody_pet_birth_rise_end[] asm("_binary_cody_pet_birth_rise_end");
extern const uint8_t cody_pet_birth_greeting_start[] asm("_binary_cody_pet_birth_greeting_start");
extern const uint8_t cody_pet_birth_greeting_end[] asm("_binary_cody_pet_birth_greeting_end");
extern const uint8_t cody_pet_tap_start[] asm("_binary_cody_pet_tap_start");
extern const uint8_t cody_pet_tap_end[] asm("_binary_cody_pet_tap_end");
extern const uint8_t cody_pet_jump_start[] asm("_binary_cody_pet_jump_start");
extern const uint8_t cody_pet_jump_end[] asm("_binary_cody_pet_jump_end");
extern const uint8_t cody_pet_surprise_start[] asm("_binary_cody_pet_surprise_start");
extern const uint8_t cody_pet_surprise_end[] asm("_binary_cody_pet_surprise_end");
}

namespace CODY {

// cue: Explicit pet sound identity, never inferred from unrelated app events.
// clip: Receives validated immutable PCM and its exact sample count.
// Exception: Returns an unknown identity or invalid embedded asset geometry.
esp_err_t pet_audio_clip(PetCue cue, PetAudioClip &clip)
{
    clip = {};
    const uint8_t *first = nullptr;
    const uint8_t *last = nullptr;
    switch (cue) {
    case PetCue::BootDing: first = cody_pet_boot_ding_start; last = cody_pet_boot_ding_end; break;
    case PetCue::BirthRise: first = cody_pet_birth_rise_start; last = cody_pet_birth_rise_end; break;
    case PetCue::BirthGreeting: first = cody_pet_birth_greeting_start; last = cody_pet_birth_greeting_end; break;
    case PetCue::Tap: first = cody_pet_tap_start; last = cody_pet_tap_end; break;
    case PetCue::Jump: first = cody_pet_jump_start; last = cody_pet_jump_end; break;
    case PetCue::Surprise: first = cody_pet_surprise_start; last = cody_pet_surprise_end; break;
    default:
        ESP_LOGE("CODY_pet_audio", "Unknown pet cue=%lu", static_cast<unsigned long>(cue));
        return ESP_ERR_INVALID_ARG;
    }
    const auto begin = reinterpret_cast<uintptr_t>(first);
    const auto end = reinterpret_cast<uintptr_t>(last);
    if (end <= begin || begin % alignof(int16_t) != 0 || (end - begin) % sizeof(int16_t) != 0 ||
        end - begin > 32000U * sizeof(int16_t)) {
        ESP_LOGE("CODY_pet_audio", "Invalid pet audio geometry for cue=%lu", static_cast<unsigned long>(cue));
        return ESP_ERR_INVALID_SIZE;
    }
    clip = {reinterpret_cast<const int16_t *>(first), (end - begin) / sizeof(int16_t)};
    return ESP_OK;
}

} // namespace CODY
