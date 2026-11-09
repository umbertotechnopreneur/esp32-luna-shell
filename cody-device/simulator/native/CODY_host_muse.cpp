#include "CODY_pixel_avatar.hpp"

#include <algorithm>
#include <cstddef>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// time_ms: Nonnegative active time since the preview starts.
// reaction_ms: Time since the accepted pet, or UINT32_MAX if there is no active pet.
// pixels: Exclusive native RGB565 output for the device-sized avatar.
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
    CODY::PixelAvatarFrame frame{};
    CODY::compose_pixel_avatar(frame, waking ? CODY::PixelAvatarPose::Waking : CODY::PixelAvatarPose::Idle,
        time_ms, waking ? time_ms : time_ms - waking_ms, reaction_ms);
    std::fill_n(pixels, count, 0);
    CODY2D::Surface surface{pixels, side, side, side, static_cast<uint32_t>(count)};
    return CODY::draw_pixel_avatar_rows(frame, {0, 0, side, side}, surface, 0, 0);
}
