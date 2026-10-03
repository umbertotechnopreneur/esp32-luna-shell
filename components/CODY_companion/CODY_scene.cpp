#include "CODY_scene.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include "scene_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_scene";
constexpr int32_t kCenter = 233;
constexpr int32_t kRadiusSquared = kCenter * kCenter;

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
        definition.intro.cloud_fade_in_ms > 10000) {
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
        definition.character_interval_ms == 0 ? definition.title.length : 0;
    prepared.valid = true;
    frame = prepared;
    return ESP_OK;
}

// frame: Existing valid scene; no definition pointer or layout identity is replaced.
// title: Caller-owned immutable ASCII text, retained through frame retirement.
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

// frame: Valid prepared scene whose content slot is updated after validation.
// content: Immutable optional content, wholly below the title and within the circle.
esp_err_t set_scene_content(SceneFrame &frame, const SceneContent &content)
{
    const bool empty = content.draw_rows == nullptr && content.state == nullptr &&
        content.bounds.left == 0 && content.bounds.top == 0 &&
        content.bounds.right == 0 && content.bounds.bottom == 0;
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
esp_err_t plan_scene_damage(const SceneFrame &previous, const SceneFrame &current, DirtyPlan &plan)
{
    if (!previous.valid || !current.valid || current.definition == nullptr ||
        !previous.title.valid || !current.title.valid || current.title.font == nullptr ||
        previous.definition != current.definition ||
        previous.visible_characters > previous.title.text.length ||
        current.visible_characters > current.title.text.length) {
        ESP_LOGE(kTag, "Scene damage state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const bool title_changed = previous.title.text.characters != current.title.text.characters ||
        previous.title.text.length != current.title.text.length;
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
    if ((previous.intro_phase == SceneIntroPhase::Complete) !=
            (current.intro_phase == SceneIntroPhase::Complete) ||
        old_content.revision != new_content.revision || old_content.state != new_content.state ||
        old_content.draw_rows != new_content.draw_rows || old_content.bounds.left != new_content.bounds.left ||
        old_content.bounds.top != new_content.bounds.top || old_content.bounds.right != new_content.bounds.right ||
        old_content.bounds.bottom != new_content.bounds.bottom) {
        esp_err_t error = include_damage(plan, old_content.bounds);
        if (error == ESP_OK) {
            error = include_damage(plan, new_content.bounds);
        }
        return error;
    }
    return ESP_OK;
}

// frame: Valid immutable text prefix and optional content for this frame.
// strip: Cloud-composited caller-owned RGB565 tile; this layer does not clear it.
// first_row: Logical tile y origin shared by all layers.
// first_column: Logical tile x origin shared by all layers.
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
    const esp_err_t error = CODY2D::draw_text_rows(frame.title, frame.visible_characters,
        strip, first_row, first_column);
    if (error != ESP_OK || frame.content.draw_rows == nullptr) {
        return error;
    }
    const DamageRect &bounds = frame.content.bounds;
    if (first_row >= bounds.bottom || first_row + strip.height <= bounds.top ||
        first_column >= bounds.right || first_column + strip.width <= bounds.left) {
        return ESP_OK;
    }
    // A content callback cannot paint outside its slot: lend only its intersection
    // with this tile, retaining the caller's stride and byte-order contract.
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
    }
    return content_error;
}
} // namespace CODY
