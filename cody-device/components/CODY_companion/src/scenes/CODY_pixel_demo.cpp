#include "CODY_pixel_demo.hpp"

#include <algorithm>
#include <cmath>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_pixel_demo";
// viewport: Logical artwork and tap bounds from the display adapter.
constexpr CODY2D::Rect avatar_bounds(LogicalRect viewport)
{
    return {viewport.x, viewport.y, viewport.width, viewport.height};
}

// state: Display-owned mascot frame, borrowed until all submitted strips retire.
// strip: Native RGB565 tile, already cleared by the existing UI compositor.
// first_row: Logical origin of the borrowed tile.
// first_column: Logical horizontal origin of the borrowed tile.
// Exception: Preserves the avatar raster error.
esp_err_t draw_mascot_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto *view = static_cast<const PixelDemoView *>(state);
    if (view == nullptr || !view->visible) return ESP_ERR_INVALID_STATE;
    return draw_pixel_avatar_rows(view->frames[view->write_buffer], avatar_bounds(view->visual_viewport),
        strip, first_row, first_column);
}

} // namespace

// motion: Motion state owned by the physical display or a separate host preview.
// sample: Canonical or explicitly synthetic acceleration and display orientation.
// now_us: Monotonic observation time after the sample was obtained.
void advance_pixel_demo_motion(PixelDemoMotionState &motion, const PixelDemoMotion &sample, int64_t now_us)
{
    constexpr int64_t kMaximumAgeUs = 500000;
    if (!sample.valid || sample.sampled_at_us <= 0 || now_us < sample.sampled_at_us ||
        now_us - sample.sampled_at_us > kMaximumAgeUs ||
        !std::isfinite(sample.acceleration_x) || !std::isfinite(sample.acceleration_y) ||
        std::fabs(sample.acceleration_x) > 256 || std::fabs(sample.acceleration_y) > 256 ||
        (sample.rendering_rotation_degrees != 0 && sample.rendering_rotation_degrees != 90 &&
         sample.rendering_rotation_degrees != 180 && sample.rendering_rotation_degrees != 270)) {
        motion = {};
        return;
    }
    motion.rendering_rotation_degrees = sample.rendering_rotation_degrees;
    if (motion.primed && sample.sampled_at_us == motion.sampled_at_us) return;
    if (!motion.primed || sample.sampled_at_us < motion.sampled_at_us ||
        sample.sampled_at_us - motion.sampled_at_us > kMaximumAgeUs) {
        motion = {};
        motion.baseline_x = sample.acceleration_x;
        motion.baseline_y = sample.acceleration_y;
        motion.sampled_at_us = sample.sampled_at_us;
        motion.rendering_rotation_degrees = sample.rendering_rotation_degrees;
        motion.primed = true;
        return;
    }

    const float seconds = static_cast<float>(sample.sampled_at_us - motion.sampled_at_us) / 1000000.0f;
    const float baseline_blend = -std::expm1(-seconds / 1.2f);
    motion.baseline_x += (sample.acceleration_x - motion.baseline_x) * baseline_blend;
    motion.baseline_y += (sample.acceleration_y - motion.baseline_y) * baseline_blend;
    // The existing board mapping is panel X = IMU Y and panel Y = IMU X.
    // Keep offsets in panel coordinates so a rotation never corrupts the filter.
    const float panel_x = sample.acceleration_y - motion.baseline_y;
    const float panel_y = sample.acceleration_x - motion.baseline_x;
    const float blend = -std::expm1(-seconds / 0.15f);
    motion.offset_x += (3.0f * std::tanh(-panel_x / 3.0f) - motion.offset_x) * blend;
    motion.offset_y += (3.0f * std::tanh(-panel_y / 3.0f) - motion.offset_y) * blend;
    motion.sampled_at_us = sample.sampled_at_us;
}

// style: Artwork composition receiving bounded motion and gaze offsets.
// motion: Previously advanced display-owner motion state.
void apply_pixel_demo_motion(MuseVisualStyle &style, const PixelDemoMotionState &motion)
{
    float motion_x = motion.offset_x;
    float motion_y = motion.offset_y;
    switch (motion.rendering_rotation_degrees) {
    case 90: motion_x = motion.offset_y; motion_y = -motion.offset_x; break;
    case 180: motion_x = -motion.offset_x; motion_y = -motion.offset_y; break;
    case 270: motion_x = -motion.offset_y; motion_y = motion.offset_x; break;
    default: break;
    }
    style.pose.body_x = std::clamp(style.pose.body_x + motion_x, -8.0f, 8.0f);
    style.pose.body_y = std::clamp(style.pose.body_y + motion_y, -8.0f, 8.0f);
    style.pose.gaze_x = std::clamp(style.pose.gaze_x + motion_x * 0.15f, -1.0f, 1.0f);
    style.pose.gaze_y = std::clamp(style.pose.gaze_y + motion_y * 0.15f, -1.0f, 1.0f);
}

// view: Display-owned motion response, independent of the showcase clock.
// sample: Validated canonical acceleration and applied display orientation.
// now_us: Monotonic observation time after the snapshot read.
void observe_pixel_demo_motion(PixelDemoView &view, const PixelDemoMotion &sample, int64_t now_us)
{
    advance_pixel_demo_motion(view.motion, sample, now_us);
}

esp_err_t set_pixel_demo_viewport(PixelDemoView &view, LogicalRect viewport)
{
    if (viewport.width < kPixelAvatarGridSize || viewport.width != viewport.height ||
        viewport.width > CODY2D::kMaxDimension ||
        static_cast<uint32_t>(viewport.x) + viewport.width > 65535U ||
        static_cast<uint32_t>(viewport.y) + viewport.height > 65535U) {
        ESP_LOGE(kTag, "Mascot viewport rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    view.visual_viewport = viewport;
    return ESP_OK;
}

// view: Display-owned state and two borrowed indexed frames.
// active_us: Existing awake clock, excluding standby.
// content: Mascot-only scene callback and its centered draw bounds.
// Exception: Rejects a negative or reversed clock.
esp_err_t prepare_pixel_demo(PixelDemoView &view, int64_t active_us, SceneContent &content)
{
    if (active_us < 0 || (view.visible && active_us < view.observed_active_us)) {
        ESP_LOGE(kTag, "Mascot clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (view.visible) {
        const auto delta = static_cast<uint64_t>(active_us - view.observed_active_us);
        view.elapsed_us += delta;
        if (view.manual && view.manual_playing) view.manual_elapsed_us += delta;
    }
    view.observed_active_us = active_us;
    view.visible = true;

    auto pose = view.showcase.sample(view.elapsed_us / 1000U);
    if (view.manual) {
        uint32_t pose_ms = static_cast<uint32_t>(std::min<uint64_t>(view.manual_elapsed_us / 1000U, UINT32_MAX));
        if (view.manual_pose == PixelAvatarPose::Sleep) pose_ms = std::min(pose_ms, uint32_t{1020});
        pose = {view.manual_pose, pose_ms, view.manual_working, view.manual_symbol};
    }
    view.prepared_symbol = pose.symbol;
    uint32_t reaction_ms = kPixelAvatarReactionMs;
    if (view.reaction_active) {
        const uint64_t elapsed_ms = (view.elapsed_us - view.reaction_started_us) / 1000U;
        reaction_ms = static_cast<uint32_t>(std::min<uint64_t>(elapsed_ms, kPixelAvatarReactionMs));
        view.reaction_active = reaction_ms < kPixelAvatarReactionMs;
    }
    auto style = view.visual_style;
    style.symbol.symbol = pose.symbol;
    apply_pixel_demo_motion(style, view.motion);
    if (view.manual && !view.manual_playing && view.retired_valid) {
        view.frames[view.write_buffer] = view.frames[1U - view.write_buffer];
    } else {
        compose_pixel_avatar(view.frames[view.write_buffer], pose.pose,
            static_cast<uint32_t>((view.manual ? view.manual_elapsed_us : view.elapsed_us) / 1000U), pose.pose_ms,
            reaction_ms, 0.0f, pose.working, style);
    }

    const auto viewport = avatar_bounds(view.visual_viewport);
    const DamageRect mascot_bounds{viewport.x, viewport.y,
        static_cast<uint16_t>(viewport.x + viewport.width),
        static_cast<uint16_t>(viewport.y + viewport.height)};
    if (!view.retired_valid) {
        view.damage.rectangles[0] = viewport;
        view.damage.count = 1;
    } else if (view.retired_viewport.x != view.visual_viewport.x ||
               view.retired_viewport.y != view.visual_viewport.y ||
               view.retired_viewport.width != view.visual_viewport.width ||
               view.retired_viewport.height != view.visual_viewport.height) {
        // Clear both the prior presentation and the new one; changed cells
        // alone cannot describe a translated or resized artwork.
        const auto old = avatar_bounds(view.retired_viewport);
        const auto left = std::min(old.x, viewport.x);
        const auto top = std::min(old.y, viewport.y);
        const auto right = std::max<uint32_t>(old.x + old.width, viewport.x + viewport.width);
        const auto bottom = std::max<uint32_t>(old.y + old.height, viewport.y + viewport.height);
        view.damage.rectangles[0] = {left, top, static_cast<uint16_t>(right - left),
            static_cast<uint16_t>(bottom - top)};
        view.damage.count = 1;
    } else {
        plan_pixel_avatar_damage(view.frames[1U - view.write_buffer],
            view.frames[view.write_buffer], viewport, view.damage);
    }
    content.bounds = mascot_bounds;
    content.state = &view;
    content.draw_rows = draw_mascot_rows;
    return ESP_OK;
}

// view: Fully retired mascot frame whose grid becomes the damage baseline.
void retire_pixel_demo(PixelDemoView &view)
{
    view.retired_symbol = view.prepared_symbol;
    view.retired_viewport = view.visual_viewport;
    view.write_buffer = static_cast<uint8_t>(1U - view.write_buffer);
    view.retired_valid = true;
    view.damage.count = 0;
}

// view: Hidden or invalidated mascot; retain the clock and discard touch ownership.
void hide_pixel_demo(PixelDemoView &view)
{
    view.visible = false;
    view.retired_valid = false;
    view.reaction_active = false;
    view.touch_candidate = false;
    view.motion = {};
}

// view: Serialized contact owner; no hardware or semantic state is changed here.
// point: Logical position from a successful acquired report.
// pressed: Current acquired contact state.
// pressed_edge: First sample of a new physical contact.
// sampled_at_us: Original acquisition timestamp, independent of animation time.
// display_revision: Mapping generation of the completely displayed page.
// Returns true once for a recognized tap, without starting its visual reaction.
bool observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us, uint32_t display_revision)
{
    if (pressed_edge) {
        view.touch_origin = point;
        view.touch_started_us = sampled_at_us;
        view.touch_sampled_us = sampled_at_us;
        view.touch_revision = display_revision;
        view.touch_candidate = view.visible && view.retired_valid && sampled_at_us > 0 &&
            view.retired_viewport.contains(point.x, point.y);
        return false;
    }
    if (!view.touch_candidate) return false;
    const int64_t elapsed_us = sampled_at_us - view.touch_started_us;
    const int dx = point.x - view.touch_origin.x;
    const int dy = point.y - view.touch_origin.y;
    if (!view.visible || !view.retired_valid || view.touch_revision != display_revision ||
        sampled_at_us <= view.touch_sampled_us || elapsed_us > 450000 ||
        (pressed && dx * dx + dy * dy > 144)) {
        view.touch_candidate = false;
        return false;
    }
    view.touch_sampled_us = sampled_at_us;
    if (pressed) return false;

    view.touch_candidate = false;
    return elapsed_us >= 30000;
}

// view: Visible mascot whose semantic tap was accepted before presentation.
void start_pixel_demo_reaction(PixelDemoView &view)
{
    if (!view.visible || !view.retired_valid) return;
    view.reaction_started_us = view.elapsed_us;
    view.reaction_active = true;
}

} // namespace CODY
