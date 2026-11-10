#pragma once

#include "CODY_device_capabilities.hpp"
#include "CODY_pixel_avatar.hpp"
#include "CODY_muse_showcase.hpp"
#include "CODY_scene.hpp"

namespace CODY {

inline constexpr uint64_t kPixelDemoWakingUs = kMuseShowcaseWakeMs * 1000U;

// One existing canonical IMU observation, projected without another hardware read.
struct PixelDemoMotion {
    float acceleration_x = 0;
    float acceleration_y = 0;
    int64_t sampled_at_us = 0;
    uint16_t rendering_rotation_degrees = 0;
    bool valid = false;
};

struct PixelDemoMotionState {
    float baseline_x = 0;
    float baseline_y = 0;
    float offset_x = 0;
    float offset_y = 0;
    int64_t sampled_at_us = 0;
    uint16_t rendering_rotation_degrees = 0;
    bool primed = false;
};

// state: Motion state owned by the physical display or a separate host preview.
// sample: Canonical or explicitly synthetic acceleration and display orientation.
// now_us: Monotonic observation time after the sample was obtained.
void advance_pixel_demo_motion(PixelDemoMotionState &state, const PixelDemoMotion &sample, int64_t now_us);

// style: Artwork composition receiving bounded motion and gaze offsets.
// state: Previously advanced display-owner motion state.
void apply_pixel_demo_motion(MuseVisualStyle &style, const PixelDemoMotionState &state);

// The display worker owns both indexed frames in PSRAM through DMA retirement.
struct PixelDemoView {
    // logical_viewport: Immutable full-size reference supplied by the display adapter.
    explicit PixelDemoView(LogicalRect logical_viewport)
        : viewport(logical_viewport), visual_viewport(logical_viewport), retired_viewport(logical_viewport) {}

    const LogicalRect viewport;
    LogicalRect visual_viewport;
    LogicalRect retired_viewport;
    MuseShowcase showcase{};
    MuseVisualStyle visual_style{};
    PixelDemoMotionState motion{};
    std::array<PixelAvatarFrame, 2> frames{};
    PixelAvatarDamage damage{};
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

// view: Display-owned motion response, independent of the showcase clock.
// sample: Validated canonical acceleration and applied display orientation.
// now_us: Monotonic observation time after the snapshot read.
void observe_pixel_demo_motion(PixelDemoView &view, const PixelDemoMotion &sample, int64_t now_us);

// Change the current artwork and touch bounds without discarding the animation clock.
// Invalid square bounds leave the current viewport unchanged.
esp_err_t set_pixel_demo_viewport(PixelDemoView &view, LogicalRect viewport);

// view: Display-owned state and two indexed frames.
// active_us: Existing awake clock, excluding standby.
// content: Mascot-only scene callback and its centered draw bounds.
// Exception: Rejects a negative or reversed clock.
esp_err_t prepare_pixel_demo(PixelDemoView &view, int64_t active_us, SceneContent &content);

// view: Fully retired mascot frame whose grid becomes the damage baseline.
void retire_pixel_demo(PixelDemoView &view);

// view: Hidden or invalidated mascot; retain the clock and discard touch ownership.
void hide_pixel_demo(PixelDemoView &view);

// view: Display-owned contact state; recognition never starts a reaction.
// point: Acquired logical coordinate; release coordinates are never used for travel.
// pressed: Real acquired contact state.
// pressed_edge: First acquired pressed sample of this contact.
// sampled_at_us: Original touch acquisition time.
// display_revision: Current retired panel/touch mapping identity.
// Returns true once for a recognized tap, before semantic delivery.
bool observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us, uint32_t display_revision);

// view: Visible mascot whose tap has already been accepted by the pet owner.
void start_pixel_demo_reaction(PixelDemoView &view);

} // namespace CODY
