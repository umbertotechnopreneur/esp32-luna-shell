#include "CODY_pixel_avatar.hpp"

#include <cstddef>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// seed: Artistic seed for a new scenario, with zero selecting startup defaults.
// The same preview owner must serialize reset and frame calls.
CODY_HOST_EXPORT void cody_host_muse_reset(uint32_t seed)
{
    CODY::reset_muse_animation(seed);
}

// pose: Numeric PixelAvatarPose value; the host ABI rejects unknown values.
// time_ms: Nonnegative active animation time.
// pose_ms: Elapsed time since the selected pose began.
// reaction_ms: Elapsed tap reaction time; a value of 1600 ms or more is inactive.
// audio_level: Optional normalized artistic input; no host audio is acquired.
// working: Zero for normal pose or one for the active-work Thinking variant.
// side: Requested viewport side, in pixels, as a positive multiple of 64.
// pixels: Exclusive native RGB565 output.
// count: Exact writable output length in pixels.
// Exception: Returns the original draw error or rejects invalid input before composition.
CODY_HOST_EXPORT esp_err_t cody_host_muse_pose_frame(uint32_t pose, uint32_t time_ms,
    uint32_t pose_ms, uint32_t reaction_ms, float audio_level, uint32_t working,
    uint32_t side, uint16_t *pixels, size_t count)
{
    if (pose > static_cast<uint32_t>(CODY::PixelAvatarPose::Sleep) || working > 1 ||
        pixels == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (side == 0 || side > CODY2D::kMaxDimension || side % CODY::kPixelAvatarGridSize != 0 ||
        count != static_cast<size_t>(side) * side) {
        return ESP_ERR_INVALID_SIZE;
    }
    CODY::PixelAvatarFrame frame{};
    const auto status = CODY::compose_muse_frame(frame, {
        static_cast<CODY::PixelAvatarPose>(pose), time_ms, pose_ms, reaction_ms, audio_level, working == 1,
    });
    if (status != CODY::PixelAvatarComposeStatus::Ok) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto width = static_cast<uint16_t>(side);
    CODY2D::Surface surface{pixels, width, width, width, static_cast<uint32_t>(count)};
    return CODY::draw_pixel_avatar_rows(frame, {0, 0, width, width}, surface, 0, 0);
}

// time_ms: Nonnegative active time since the preview starts.
// reaction_ms: Time since the accepted pet, or UINT32_MAX if there is no active pet.
// pixels: Exclusive native RGB565 output for the Studio preview.
// count: Writable output length in pixels.
// Exception: Returns the native surface validation error if drawing fails.
CODY_HOST_EXPORT esp_err_t cody_host_muse_frame(uint32_t time_ms, uint32_t reaction_ms,
    uint16_t *pixels, size_t count)
{
    constexpr uint16_t side = 320;
    constexpr uint32_t waking_ms = 1500;
    if (pixels == nullptr || count != side * side) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool waking = time_ms < waking_ms;
    const auto pose = waking ? CODY::PixelAvatarPose::Waking : CODY::PixelAvatarPose::Idle;
    return cody_host_muse_pose_frame(static_cast<uint32_t>(pose), time_ms,
        waking ? time_ms : time_ms - waking_ms, reaction_ms, 0.0f, 0, side, pixels, count);
}
