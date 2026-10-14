/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "CODY_scene.hpp"
#include "CODY_activity_description.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_identity.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <limits>
#include "scene_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_scene";
constexpr int32_t kCenter = 233;
constexpr int32_t kRadiusSquared = kCenter * kCenter;
constexpr uint32_t kActivityCycleUs = kActivityOverlayCycleUs;
constexpr uint32_t kResponseAccentUs = kActivityOverlayResponseUs;
constexpr uint32_t kActivityPaintStepUs = 50000;
// state: Typed presentation identity admitted by the authenticated activity owner.
bool valid_activity_overlay_state(ActivityOverlayState state)
{
    return state == ActivityOverlayState::Hidden || state == ActivityOverlayState::Working ||
        state == ActivityOverlayState::Waiting || state == ActivityOverlayState::ResponseReady ||
        state == ActivityOverlayState::Compacting;
}

// frame: Active-time overlay state; completed response accents stop producing damage.
uint32_t activity_paint_tick(const ActivityOverlayFrame &frame)
{
    if (frame.state == ActivityOverlayState::Hidden) {
        return 0;
    }
    return std::min(frame.elapsed_us,
        frame.state == ActivityOverlayState::ResponseReady ? kResponseAccentUs : kActivityCycleUs) /
        kActivityPaintStepUs;
}

// bounds: Nonempty half-open logical rectangle, including every visible content pixel.
bool inside_circle(const DamageRect &bounds)
{
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom ||
        bounds.right > 466 || bounds.bottom > 466) {
        return false;
    }
    for (int32_t row : {static_cast<int32_t>(bounds.top), static_cast<int32_t>(bounds.bottom - 1U)}) {
        for (int32_t column : {static_cast<int32_t>(bounds.left), static_cast<int32_t>(bounds.right - 1U)}) {
            const int32_t dx = column - kCenter;
            const int32_t dy = row - kCenter;
            if (dx * dx + dy * dy > kRadiusSquared) {
                return false;
            }
        }
    }
    return true;
}

// line: Prepared full-canvas text layout with a nonempty bounded extent.
DamageRect text_bounds(const CODY2D::TextLine &line)
{
    return {line.bounds.x, line.bounds.y,
        static_cast<uint16_t>(line.bounds.x + line.bounds.width),
        static_cast<uint16_t>(line.bounds.y + line.bounds.height)};
}
} // namespace

// frame: Prepared scene retaining the absolute activity layer and its active clock.
// state: Authenticated activity or Hidden when unavailable, stale, disconnected or idle.
// Exception: Returns invalid scene or activity identity without changing the frame.
esp_err_t set_scene_activity_overlay(SceneFrame &frame, ActivityOverlayState state)
{
    if (!frame.valid || frame.definition == nullptr || !valid_activity_overlay_state(state)) {
        ESP_LOGE(kTag, "Activity overlay state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (frame.activity_overlay.state != state) {
        frame.activity_overlay = {state, 0};
    }
    return ESP_OK;
}

// frame: Serialized current scene receiving bounded identity visual keys for damage planning.
// identity: Stable route-owned native captions, or nullptr to clear the identity and timer.
// revision: Monotonic owner generation changed by new identity text, retained across view resets.
void capture_scene_activity_identity(SceneFrame &frame, const ActivityIdentityState *identity, uint32_t revision)
{
    // Retired frames keep their numeric visual keys when current route-owned text advances.
    frame.activity_identity = identity;
    frame.activity_identity_revision = identity ? revision : 0;
    frame.activity_token_metric = identity ? identity->token_metric : HomeTokenMetric::LastCall;
    frame.activity_identity_timer_seconds = identity ? identity->timer_seconds : 0;
    frame.activity_identity_timer_visible = identity && identity->timer_visible;
    frame.activity_identity_offset = identity ? activity_identity_offset(*identity) : 0;
    frame.activity_identity_opacity = identity ? identity->opacity : 0;
    frame.activity_identity_kind = identity ? identity->selected : ActivityIdentityKind::Project;
    frame.activity_cost_band = identity ? identity->cost_band : LUNASHELL::CodexCostBand::Unknown;
    frame.activity_cost_visible = identity && identity->selected == ActivityIdentityKind::Model &&
        identity->opacity != 0 && identity->project_line.valid && identity->cost_line.valid;
}

// identity: Named generic scene, not a table position; demo has its own existing route.
// definition: Receives an immutable flash-owned definition on success only.
esp_err_t find_scene(SceneId identity, const SceneDefinition *&definition)
{
    const SceneDefinition *found = nullptr;
    for (const SceneDefinition &candidate : SceneData::definitions) {
        if (candidate.id == identity) {
            if (found != nullptr) {
                ESP_LOGE(kTag, "Duplicate scene identity %u (%s)", to_index(identity),
                    esp_err_to_name(ESP_ERR_INVALID_STATE));
                return ESP_ERR_INVALID_STATE;
            }
            found = &candidate;
        }
    }
    if (found == nullptr) {
        ESP_LOGE(kTag, "Unknown generic scene identity %u (%s)", to_index(identity),
            esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    definition = found;
    return ESP_OK;
}

// definition: Flash-owned cloud/expression/title layout retained by this frame.
// frame: Receives a fresh typewriter and prepared text only after validation succeeds.
esp_err_t begin_scene(const SceneDefinition &definition, SceneFrame &frame)
{
    if (definition.id == SceneId::AnimationDemo || definition.font == nullptr ||
        definition.name == nullptr || definition.name[0] == '\0' ||
        definition.character_interval_ms > 1000 || definition.title_top >= 466 ||
        definition.title.length == 0 || definition.intro.bitmap_fade_out_ms > 10000 ||
        definition.intro.cloud_fade_in_ms > 10000 || definition.entrance.move_ms > 10000 ||
        definition.entrance.text_delay_ms > 10000) {
        ESP_LOGE(kTag, "Scene definition rejected: identity=%u (%s)", to_index(definition.id),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const SceneDefinition *registered = nullptr;
    esp_err_t error = find_scene(definition.id, registered);
    if (error != ESP_OK) {
        return error;
    }
    if (std::strcmp(definition.name, registered->name) != 0) {
        ESP_LOGE(kTag, "Scene identity/name mismatch: id=%u name=%s (%s)",
            to_index(definition.id), definition.name, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    error = validate_rig_pose(definition.face, definition.cloud);
    if (error == ESP_OK && definition.entrance.move_ms != 0) {
        error = validate_rig_pose(definition.face, definition.entrance.from);
    }
    if (error != ESP_OK) {
        return error;
    }
    error = CODY2D::validate_mono_font(*definition.font);
    if (error != ESP_OK) {
        return error;
    }
    const uint32_t width = static_cast<uint32_t>(definition.title.length - 1U) *
        definition.font->advance + definition.font->width;
    if (width > 320) {
        ESP_LOGE(kTag, "Scene title too wide: %lu (%s)", static_cast<unsigned long>(width),
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    SceneFrame prepared{};
    const CODY2D::Point origin{static_cast<int16_t>((466U - width) / 2U),
        static_cast<int16_t>(definition.title_top)};
    error = CODY2D::prepare_text_line(*definition.font, definition.title,
        origin, definition.title_color, prepared.title);
    if (error != ESP_OK) {
        return error;
    }
    if (!inside_circle(text_bounds(prepared.title))) {
        ESP_LOGE(kTag, "Scene title leaves the display circle (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    prepared.definition = &definition;
    prepared.intro_phase = definition.intro.bitmap_fade_out_ms != 0 ? SceneIntroPhase::BitmapFadeOut :
        definition.intro.cloud_fade_in_ms != 0 ? SceneIntroPhase::CloudFadeIn : SceneIntroPhase::Complete;
    prepared.visible_characters = prepared.intro_phase == SceneIntroPhase::Complete &&
        definition.character_interval_ms == 0 && definition.entrance.move_ms == 0 &&
        definition.entrance.text_delay_ms == 0 ? definition.title.length : 0;
    prepared.valid = true;
    frame = prepared;
    return ESP_OK;
}

// frame: Existing valid scene; no definition pointer or layout identity is replaced.
// title: Caller-owned immutable ASCII text, retained through frame retirement.
// Exception: Returns the original state, text or circular-layout preparation error.
esp_err_t set_scene_title(SceneFrame &frame, CODY2D::TextSpan title)
{
    if (!frame.valid || frame.definition == nullptr || frame.definition->font == nullptr ||
        !frame.title.valid || title.length == 0) {
        ESP_LOGE(kTag, "Scene title replacement rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto &definition = *frame.definition;
    const uint32_t width = static_cast<uint32_t>(title.length - 1U) *
        definition.font->advance + definition.font->width;
    if (width > 320) {
        ESP_LOGE(kTag, "Replacement scene title too wide: %lu (%s)",
            static_cast<unsigned long>(width), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    CODY2D::TextLine prepared;
    const CODY2D::Point origin{static_cast<int16_t>((466U - width) / 2U),
        static_cast<int16_t>(definition.title_top)};
    const esp_err_t error = CODY2D::prepare_text_line(*definition.font, title,
        origin, definition.title_color, prepared);
    if (error != ESP_OK) {
        return error;
    }
    if (!inside_circle(text_bounds(prepared))) {
        ESP_LOGE(kTag, "Replacement title leaves circular chord (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    frame.title = prepared;
    frame.visible_characters = frame.intro_phase == SceneIntroPhase::Complete &&
        definition.character_interval_ms == 0 ? title.length : 0;
    frame.character_remainder_us = 0;
    frame.text_elapsed_us = 0;
    return ESP_OK;
}

// frame: Prepared scene with a bounded, explicitly named introduction phase.
// presentation: Updated only after all phase and duration invariants hold.
esp_err_t prepare_scene_presentation(const SceneFrame &frame, ScenePresentation &presentation)
{
    if (!frame.valid || frame.definition == nullptr) {
        ESP_LOGE(kTag, "Scene presentation rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    ScenePresentation prepared{};
    uint32_t duration_us = 0;
    switch (frame.intro_phase) {
    case SceneIntroPhase::BitmapFadeOut:
        duration_us = frame.definition->intro.bitmap_fade_out_ms * 1000U;
        prepared.boot_bitmap_visible = true;
        prepared.content_visible = false;
        break;
    case SceneIntroPhase::CloudFadeIn:
        duration_us = frame.definition->intro.cloud_fade_in_ms * 1000U;
        prepared.content_visible = false;
        break;
    case SceneIntroPhase::Complete:
        if (frame.intro_elapsed_us != 0) {
            ESP_LOGE(kTag, "Completed intro retains a phase clock (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        prepared.content_visible = frame.entrance_elapsed_us >=
            (frame.definition->entrance.move_ms + frame.definition->entrance.text_delay_ms) * 1000U;
        presentation = prepared;
        return ESP_OK;
    default:
        ESP_LOGE(kTag, "Unknown scene intro phase (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_us == 0 || duration_us > 10000000U || frame.intro_elapsed_us >= duration_us) {
        ESP_LOGE(kTag, "Scene intro clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t level = (frame.intro_elapsed_us * 255U + duration_us / 2U) / duration_us;
    prepared.opacity = static_cast<uint8_t>(prepared.boot_bitmap_visible ? 255U - level : level);
    presentation = prepared;
    return ESP_OK;
}

// frame: Current prepared scene; unchanged if the request is invalid.
// active_delta_us: Nonnegative animation time, excluding pause and standby intervals.
// Exception: Returns the real invalid clock or presentation-state error.
esp_err_t advance_scene(SceneFrame &frame, int64_t active_delta_us)
{
    if (!frame.valid || frame.definition == nullptr || !frame.title.valid ||
        frame.title.font == nullptr || active_delta_us < 0 ||
        frame.visible_characters > frame.title.text.length) {
        ESP_LOGE(kTag, "Scene clock rejected: valid=%u delta=%lld (%s)", frame.valid,
            static_cast<long long>(active_delta_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    ScenePresentation presentation{};
    const esp_err_t presentation_error = prepare_scene_presentation(frame, presentation);
    if (presentation_error != ESP_OK) {
        return presentation_error;
    }
    SceneFrame next = frame;
    if (next.activity_overlay.state == ActivityOverlayState::ResponseReady) {
        next.activity_overlay.elapsed_us = static_cast<uint32_t>(std::min<uint64_t>(
            static_cast<uint64_t>(next.activity_overlay.elapsed_us) + active_delta_us, kResponseAccentUs));
    } else if (next.activity_overlay.state != ActivityOverlayState::Hidden) {
        next.activity_overlay.elapsed_us = static_cast<uint32_t>(
            (static_cast<uint64_t>(next.activity_overlay.elapsed_us) + active_delta_us) % kActivityCycleUs);
    }
    int64_t remaining_us = active_delta_us;
    // At most two phases are consumed, even after a delayed frame. Only time
    // left after full visibility belongs to the typewriter clock.
    while (next.intro_phase != SceneIntroPhase::Complete) {
        const uint32_t duration_us = next.intro_phase == SceneIntroPhase::BitmapFadeOut
            ? next.definition->intro.bitmap_fade_out_ms * 1000U
            : next.definition->intro.cloud_fade_in_ms * 1000U;
        const uint32_t available_us = duration_us - next.intro_elapsed_us;
        const uint32_t consumed_us = static_cast<uint32_t>(std::min<int64_t>(remaining_us, available_us));
        next.intro_elapsed_us += consumed_us;
        remaining_us -= consumed_us;
        if (next.intro_elapsed_us < duration_us) {
            frame = next;
            return ESP_OK;
        }
        next.intro_phase = next.intro_phase == SceneIntroPhase::BitmapFadeOut &&
            next.definition->intro.cloud_fade_in_ms != 0
            ? SceneIntroPhase::CloudFadeIn : SceneIntroPhase::Complete;
        next.intro_elapsed_us = 0;
    }
    const uint32_t text_start_us =
        (next.definition->entrance.move_ms + next.definition->entrance.text_delay_ms) * 1000U;
    const uint32_t previous_elapsed_us = next.entrance_elapsed_us;
    // This bounded presentation clock may stop after all text is visible; it is
    // not a sensor timestamp. Saturation prevents long-running screens wrapping.
    next.entrance_elapsed_us = static_cast<uint32_t>(std::min<uint64_t>(
        static_cast<uint64_t>(next.entrance_elapsed_us) + remaining_us,
        std::numeric_limits<uint32_t>::max()));
    if (previous_elapsed_us < text_start_us) {
        remaining_us -= std::min<int64_t>(remaining_us, text_start_us - previous_elapsed_us);
        if (next.entrance_elapsed_us < text_start_us) {
            frame = next;
            return ESP_OK;
        }
    }
    // Text has its own active clock: changing a message must not replay Cody's
    // entrance or expose all later captions immediately on a long-lived screen.
    next.text_elapsed_us += std::min<uint64_t>(static_cast<uint64_t>(remaining_us),
        std::numeric_limits<uint64_t>::max() - next.text_elapsed_us);
    if (next.visible_characters == next.title.text.length) {
        frame = next;
        return ESP_OK;
    }
    const uint32_t interval_us = frame.definition->character_interval_ms * 1000U;
    if (interval_us == 0) {
        next.visible_characters = next.title.text.length;
        next.character_remainder_us = 0;
        frame = next;
        return ESP_OK;
    }
    if (next.character_remainder_us >= interval_us) {
        ESP_LOGE(kTag, "Scene typewriter state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t elapsed_us = static_cast<uint64_t>(remaining_us) + next.character_remainder_us;
    const uint64_t remaining = next.title.text.length - next.visible_characters;
    const uint16_t added = static_cast<uint16_t>(std::min(remaining, elapsed_us / interval_us));
    next.visible_characters += added;
    next.character_remainder_us = next.visible_characters == next.title.text.length ? 0 : elapsed_us % interval_us;
    frame = next;
    return ESP_OK;
}

// frame: Valid generic scene, not a special-case screen number.
// transform: Receives placement only after frame validation succeeds.
// Exception: Returns ESP_ERR_INVALID_STATE for a missing scene definition.
esp_err_t scene_cloud_transform(const SceneFrame &frame, Transform &transform)
{
    if (!frame.valid || frame.definition == nullptr) {
        ESP_LOGE(kTag, "Entrance transform rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const auto &definition = *frame.definition;
    const uint32_t duration_us = definition.entrance.move_ms * 1000U;
    if (duration_us == 0 || frame.entrance_elapsed_us >= duration_us) {
        transform = definition.cloud;
        return ESP_OK;
    }
    const float progress = static_cast<float>(frame.entrance_elapsed_us) / duration_us;
    const float eased = progress * progress * (3.f - 2.f * progress);
    const auto &from = definition.entrance.from;
    transform = {from.x + (definition.cloud.x - from.x) * eased,
        from.y + (definition.cloud.y - from.y) * eased,
        from.scale + (definition.cloud.scale - from.scale) * eased};
    return ESP_OK;
}

// frame: Valid prepared frame; all timing derives from its active presentation clock.
// length: Bounded ASCII character count of this content line.
// preceding_characters: Earlier title/body characters sharing the same cadence.
uint16_t scene_content_characters(const SceneFrame &frame, uint16_t length,
                                 uint16_t preceding_characters)
{
    const auto &definition = *frame.definition;
    const uint32_t start_us = (definition.entrance.move_ms + definition.entrance.text_delay_ms) * 1000U;
    if (frame.intro_phase != SceneIntroPhase::Complete || frame.entrance_elapsed_us < start_us) {
        return 0;
    }
    const uint32_t interval_us = definition.character_interval_ms * 1000U;
    if (interval_us == 0) {
        return length;
    }
    const uint64_t total = frame.text_elapsed_us / interval_us;
    return total <= preceding_characters ? 0 :
        static_cast<uint16_t>(std::min<uint64_t>(length, total - preceding_characters));
}

// frame: Valid prepared scene whose content slot is updated after validation.
// content: Immutable optional content, wholly below the title and within the circle.
// Exception: Rejects invalid content/action/control rectangles before replacing the frame.
esp_err_t set_scene_content(SceneFrame &frame, const SceneContent &content)
{
    for (const auto &bounds : content.control_bounds) {
        const bool empty_control = bounds.left == 0 && bounds.top == 0 && bounds.right == 0 && bounds.bottom == 0;
        if (!empty_control && !inside_circle(bounds)) {
            ESP_LOGE(kTag, "Scene control bounds leave the display (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
    }
    const bool empty = content.draw_rows == nullptr && content.state == nullptr &&
        content.bounds.left == 0 && content.bounds.top == 0 &&
        content.bounds.right == 0 && content.bounds.bottom == 0;
    const auto &action = content.bottom_action_bounds;
    const bool empty_action = action.left == 0 && action.top == 0 && action.right == 0 && action.bottom == 0;
    if (!empty_action && (!inside_circle(action) || action.top < content.bounds.bottom ||
        !content.bottom_action_visible || empty)) {
        ESP_LOGE(kTag, "Scene bottom action bounds rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    if (!frame.valid || !frame.title.valid || content.id != SceneElementId::Content ||
        (!empty && (content.draw_rows == nullptr || content.state == nullptr ||
         !inside_circle(content.bounds) || content.bounds.top < text_bounds(frame.title).bottom))) {
        ESP_LOGE(kTag, "Scene content rejected: id=%u (%s)", to_index(content.id),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    frame.content = content;
    return ESP_OK;
}

// previous: Last completely displayed text/content state, not a partially written frame.
// current: Next prepared scene sharing the same stable scene identity.
// plan: Existing cloud damage; text/content changes are accumulated, never cleared.
// Exception: Returns invalid scene state, presentation or accumulated damage errors.
esp_err_t plan_scene_damage(const SceneFrame &previous, const SceneFrame &current, DirtyPlan &plan)
{
    if (!previous.valid || !current.valid || current.definition == nullptr ||
        !previous.title.valid || !current.title.valid || previous.title.font == nullptr || current.title.font == nullptr ||
        previous.definition != current.definition ||
        previous.visible_characters > previous.title.text.length ||
        current.visible_characters > current.title.text.length) {
        ESP_LOGE(kTag, "Scene damage state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const bool dashboard_changed = previous.home_dashboard != current.home_dashboard;
    const bool legacy_hidden = previous.home_dashboard && current.home_dashboard;
    const auto old_title_bounds = text_bounds(previous.title);
    const auto new_title_bounds = text_bounds(current.title);
    const bool title_changed = previous.title.text.characters != current.title.text.characters ||
        previous.title.text.length != current.title.text.length || previous.title.color != current.title.color ||
        previous.title.font != current.title.font || previous.title.origin.x != current.title.origin.x ||
        previous.title.origin.y != current.title.origin.y || old_title_bounds.left != new_title_bounds.left ||
        old_title_bounds.top != new_title_bounds.top || old_title_bounds.right != new_title_bounds.right ||
        old_title_bounds.bottom != new_title_bounds.bottom;
    if (!legacy_hidden && (dashboard_changed ||
        previous.activity_overlay.state != current.activity_overlay.state ||
        activity_paint_tick(previous.activity_overlay) != activity_paint_tick(current.activity_overlay))) {
        const esp_err_t overlay_error = include_damage(plan, activity_overlay_bounds());
        if (overlay_error != ESP_OK) {
            return overlay_error;
        }
    }
    if (title_changed) {
        // Retitling can re-center or shrink. Clear the entire old extent, not
        // just the new prefix; the last completed frame still borrows old text.
        esp_err_t error = include_damage(plan, text_bounds(previous.title));
        if (error == ESP_OK) {
            error = include_damage(plan, text_bounds(current.title));
        }
        if (error != ESP_OK) {
            return error;
        }
    } else if (previous.visible_characters != current.visible_characters) {
        const uint16_t first = std::min(previous.visible_characters, current.visible_characters);
        const uint16_t last = std::max(previous.visible_characters, current.visible_characters);
        const auto &font = *current.title.font;
        const DamageRect changed{
            static_cast<uint16_t>(current.title.origin.x + first * font.advance),
            static_cast<uint16_t>(current.title.origin.y),
            static_cast<uint16_t>(current.title.origin.x + (last - 1U) * font.advance + font.width),
            static_cast<uint16_t>(current.title.origin.y + font.height)};
        const esp_err_t error = include_damage(plan, changed);
        if (error != ESP_OK) {
            return error;
        }
    }
    const auto &old_content = previous.content;
    const auto &new_content = current.content;
    ScenePresentation old_presentation;
    ScenePresentation new_presentation;
    esp_err_t error = prepare_scene_presentation(previous, old_presentation);
    if (error == ESP_OK) {
        error = prepare_scene_presentation(current, new_presentation);
    }
    if (error != ESP_OK) {
        return error;
    }
    const bool visibility_changed = old_presentation.content_visible != new_presentation.content_visible;
    if (previous.command_hints != current.command_hints ||
        previous.command_hints_revision != current.command_hints_revision ||
        (visibility_changed && (previous.command_hints != nullptr || current.command_hints != nullptr))) {
        const auto &bounds = kHomeCommandHintsBounds;
        error = include_damage(plan, {bounds.x, bounds.y,
            static_cast<uint16_t>(bounds.x + bounds.width),
            static_cast<uint16_t>(bounds.y + bounds.height)});
        if (error != ESP_OK) { return error; }
    }
    // Dashboard layers keep these semantic owners alive for audio and priority decisions.
    // Their hidden timers and text cannot dirty the screen until the legacy view returns.
    if (legacy_hidden) {
        return ESP_OK;
    }
    const bool legacy_visibility_changed = visibility_changed || dashboard_changed;
    if (previous.activity_description != current.activity_description ||
        previous.activity_description_revision != current.activity_description_revision ||
        (legacy_visibility_changed && (previous.activity_description != nullptr || current.activity_description != nullptr))) {
        error = include_damage(plan, {73, 360, 393, 388});
        if (error != ESP_OK) {
            return error;
        }
    }
    if (previous.activity_identity != current.activity_identity ||
        previous.activity_identity_revision != current.activity_identity_revision ||
        previous.activity_token_metric != current.activity_token_metric ||
        previous.activity_identity_offset != current.activity_identity_offset ||
        previous.activity_identity_opacity != current.activity_identity_opacity ||
        previous.activity_identity_kind != current.activity_identity_kind ||
        (legacy_visibility_changed && (previous.activity_identity != nullptr || current.activity_identity != nullptr))) {
        error = include_damage(plan, {73, 300, 393, 328});
        if (error != ESP_OK) {
            return error;
        }
    }
    if (previous.activity_identity != current.activity_identity ||
        previous.activity_cost_band != current.activity_cost_band ||
        previous.activity_cost_visible != current.activity_cost_visible ||
        previous.activity_identity_timer_visible != current.activity_identity_timer_visible ||
        (current.activity_identity_timer_visible &&
         previous.activity_identity_timer_seconds != current.activity_identity_timer_seconds) ||
        (legacy_visibility_changed && (previous.activity_identity != nullptr || current.activity_identity != nullptr))) {
        error = include_damage(plan, {73, 330, 393, 358});
        if (error != ESP_OK) {
            return error;
        }
    }
    if (legacy_visibility_changed ||
        old_content.revision != new_content.revision || old_content.state != new_content.state ||
        old_content.draw_rows != new_content.draw_rows || old_content.bounds.left != new_content.bounds.left ||
        old_content.bounds.top != new_content.bounds.top || old_content.bounds.right != new_content.bounds.right ||
        old_content.bounds.bottom != new_content.bounds.bottom ||
        old_content.bottom_action_bounds.left != new_content.bottom_action_bounds.left ||
        old_content.bottom_action_bounds.top != new_content.bottom_action_bounds.top ||
        old_content.bottom_action_bounds.right != new_content.bottom_action_bounds.right ||
        old_content.bottom_action_bounds.bottom != new_content.bottom_action_bounds.bottom) {
        error = include_damage(plan, old_content.bounds);
        if (error == ESP_OK) {
            error = include_damage(plan, new_content.bounds);
        }
        if (error == ESP_OK) {
            error = include_damage(plan, old_content.bottom_action_bounds);
        }
        if (error == ESP_OK) {
            error = include_damage(plan, new_content.bottom_action_bounds);
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    if (legacy_visibility_changed || old_content.control_revision != new_content.control_revision) {
        for (uint16_t index = 0; index < 2; ++index) {
            error = include_damage(plan, old_content.control_bounds[index]);
            if (error == ESP_OK) {
                error = include_damage(plan, new_content.control_bounds[index]);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    return ESP_OK;
}

// frame: Valid immutable text prefix and optional content for this frame.
// strip: Cloud-composited caller-owned RGB565 tile; this layer does not clear it.
// first_row: Logical tile y origin shared by all layers.
// first_column: Logical tile x origin shared by all layers.
// Exception: Returns real scene, presentation or clipped content drawing failures.
esp_err_t draw_scene_rows(const SceneFrame &frame, CODY2D::Surface strip,
                          uint16_t first_row, uint16_t first_column)
{
    if (!frame.valid || frame.definition == nullptr) {
        ESP_LOGE(kTag, "Scene draw rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    ScenePresentation presentation{};
    const esp_err_t presentation_error = prepare_scene_presentation(frame, presentation);
    if (presentation_error != ESP_OK || !presentation.content_visible) {
        return presentation_error;
    }
    esp_err_t error = frame.definition->title_visible
        ? CODY2D::draw_text_rows(frame.title, frame.visible_characters, strip, first_row, first_column)
        : ESP_OK;
    if (error == ESP_OK && frame.command_hints != nullptr) {
        error = draw_command_hints(*frame.command_hints, strip, first_row, first_column);
    }
    if (frame.home_dashboard) {
        return error;
    }
    if (error == ESP_OK && frame.activity_description != nullptr) {
        error = draw_activity_description(*frame.activity_description, strip, first_row, first_column);
    }
    if (error == ESP_OK && frame.activity_identity != nullptr) {
        error = draw_activity_identity(*frame.activity_identity, strip, first_row, first_column);
    }
    if (error != ESP_OK || frame.content.draw_rows == nullptr) {
        return error;
    }
    const DamageRect slots[]{frame.content.bounds, frame.content.bottom_action_bounds};
    for (const auto &bounds : slots) {
        if (first_row >= bounds.bottom || first_row + strip.height <= bounds.top ||
            first_column >= bounds.right || first_column + strip.width <= bounds.left) {
            continue;
        }
        // Borrow only each slot's intersection with the tile. The separated
        // bottom action keeps every borrowed rectangle within its local chord.
        const uint16_t top = std::max(first_row, bounds.top);
        const uint16_t left = std::max(first_column, bounds.left);
        const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.bottom);
        const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.right);
        const uint32_t offset = static_cast<uint32_t>(top - first_row) * strip.stride + left - first_column;
        CODY2D::Surface content_strip = strip;
        content_strip.pixels += offset;
        content_strip.width = right - left;
        content_strip.height = bottom - top;
        content_strip.pixel_capacity -= offset;
        const esp_err_t content_error = frame.content.draw_rows(frame.content.state, content_strip, top, left);
        if (content_error != ESP_OK) {
            ESP_LOGE(kTag, "Scene content draw failed (%s)", esp_err_to_name(content_error));
            return content_error;
        }
    }
    return ESP_OK;
}
} // namespace CODY
