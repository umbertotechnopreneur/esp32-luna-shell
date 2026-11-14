#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

// Host-only static Studio contract. No firmware enum or mutable owner pointer
// crosses this boundary. Pixels and this identity retire in one display lock.
struct CodyHostStudioFrameIdentity {
    uint32_t abi = 1;
    uint32_t frame_revision = 0;
    uint32_t at_ms = 0;
    char target[48]{};
    char scenario_revision[65]{};
};
static_assert(sizeof(CodyHostStudioFrameIdentity) == 128, "Studio frame identity ABI must remain fixed");

#if defined(_WIN32)
#define CODY_HOST_STUDIO_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_STUDIO_API extern "C" __attribute__((visibility("default")))
#endif

// identity: Actual prepared route and scenario; a null pointer clears staging.
// Exception: No exceptions; called only by the serialized rendering owner.
void cody_host_display_stage_studio_identity(const CodyHostStudioFrameIdentity *identity);

// scenario_revision: Exactly 64 lowercase hexadecimal characters, plus terminator.
// at_ms: Static prepared sample; only zero is currently supported.
// brightness_percent: Explicit panel setting, 10..100.
// auto_rotate: Explicit simulated preference, 0 or 1; gravity stays unavailable.
// orientation_degrees: Manual logical orientation, 0 or 180.
// Exception: Returns validation, owner preparation/render, or real deadline errors.
#if defined(CODY_HOST_REAL_AUDIO)
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_prepare_display(const char *scenario_revision,
    uint32_t at_ms, uint32_t brightness_percent, uint8_t auto_rotate, uint16_t orientation_degrees);
#endif

// pixels: Receives the complete latest native RGB565 frame.
// count: Exact logical framebuffer capacity, 466 x 466 pixels.
// identity: Receives the identity committed with those exact pixels.
// Exception: Rejects invalid buffers, missing frames, or a non-Studio frame.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_snapshot(uint16_t *pixels,
    size_t count, CodyHostStudioFrameIdentity *identity);
