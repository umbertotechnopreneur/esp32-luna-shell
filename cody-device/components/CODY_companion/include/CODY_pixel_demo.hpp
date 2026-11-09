#pragma once

#include "CODY_device_view.hpp"
#include "CODY_pixel_avatar.hpp"

namespace CODY {

inline constexpr CODY2D::Rect kPixelDemoViewport{137, 112, 192, 192};

// Retained by DeviceUi in PSRAM; neither indexed frame belongs on the task stack.
struct PixelDemoView {
    std::array<PixelAvatarFrame, 2> frames{};
    CODY2D::TextLine heading{};
    CODY2D::TextLine caption{};
    const CODY2D::CapsuleControl *record = nullptr;
    DamageRect damage{};
    uint64_t elapsed_us = 0;
    uint64_t reaction_started_us = 0;
    int64_t observed_active_us = 0;
    int64_t touch_started_us = 0;
    int64_t touch_sampled_us = 0;
    CODY2D::Point touch_origin{};
    uint32_t touch_revision = 0;
    PixelAvatarPose pose = PixelAvatarPose::Waking;
    PixelAvatarPose retired_pose = PixelAvatarPose::Waking;
    uint8_t write_buffer = 0;
    bool visible = false;
    bool retired_valid = false;
    bool reaction_active = false;
    bool touch_candidate = false;
};
static_assert(sizeof(PixelDemoView) <= 9 * 1024, "Keep demo grids in the display owner's PSRAM budget");

// view: Display-owned demo, including both indexed grids and retained glyphs.
// record: Existing Device action whose geometry and admission are preserved.
// active_us: Existing awake clock, never a sensor or a fabricated device state.
// content: Prepared Device content whose callback is replaced by the explicit demo.
// Exception: Preserves native text preparation errors and rejects a reversed clock.
esp_err_t prepare_pixel_demo(PixelDemoView &view, const CODY2D::CapsuleControl &record,
    int64_t active_us, SceneContent &content);

// view: Successfully displayed demo whose working grid becomes the retired reference.
void retire_pixel_demo(PixelDemoView &view);

// view: Hidden or invalidated demo; retain its pose clock but discard touch ownership.
void hide_pixel_demo(PixelDemoView &view);

// view: Display-owned contact and reaction state.
// point: Acquired logical coordinate; release coordinates are never used for travel.
// pressed: Real acquired contact state.
// pressed_edge: First acquired pressed sample of this contact.
// sampled_at_us: Original touch acquisition time.
// display_revision: Current retired panel/touch mapping identity.
// eligible: Visible demo and a contact that belongs to no explicit control.
void observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us,
    uint32_t display_revision, bool eligible);

} // namespace CODY
