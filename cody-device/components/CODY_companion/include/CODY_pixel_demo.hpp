#pragma once

#include "CODY_device_capabilities.hpp"
#include "CODY_pixel_avatar.hpp"
#include "CODY_scene.hpp"

namespace CODY {

inline constexpr uint64_t kPixelDemoWakingUs = 1500000;

// The display worker owns both indexed frames in PSRAM through DMA retirement.
struct PixelDemoView {
    // logical_viewport: Immutable artwork and tap bounds supplied by the display adapter.
    explicit PixelDemoView(LogicalRect logical_viewport) : viewport(logical_viewport) {}

    const LogicalRect viewport;
    std::array<PixelAvatarFrame, 2> frames{};
    DamageRect damage{};
    uint64_t elapsed_us = 0;
    uint64_t reaction_started_us = 0;
    int64_t observed_active_us = 0;
    int64_t touch_started_us = 0;
    int64_t touch_sampled_us = 0;
    CODY2D::Point touch_origin{};
    uint32_t touch_revision = 0;
    uint8_t write_buffer = 0;
    bool visible = false;
    bool retired_valid = false;
    bool reaction_active = false;
    bool touch_candidate = false;
};
static_assert(sizeof(PixelDemoView) <= 10 * 1024, "Keep mascot frames in the display owner's PSRAM budget");

// view: Display-owned state and two borrowed RGB565 frames.
// active_us: Existing awake clock, excluding standby.
// content: Mascot-only scene callback and its centered draw bounds.
// Exception: Rejects a negative or reversed clock.
esp_err_t prepare_pixel_demo(PixelDemoView &view, int64_t active_us, SceneContent &content);

// view: Fully retired mascot frame whose grid becomes the damage baseline.
void retire_pixel_demo(PixelDemoView &view);

// view: Hidden or invalidated mascot; retain the clock and discard touch ownership.
void hide_pixel_demo(PixelDemoView &view);

// view: Display-owned contact and reaction state.
// point: Acquired logical coordinate; release coordinates are never used for travel.
// pressed: Real acquired contact state.
// pressed_edge: First acquired pressed sample of this contact.
// sampled_at_us: Original touch acquisition time.
// display_revision: Current retired panel/touch mapping identity.
// Returns true only when a tap starts the Muse reaction.
bool observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us, uint32_t display_revision);

} // namespace CODY
