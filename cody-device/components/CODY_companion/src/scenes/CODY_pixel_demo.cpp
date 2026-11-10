#include "CODY_pixel_demo.hpp"

#include <algorithm>

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
    return draw_pixel_avatar_rows(view->frames[view->write_buffer], avatar_bounds(view->viewport),
        strip, first_row, first_column);
}

} // namespace

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
    if (view.visible) view.elapsed_us += static_cast<uint64_t>(active_us - view.observed_active_us);
    view.observed_active_us = active_us;
    view.visible = true;

    const auto pose = view.showcase.sample(view.elapsed_us / 1000U);
    uint32_t reaction_ms = kPixelAvatarReactionMs;
    if (view.reaction_active) {
        const uint64_t elapsed_ms = (view.elapsed_us - view.reaction_started_us) / 1000U;
        reaction_ms = static_cast<uint32_t>(std::min<uint64_t>(elapsed_ms, kPixelAvatarReactionMs));
        view.reaction_active = reaction_ms < kPixelAvatarReactionMs;
    }
    compose_pixel_avatar(view.frames[view.write_buffer], pose.pose,
        static_cast<uint32_t>(view.elapsed_us / 1000U), pose.pose_ms,
        reaction_ms, 0.0f, pose.working);

    const auto viewport = avatar_bounds(view.viewport);
    const DamageRect mascot_bounds{viewport.x, viewport.y,
        static_cast<uint16_t>(viewport.x + viewport.width),
        static_cast<uint16_t>(viewport.y + viewport.height)};
    if (!view.retired_valid) {
        view.damage = mascot_bounds;
    } else {
        const auto damage = pixel_avatar_damage(view.frames[1U - view.write_buffer],
            view.frames[view.write_buffer], viewport);
        view.damage = {damage.x, damage.y, static_cast<uint16_t>(damage.x + damage.width),
            static_cast<uint16_t>(damage.y + damage.height)};
    }
    content.bounds = mascot_bounds;
    content.state = &view;
    content.draw_rows = draw_mascot_rows;
    return ESP_OK;
}

// view: Fully retired mascot frame whose grid becomes the damage baseline.
void retire_pixel_demo(PixelDemoView &view)
{
    view.write_buffer = static_cast<uint8_t>(1U - view.write_buffer);
    view.retired_valid = true;
    view.damage = {};
}

// view: Hidden or invalidated mascot; retain the clock and discard touch ownership.
void hide_pixel_demo(PixelDemoView &view)
{
    view.visible = false;
    view.retired_valid = false;
    view.reaction_active = false;
    view.touch_candidate = false;
}

// view: Serialized contact/reaction owner; no hardware is read here.
// point: Logical position from a successful acquired report.
// pressed: Current acquired contact state.
// pressed_edge: First sample of a new physical contact.
// sampled_at_us: Original acquisition timestamp, independent of animation time.
// display_revision: Mapping generation of the completely displayed page.
// Returns true only when a tap starts the Muse reaction.
bool observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us, uint32_t display_revision)
{
    if (pressed_edge) {
        view.touch_origin = point;
        view.touch_started_us = sampled_at_us;
        view.touch_sampled_us = sampled_at_us;
        view.touch_revision = display_revision;
        view.touch_candidate = view.visible && view.retired_valid && sampled_at_us > 0 &&
            view.viewport.contains(point.x, point.y);
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
    if (elapsed_us < 30000) return false;
    view.reaction_started_us = view.elapsed_us;
    view.reaction_active = true;
    return true;
}

} // namespace CODY
