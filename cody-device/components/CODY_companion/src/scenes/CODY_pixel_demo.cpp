#include "CODY_pixel_demo.hpp"

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "scene_data.hpp"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_pixel_demo";
constexpr uint64_t kPoseDurationUs = 4000000;
constexpr CODY2D::Rect kHeadingBounds{73, 79, 320, 25};
constexpr CODY2D::Rect kCaptionBounds{73, 305, 320, 25};
constexpr DamageRect kDemoBounds{73, 79, 393, 380};

struct DemoPose {
    PixelAvatarPose id;
    const char *caption;
};

// These are authored demonstration states, never projections of chat or audio activity.
constexpr std::array<DemoPose, 5> kPoses{{
    {PixelAvatarPose::Waking, "Demo cycle: Waking"},
    {PixelAvatarPose::Idle, "Demo cycle: Idle"},
    {PixelAvatarPose::Listening, "Demo cycle: Listening"},
    {PixelAvatarPose::Thinking, "Demo cycle: Thinking"},
    {PixelAvatarPose::Reply, "Demo cycle: Reply"},
}};

// caption: Immutable ASCII text retained in flash.
// bounds: Reserved logical text slot inside the baseline safe square.
// color: Native RGB565 glyph color.
// line: Display-owned prepared line borrowing caption storage.
// Exception: Preserves glyph errors and rejects text larger than its reserved slot.
esp_err_t prepare_caption(const char *caption, CODY2D::Rect bounds,
    uint16_t color, CODY2D::TextLine &line)
{
    const CODY2D::TextSpan text{caption, static_cast<uint16_t>(std::strlen(caption))};
    const auto &font = SceneData::body_font;
    uint32_t width = 0;
    const auto error = CODY2D::measure_text_width(font, text, width);
    if (error != ESP_OK) return error;
    if (width > bounds.width || font.height > bounds.height) {
        ESP_LOGE(kTag, "Demo caption exceeds its slot (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, text,
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2U),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)}, color, line);
}

// state: Display-owned prepared demo, borrowed until all submitted strips retire.
// strip: Native RGB565 tile, already cleared by the existing UI compositor.
// first_row: Logical origin of the borrowed tile.
// first_column: Logical horizontal origin of the borrowed tile.
// Exception: Preserves text, avatar and existing action raster errors.
esp_err_t draw_demo_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto *view = static_cast<const PixelDemoView *>(state);
    if (view == nullptr || view->record == nullptr || !view->visible) return ESP_ERR_INVALID_STATE;
    auto error = CODY2D::draw_text_rows(view->heading, view->heading.text.length,
        strip, first_row, first_column);
    if (error == ESP_OK) {
        error = draw_pixel_avatar_rows(view->frames[view->write_buffer], kPixelDemoViewport,
            strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = CODY2D::draw_text_rows(view->caption, view->caption.text.length,
            strip, first_row, first_column);
    }
    if (error == ESP_OK && view->record->enabled) {
        error = CODY2D::draw_capsule_rows(*view->record, strip, first_row, first_column);
    }
    return error;
}

} // namespace

// view: Persistent demo preparation and bounded frame storage.
// record: Existing Device record action; this demo never changes its admission.
// active_us: Awake time supplied by the existing display worker.
// content: Existing Device projection retaining its original revision and action owner.
// Exception: Preserves original preparation errors; negative/reversed time is rejected.
esp_err_t prepare_pixel_demo(PixelDemoView &view, const CODY2D::CapsuleControl &record,
    int64_t active_us, SceneContent &content)
{
    if (active_us < 0 || (view.visible && active_us < view.observed_active_us)) {
        ESP_LOGE(kTag, "Demo clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (view.visible) view.elapsed_us += static_cast<uint64_t>(active_us - view.observed_active_us);
    view.observed_active_us = active_us;
    const auto &pose = kPoses[(view.elapsed_us / kPoseDurationUs) % kPoses.size()];
    if (!view.heading.valid) {
        const auto error = prepare_caption("Cody demo - tap for hearts", kHeadingBounds, 0xC7D9, view.heading);
        if (error != ESP_OK) return error;
    }
    if (!view.caption.valid || view.pose != pose.id) {
        const auto error = prepare_caption(pose.caption, kCaptionBounds, 0xEFFF, view.caption);
        if (error != ESP_OK) return error;
    }
    view.pose = pose.id;
    view.record = &record;
    view.visible = true;
    uint32_t reaction_ms = kPixelAvatarReactionMs;
    if (view.reaction_active) {
        const uint64_t elapsed_ms = (view.elapsed_us - view.reaction_started_us) / 1000U;
        reaction_ms = static_cast<uint32_t>(std::min<uint64_t>(elapsed_ms, kPixelAvatarReactionMs));
        view.reaction_active = reaction_ms < kPixelAvatarReactionMs;
    }
    compose_pixel_avatar(view.frames[view.write_buffer], pose.id,
        static_cast<uint32_t>(view.elapsed_us / 1000U),
        static_cast<uint32_t>((view.elapsed_us % kPoseDurationUs) / 1000U), reaction_ms);
    if (!view.retired_valid) {
        view.damage = kDemoBounds;
    } else {
        const auto damage = pixel_avatar_damage(view.frames[1U - view.write_buffer],
            view.frames[view.write_buffer], kPixelDemoViewport);
        view.damage = {damage.x, damage.y, static_cast<uint16_t>(damage.x + damage.width),
            static_cast<uint16_t>(damage.y + damage.height)};
        if (view.retired_pose != pose.id) {
            // The pose label has a stable envelope, so shorter labels erase old glyphs too.
            view.damage = {kPixelDemoViewport.x, kPixelDemoViewport.y,
                static_cast<uint16_t>(kPixelDemoViewport.x + kPixelDemoViewport.width), 330};
            view.damage.left = std::min<uint16_t>(view.damage.left, kCaptionBounds.x);
            view.damage.right = std::max<uint16_t>(view.damage.right, kCaptionBounds.x + kCaptionBounds.width);
        }
    }
    content.bounds = kDemoBounds;
    content.state = &view;
    content.draw_rows = draw_demo_rows;
    return ESP_OK;
}

// view: Demo whose complete native frame has retired successfully.
void retire_pixel_demo(PixelDemoView &view)
{
    // Swap ownership rather than copying either 4096-byte grid onto a nested stack.
    view.write_buffer = static_cast<uint8_t>(1U - view.write_buffer);
    view.retired_pose = view.pose;
    view.retired_valid = true;
    view.damage = {};
}

// view: Hidden or invalidated presentation; no clock advances while hidden.
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
// eligible: Existing navigation/control admission permits a passive visual reaction.
void observe_pixel_demo_touch(PixelDemoView &view, CODY2D::Point point,
    bool pressed, bool pressed_edge, int64_t sampled_at_us,
    uint32_t display_revision, bool eligible)
{
    if (pressed_edge) {
        view.touch_origin = point;
        view.touch_started_us = sampled_at_us;
        view.touch_sampled_us = sampled_at_us;
        view.touch_revision = display_revision;
        view.touch_candidate = eligible && view.visible && view.retired_valid && sampled_at_us > 0 &&
            point.x >= kPixelDemoViewport.x && point.x < kPixelDemoViewport.x + kPixelDemoViewport.width &&
            point.y >= kPixelDemoViewport.y && point.y < kPixelDemoViewport.y + kPixelDemoViewport.height;
        return;
    }
    if (!view.touch_candidate) return;
    const int64_t elapsed_us = sampled_at_us - view.touch_started_us;
    const int dx = point.x - view.touch_origin.x;
    const int dy = point.y - view.touch_origin.y;
    if (!eligible || !view.visible || !view.retired_valid || view.touch_revision != display_revision ||
        sampled_at_us <= view.touch_sampled_us || elapsed_us > 450000 ||
        (pressed && dx * dx + dy * dy > 144)) {
        view.touch_candidate = false;
        return;
    }
    view.touch_sampled_us = sampled_at_us;
    if (!pressed) {
        view.touch_candidate = false;
        if (elapsed_us >= 30000) {
            view.reaction_started_us = view.elapsed_us;
            view.reaction_active = true;
        }
    }
}

} // namespace CODY
