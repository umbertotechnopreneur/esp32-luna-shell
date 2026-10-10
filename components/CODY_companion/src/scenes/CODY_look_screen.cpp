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

#include "CODY_look_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

#include "CODY2D_controls.hpp"
#include "CODY_jelly.hpp"
#include "scene_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_look";
constexpr uint16_t kSelectedTop = 0x1147;
constexpr uint16_t kSelectedBottom = 0x08C4;
constexpr uint16_t kLookNeutralTop = 0x18E5;
constexpr uint16_t kLookNeutralBottom = 0x10A3;
constexpr uint16_t kCaptionWidth = 232;
constexpr CODY2D::Rect kLookItemBase{117, 226, 220, 38};
constexpr uint16_t kLookItemStep = 44;
constexpr uint16_t kLookVisibleRows = 3;
constexpr CODY2D::Rect kLookListViewport{117, 226, 232,
    (kLookVisibleRows - 1U) * kLookItemStep + kLookItemBase.height};
constexpr uint16_t kLookScrollBarLeft = 343;
constexpr uint16_t kLookScrollBarWidth = 4;
constexpr uint16_t kLookDragThreshold = 5;
constexpr size_t kLookItemCapacity = 8;
constexpr CODY2D::Rect kDoneBounds{123, 390, 220, 44};

struct LookItem {
    const char *id = nullptr;
    CODY2D::CapsuleControl control{};
    uint16_t visible_characters = 0;
};
struct LookTextPrefixes {
    bool controls_visible = false;
    uint16_t look_characters[kLookItemCapacity]{};
    uint16_t status_characters = 0;
    uint16_t hint_characters = 0;
    uint16_t visible_characters = 0;
};
struct LookView {
    bool controls_visible = false;
    LookItem look_items[kLookItemCapacity]{};
    const char *selected_look_id = nullptr;
    uint8_t look_count = 0;
    uint16_t look_scroll = 0;
    bool look_list_visible = false;
    LookNavigationContext navigation{};
    uint32_t pending_request_id = 0;
    const char *pending_id = nullptr;
    esp_err_t save_error = ESP_OK;
    char status_text[32]{};
    CODY2D::TextLine status{};
    CODY2D::TextLine hint{};
    CODY2D::CapsuleControl done{};
    uint64_t status_text_started_us = 0;
    uint16_t status_visible_characters = 0;
    uint16_t hint_visible_characters = 0;
    uint32_t text_generation = 0;
    bool choice_confirmed = false;
    bool valid = false;
};

// One active selector, reused by any host. All borrowed text outlives its frame.
#ifdef ESP_PLATFORM
// Preserve initialized controls/text while keeping their lifetime storage in PSRAM.
alignas(LookView) EXT_RAM_BSS_ATTR uint8_t s_view_storage[sizeof(LookView)]{};
LookView &s_view = *new (s_view_storage) LookView{};
#else
LookView s_view;
#endif
uint32_t s_revision = 0;
uint32_t s_text_generation = 0;
bool s_touch_active = false;
bool s_touch_dragged = false;
int16_t s_touch_start_y = 0;
uint16_t s_touch_scroll_start = 0;

// No parameters: create a new text identity without reusing a retired selector's baseline.
// Exception: Returns ESP_ERR_INVALID_STATE if the generation namespace is exhausted.
esp_err_t advance_text_generation()
{
    if (s_text_generation == std::numeric_limits<uint32_t>::max()) {
        ESP_LOGE(kTag, "Look text generation exhausted (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_view.text_generation = ++s_text_generation;
    return ESP_OK;
}

// item: Stable catalog row with its complete prepared control geometry.
// scroll: Current bounded viewport displacement in logical pixels.
bool row_in_viewport(const LookItem &item, uint16_t scroll)
{
    const int32_t top = static_cast<int32_t>(item.control.bounds.y) - scroll;
    const int32_t bottom = top + item.control.bounds.height;
    return top >= kLookListViewport.y &&
        bottom <= kLookListViewport.y + kLookListViewport.height;
}

// scene: Valid selector with an active text clock that never resets during scroll/save.
// prefixes: Receives fixed-capacity counts; no renderer reads the clock during a frame.
// Exception: Returns the real missing-view, caption or regressed-clock error.
esp_err_t prepare_text_prefixes(const SceneFrame &scene, LookTextPrefixes &prefixes)
{
    if (!s_view.valid || !scene.valid || scene.definition == nullptr ||
        scene.definition->id != SceneId::CodyLook || !scene.title.valid ||
        !s_view.look_list_visible || s_view.look_count == 0 ||
        s_view.look_count > kLookItemCapacity || s_view.text_generation == 0) {
        ESP_LOGE(kTag, "Look typewriter has no prepared selector (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    LookTextPrefixes prepared;
    const uint16_t title_length = scene.definition->title_visible ? scene.title.text.length : 0;
    prepared.controls_visible = scene.intro_phase == SceneIntroPhase::Complete &&
        (!scene.definition->title_visible || scene.visible_characters == title_length);
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        const auto &item = s_view.look_items[index];
        if (item.id == nullptr || !item.control.caption.valid) {
            ESP_LOGE(kTag, "Look typewriter row %u is unprepared (%s)", index,
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        // Catalog rows are controls: their complete labels appear together and
        // never extend the narrative typewriter cadence or its audio counts.
        prepared.look_characters[index] = prepared.controls_visible ? item.control.caption.text.length : 0;
    }
    if (s_view.hint.valid && !s_view.status.valid) {
        ESP_LOGE(kTag, "Look retry hint has no status message (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_view.status.valid) {
        if (scene.text_elapsed_us < s_view.status_text_started_us) {
            ESP_LOGE(kTag, "Look status text clock regressed (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        // A new save/result message starts here, even on a long-lived selector.
        // Only the borrowed clock copy changes; Cody's entrance and row clocks stay put.
        SceneFrame status_scene = scene;
        status_scene.text_elapsed_us -= s_view.status_text_started_us;
        prepared.status_characters = scene_content_characters(status_scene, s_view.status.text.length, 0);
        if (s_view.hint.valid) {
            prepared.hint_characters = scene_content_characters(
                status_scene, s_view.hint.text.length, s_view.status.text.length);
        }
        prepared.visible_characters += prepared.status_characters + prepared.hint_characters;
    }
    prefixes = prepared;
    return ESP_OK;
}

// text: Bounded immutable English text, retained through frame retirement.
// top: Logical caption baseline container, entirely inside the shared content chord.
// line: Receives the prepared flash-font layout.
// Exception: Returns ESP_ERR_INVALID_SIZE for empty, unsupported or out-of-canvas text.
esp_err_t prepare_caption(const char *text, uint16_t top, CODY2D::TextLine &line)
{
    const auto &font = SceneData::body_font;
    const size_t length = std::strlen(text);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length > 31 || width > kCaptionWidth || top + font.height > 466) {
        ESP_LOGE(kTag, "Caption rejected: length=%u width=%lu top=%u (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width), top,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(top)}, 0xFFFF, line);
}

// label: Bounded retained text for the footer's current Done, Saving or error state.
// enabled: Whether the footer may request the host's Done destination.
// Exception: Returns the real text-layout error without replacing the prior footer.
esp_err_t prepare_done_control(const char *label, bool enabled)
{
    if (label == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto &font = SceneData::body_font;
    const size_t length = std::strlen(label);
    const uint32_t width = length == 0 ? 0 : (length - 1U) * font.advance + font.width;
    if (length == 0 || length > 31 || width > kDoneBounds.width || font.height > kDoneBounds.height) {
        ESP_LOGE(kTag, "Look footer rejected: length=%u width=%lu (%s)",
                 static_cast<unsigned>(length), static_cast<unsigned long>(width),
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    CODY2D::CapsuleControl prepared{kDoneBounds, {}, kSelectedTop, kSelectedBottom, enabled};
    prepared.border_color = 0x2B4F;
    prepared.icon_color = 0x8F1D;
    prepared.icon = enabled ? CODY2D::ActionIcon::Check : CODY2D::ActionIcon::None;
    const uint16_t text_left = enabled ? kDoneBounds.x + (kDoneBounds.width - width - 36U) / 2U + 36U : kDoneBounds.x;
    // Match onboarding's centered icon-label group using the same font container.
    prepared.icon_center = {static_cast<int16_t>(text_left - 24U),
        static_cast<int16_t>(kDoneBounds.y + (kDoneBounds.height - font.height) / 2U + font.height / 2U)};
    const uint16_t text_width = enabled ? 164 : kDoneBounds.width;
    if (width > text_width) {
        ESP_LOGE(kTag, "Look footer icon/caption does not fit (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t error = CODY2D::prepare_text_line(font,
        {label, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(enabled ? text_left : text_left + (text_width - width) / 2U),
         static_cast<int16_t>(kDoneBounds.y + (kDoneBounds.height - font.height) / 2U)},
        0xFFFF, prepared.caption);
    if (error == ESP_OK) {
        s_view.done = prepared;
    }
    return error;
}

// No parameters: maximum list displacement that still ends on the last authored row.
uint16_t look_scroll_limit()
{
    if (s_view.look_count == 0) {
        return 0;
    }
    const uint32_t content_bottom = kLookItemBase.y +
        static_cast<uint32_t>(s_view.look_count - 1U) * kLookItemStep + kLookItemBase.height;
    const uint32_t viewport_bottom = kLookListViewport.y + kLookListViewport.height;
    return content_bottom > viewport_bottom
        ? static_cast<uint16_t>(content_bottom - viewport_bottom) : 0;
}

// No parameters: build the fixed-capacity list from stable catalog IDs and current preference.
// Exception: Returns the real catalog, font or unsupported-renderer error.
esp_err_t prepare_look_list()
{
#if !CODY_ENABLE_PEARL_JELLY
    ESP_LOGE(kTag, "Screen 11 requires the authored jelly look catalog (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#else
    const size_t count = jelly_skin_count();
    if (count == 0 || count > kLookItemCapacity) {
        ESP_LOGE(kTag, "Screen 11 look count rejected: %u (%s)",
                 static_cast<unsigned>(count), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    JellySkinSelection selected;
    esp_err_t error = selected_jelly_skin(selected);
    if (error != ESP_OK) {
        return error;
    }
    s_view.look_list_visible = true;
    s_view.look_count = static_cast<uint8_t>(count);
    s_view.selected_look_id = selected.id;
    for (size_t index = 0; index < count; ++index) {
        JellySkinSelection entry;
        error = jelly_skin_at(index, entry);
        if (error != ESP_OK) {
            return error;
        }
        const uint16_t row_top = kLookItemBase.y + static_cast<uint16_t>(index) * kLookItemStep;
        auto &item = s_view.look_items[index];
        item.id = entry.id;
        item.control = {
            {kLookItemBase.x, row_top, kLookItemBase.width, kLookItemBase.height},
            {}, kLookNeutralTop, kLookNeutralBottom, true
        };
        const uint16_t caption_top = row_top +
            (kLookItemBase.height - SceneData::body_font.height) / 2U;
        error = prepare_caption(entry.display_name, caption_top, item.control.caption);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
#endif
}

// strip: Existing content-clipped tile retaining its original stride and byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// bounds: Full-canvas rectangle to intersect with this tile.
// color: Native RGB565 fill color.
// Exception: Returns the real destination-surface fill error.
esp_err_t fill_canvas_rect(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
                           CODY2D::Rect bounds, uint16_t color)
{
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    if (right <= left || bottom <= top) {
        return ESP_OK;
    }
    return CODY2D::fill_rect(strip,
        {static_cast<uint16_t>(left - first_column), static_cast<uint16_t>(top - first_row),
         static_cast<uint16_t>(right - left), static_cast<uint16_t>(bottom - top)}, color);
}

// view: Immutable prepared list, selection and bounded scroll position.
// strip: Scene-content tile; only complete rows are admitted inside the list viewport.
// first_row: Logical y origin shared with touch coordinates.
// first_column: Logical x origin shared with touch coordinates.
// Exception: Returns the real invalid-state, surface or text drawing error.
esp_err_t draw_look_list(const LookView &view, CODY2D::Surface strip,
                         uint16_t first_row, uint16_t first_column)
{
    if (!view.look_list_visible || view.look_count == 0 ||
        view.look_count > kLookItemCapacity || view.selected_look_id == nullptr) {
        if (!view.look_list_visible) {
            return ESP_OK;
        }
        ESP_LOGE(kTag, "Look list draw state rejected: count=%u selected=%s (%s)",
                 view.look_count, view.selected_look_id != nullptr ? view.selected_look_id : "<null>",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t clipped_top = std::max(first_row, kLookListViewport.y);
    const uint16_t clipped_left = std::max(first_column, kLookListViewport.x);
    const uint16_t clipped_bottom = std::min<uint16_t>(first_row + strip.height,
        kLookListViewport.y + kLookListViewport.height);
    const uint16_t clipped_right = std::min<uint16_t>(first_column + strip.width,
        kLookListViewport.x + kLookListViewport.width);
    if (clipped_right <= clipped_left || clipped_bottom <= clipped_top) {
        return ESP_OK;
    }
    const uint32_t offset = static_cast<uint32_t>(clipped_top - first_row) * strip.stride +
        clipped_left - first_column;
    CODY2D::Surface list_strip = strip;
    list_strip.pixels += offset;
    list_strip.width = clipped_right - clipped_left;
    list_strip.height = clipped_bottom - clipped_top;
    list_strip.pixel_capacity -= offset;
    for (uint8_t index = 0; index < view.look_count; ++index) {
        const auto &item = view.look_items[index];
        if (item.id == nullptr || !item.control.caption.valid) {
            ESP_LOGE(kTag, "Look row draw state rejected: row=%u id=%s caption=%u (%s)",
                     index, item.id != nullptr ? item.id : "<null>", item.control.caption.valid,
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        // Rendering and touch admit the same whole row; a hidden fragment is never selectable.
        if (!row_in_viewport(item, view.look_scroll)) {
            continue;
        }
        CODY2D::CapsuleControl row = item.control;
        row.bounds.y = static_cast<uint16_t>(row.bounds.y - view.look_scroll);
        row.caption.origin.y = static_cast<int16_t>(row.caption.origin.y - view.look_scroll);
        row.caption.bounds.y = static_cast<uint16_t>(row.caption.bounds.y - view.look_scroll);
        const bool selected = std::strcmp(item.id, view.selected_look_id) == 0;
        row.top_color = selected ? kSelectedTop : kLookNeutralTop;
        row.bottom_color = selected ? kSelectedBottom : kLookNeutralBottom;
        row.border_color = selected ? 0x2B4F : 0x4A8C;
        row.caption.color = 0xFFFF;
        // Complete control captions share the title's reveal gate without typewriter timing.
        const CODY2D::TextLine caption = row.caption;
        row.caption = {};
        esp_err_t error = CODY2D::draw_capsule_rows(row, list_strip, clipped_top, clipped_left);
        if (error == ESP_OK) {
            error = CODY2D::draw_text_rows(caption, item.visible_characters,
                list_strip, clipped_top, clipped_left);
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    const uint16_t limit = look_scroll_limit();
    if (limit == 0) {
        return ESP_OK;
    }
    const CODY2D::Rect track{kLookScrollBarLeft, kLookListViewport.y,
        kLookScrollBarWidth, kLookListViewport.height};
    esp_err_t error = fill_canvas_rect(list_strip, clipped_top, clipped_left, track, kLookNeutralBottom);
    const uint16_t content_height = kLookListViewport.height + limit;
    const uint16_t thumb_height = static_cast<uint16_t>(
        static_cast<uint32_t>(kLookListViewport.height) * kLookListViewport.height / content_height);
    const uint16_t thumb_range = kLookListViewport.height - thumb_height;
    const uint16_t thumb_top = kLookListViewport.y +
        static_cast<uint16_t>(view.look_scroll * thumb_range / limit);
    if (error == ESP_OK) {
        error = fill_canvas_rect(list_strip, clipped_top, clipped_left,
            {kLookScrollBarLeft, thumb_top, kLookScrollBarWidth, thumb_height}, 0x6C33);
    }
    return error;
}

// point: Acquired logical point in the shared display/touch coordinate space.
bool in_list(CODY2D::Point point)
{
    return point.x >= kLookListViewport.x && point.y >= kLookListViewport.y &&
        point.x < kLookListViewport.x + kLookListViewport.width &&
        point.y < kLookListViewport.y + kLookListViewport.height;
}

// state: Module-owned view, immutable until the complete frame has retired.
// strip: Borrowed content-clipped tile; no allocation or display submission.
// first_row: Logical tile origin on the vertical axis.
// first_column: Logical tile origin on the horizontal axis.
// Exception: Returns the original list or status-text drawing error.
esp_err_t draw_content(const void *state, CODY2D::Surface strip,
                       uint16_t first_row, uint16_t first_column)
{
    const auto &view = *static_cast<const LookView *>(state);
    esp_err_t error = view.controls_visible ? draw_look_list(view, strip, first_row, first_column) : ESP_OK;
    if (error == ESP_OK && view.status.valid) {
        error = CODY2D::draw_text_rows(view.status, view.status_visible_characters,
                                      strip, first_row, first_column);
    }
    if (error == ESP_OK && view.hint.valid) {
        error = CODY2D::draw_text_rows(view.hint, view.hint_visible_characters,
                                      strip, first_row, first_column);
    }
    if (error == ESP_OK && view.controls_visible) {
        error = CODY2D::draw_capsule_rows(view.done, strip, first_row, first_column);
    }
    return error;
}
} // namespace

// No parameters: retire view storage between frames; requests own their copied IDs.
void reset_look_screen()
{
    s_view = {};
    cancel_look_touch();
}

// No parameters: a cancelled gesture may neither select nor scroll on a later release.
void cancel_look_touch()
{
    s_touch_active = false;
    s_touch_dragged = false;
    s_touch_start_y = 0;
    s_touch_scroll_start = 0;
}

// scene: Frame that borrows this fixed view until its last tile has retired.
// control_revision: Revision of the host's two lateral controls.
// Exception: Returns a real invalid-state or scene-content error.
esp_err_t attach_look_content(SceneFrame &scene, uint32_t control_revision)
{
    LookTextPrefixes prefixes;
    const esp_err_t prefix_error = prepare_text_prefixes(scene, prefixes);
    if (prefix_error != ESP_OK) {
        return prefix_error;
    }
    // Hosts invoke this only after the previous frame has fully retired.
    s_view.controls_visible = prefixes.controls_visible;
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        s_view.look_items[index].visible_characters = prefixes.look_characters[index];
    }
    s_view.status_visible_characters = prefixes.status_characters;
    s_view.hint_visible_characters = prefixes.hint_characters;
    SceneContent content;
    content.bounds = {113, 226, 353, 386};
    content.bottom_action_bounds = {123, 390, 343, 434};
    content.state = &s_view;
    content.revision = ++s_revision;
    content.control_revision = control_revision;
    content.bottom_action_visible = true;
    content.visible_characters = prefixes.visible_characters;
    content.text_generation = s_view.text_generation;
    content.control_bounds[1] = {kDoneBounds.x, kDoneBounds.y,
        static_cast<uint16_t>(kDoneBounds.x + kDoneBounds.width),
        static_cast<uint16_t>(kDoneBounds.y + kDoneBounds.height)};
    content.draw_rows = draw_content;
    return set_scene_content(scene, content);
}

// scene: Active selector advanced by its host between fully retired frames.
// control_revision: Current host-owned lateral-control revision.
// changed: Preserves prior changes and becomes true when visible text/control state changes.
// Exception: Returns the original selector, clock or scene-content validation error.
esp_err_t update_look_text(SceneFrame &scene, uint32_t control_revision, bool &changed)
{
    LookTextPrefixes prefixes;
    const esp_err_t prefix_error = prepare_text_prefixes(scene, prefixes);
    if (prefix_error != ESP_OK) {
        return prefix_error;
    }
    bool prefix_changed = prefixes.controls_visible != s_view.controls_visible ||
        prefixes.status_characters != s_view.status_visible_characters ||
        prefixes.hint_characters != s_view.hint_visible_characters ||
        prefixes.visible_characters != scene.content.visible_characters;
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        const auto &item = s_view.look_items[index];
        if (row_in_viewport(item, s_view.look_scroll) &&
            prefixes.look_characters[index] != item.visible_characters) {
            prefix_changed = true;
        }
    }
    if (!prefix_changed && scene.content.control_revision == control_revision) {
        return ESP_OK;
    }
    const esp_err_t error = attach_look_content(scene, control_revision);
    if (error == ESP_OK) {
        changed = true;
    }
    return error;
}

// scene: Receives a complete reusable selector with no host-specific route decisions.
// navigation: Stable caller and Done destinations; no future screen is invented.
// Exception: Returns the first real identity, catalog or preparation error.
esp_err_t open_look_screen(SceneFrame &scene, const LookNavigationContext &navigation)
{
    if (s_view.pending_request_id != 0 || navigation.caller == SceneId::CodyLook ||
        navigation.done == SceneId::CodyLook) {
        ESP_LOGE(kTag, "Look entry rejected: caller=%u done=%u pending=%lu (%s)",
                 static_cast<unsigned>(navigation.caller), static_cast<unsigned>(navigation.done),
                 static_cast<unsigned long>(s_view.pending_request_id), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const SceneDefinition *definition = nullptr;
    esp_err_t error = find_scene(navigation.caller, definition);
    if (error == ESP_OK) {
        error = find_scene(navigation.done, definition);
    }
    if (error == ESP_OK) {
        error = find_scene(SceneId::CodyLook, definition);
    }
    if (error != ESP_OK) {
        return error;
    }
    SceneFrame prepared;
    error = begin_scene(*definition, prepared);
    if (error != ESP_OK) {
        return error;
    }
    reset_look_screen();
    s_view.navigation = navigation;
    error = prepare_look_list();
    if (error != ESP_OK) {
        return error;
    }
    error = advance_text_generation();
    if (error != ESP_OK) {
        return error;
    }
    static constexpr char done_label[] = "Done";
    error = prepare_done_control(done_label, false);
    if (error != ESP_OK) {
        return error;
    }
    s_view.valid = true;
    error = attach_look_content(prepared, 0);
    if (error == ESP_OK) {
        scene = prepared;
    }
    return error;
}

// exit: Explicit Back or Done action, not a numeric list position.
// destination: Receives the corresponding identity provided by the host.
// Exception: Rejects unknown actions and departure during an admitted save.
esp_err_t look_exit_destination(LookExit exit, SceneId &destination)
{
    if (!s_view.valid || s_view.pending_request_id != 0) {
        ESP_LOGE(kTag, "Look exit unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    switch (exit) {
    case LookExit::Back:
        destination = s_view.navigation.caller;
        return ESP_OK;
    case LookExit::Done:
        destination = s_view.navigation.done;
        return ESP_OK;
    default:
        ESP_LOGE(kTag, "Unknown look exit %u (%s)",
                 static_cast<unsigned>(exit), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
}

// scene: Current module-owned selector frame, changed only between complete frames.
// point: Last real contact coordinates on release.
// pressed: Actual hardware contact state.
// pressed_edge: First pressed sample, used to admit a new list gesture.
// horizontal_gesture: Navigation owns the gesture; suppress vertical movement and selection.
// result: Explicit redraw and save intent, without performing blocking work.
// Exception: Returns the real state, catalog-row or scene-content error.
esp_err_t handle_look_touch(SceneFrame &scene, CODY2D::Point point, bool pressed,
                            bool pressed_edge, bool horizontal_gesture, LookTouchResult &result)
{
    result = {};
    if (!s_view.valid) {
        ESP_LOGE(kTag, "Touch outside active look screen (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_view.controls_visible) {
        cancel_look_touch();
        return ESP_OK;
    }
    if (s_view.pending_request_id != 0) {
        result.consumed = in_list(point) ||
            (point.x >= kDoneBounds.x && point.y >= kDoneBounds.y &&
             point.x < kDoneBounds.x + kDoneBounds.width &&
             point.y < kDoneBounds.y + kDoneBounds.height);
        return ESP_OK; // No second request was admitted.
    }
    if (pressed_edge && point.x >= kDoneBounds.x && point.y >= kDoneBounds.y &&
        point.x < kDoneBounds.x + kDoneBounds.width &&
        point.y < kDoneBounds.y + kDoneBounds.height) {
        result.consumed = true;
        result.done_requested = CODY2D::capsule_contains(s_view.done, point);
        return ESP_OK;
    }
    if (pressed_edge && in_list(point)) {
        s_touch_active = true;
        s_touch_dragged = false;
        s_touch_start_y = point.y;
        s_touch_scroll_start = s_view.look_scroll;
    }
    if (!s_touch_active) {
        return ESP_OK;
    }
    result.consumed = true;
    if (pressed) {
        const int displacement = s_touch_start_y - point.y;
        if (std::abs(displacement) >= kLookDragThreshold || horizontal_gesture) {
            s_touch_dragged = true;
        }
        if (horizontal_gesture) {
            return ESP_OK;
        }
        const int displacement_limit = look_scroll_limit();
        const int requested_scroll = std::clamp<int>(
            s_touch_scroll_start + displacement, 0, displacement_limit);
        // Whole-row steps keep three complete controls visible throughout the gesture.
        const int scroll = std::min<int>(displacement_limit,
            ((requested_scroll + kLookItemStep / 2U) / kLookItemStep) * kLookItemStep);
        if (scroll != s_view.look_scroll) {
            s_view.look_scroll = static_cast<uint16_t>(scroll);
            result.changed = true;
            const esp_err_t generation_error = advance_text_generation();
            if (generation_error != ESP_OK) {
                return generation_error;
            }
            return attach_look_content(scene, scene.content.control_revision);
        }
        return ESP_OK;
    }
    const bool tap = !s_touch_dragged && !horizontal_gesture && in_list(point);
    cancel_look_touch();
    if (!tap) {
        return ESP_OK;
    }
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        const auto &item = s_view.look_items[index];
        if (item.id == nullptr) {
            ESP_LOGE(kTag, "Look row %u has no identity (%s)", index, esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        if (!row_in_viewport(item, s_view.look_scroll)) {
            continue;
        }
        auto target = item.control;
        target.bounds.y = static_cast<uint16_t>(target.bounds.y - s_view.look_scroll);
        if (CODY2D::capsule_contains(target, point)) {
            if (std::strcmp(item.id, s_view.selected_look_id) != 0) {
                result.save_id = item.id;
            } else {
                s_view.choice_confirmed = true;
                const esp_err_t footer_error = prepare_done_control("Done", true);
                if (footer_error != ESP_OK) {
                    return footer_error;
                }
                result.selection_confirmed = true;
            }
            break;
        }
    }
    return ESP_OK;
}

// scene: Active selector, with the previously saved skin still displayed.
// id: Requested stable catalog ID, copied into the worker request by the host.
// request_id: Nonzero identity used to reject late/unrelated completions.
// Exception: Returns the real state, identity or presentation error; never writes NVS.
esp_err_t begin_look_save(SceneFrame &scene, const char *id, uint32_t request_id)
{
    if (!s_view.valid || s_view.pending_request_id != 0 || id == nullptr || request_id == 0) {
        ESP_LOGE(kTag, "Look save admission rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const char *catalog_id = nullptr;
    for (uint8_t index = 0; index < s_view.look_count; ++index) {
        if (std::strcmp(s_view.look_items[index].id, id) == 0) {
            catalog_id = s_view.look_items[index].id;
            break;
        }
    }
    if (catalog_id == nullptr) {
        ESP_LOGE(kTag, "Look save ID rejected: %s (%s)", id, esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    s_view.pending_id = catalog_id;
    s_view.pending_request_id = request_id;
    s_view.save_error = ESP_OK;
    s_view.hint = {};
    s_view.status = {};
    cancel_look_touch();
    esp_err_t error = prepare_done_control("Saving...", false);
    if (error == ESP_OK) {
        error = advance_text_generation();
    }
    if (error == ESP_OK) {
        error = attach_look_content(scene, scene.content.control_revision);
    }
    return error;
}

// scene: Current selector borrowing fixed view storage.
// request_id: Completed operation, which must match this view's admitted save.
// save_error: Original write result; retained even if error presentation also fails.
// Exception: Returns separate operation/presentation outcomes, never masks a failed save.
LookSaveCompletion complete_look_save(SceneFrame &scene, uint32_t request_id, esp_err_t save_error)
{
    LookSaveCompletion result{save_error, ESP_OK};
    if (!s_view.valid || request_id == 0 || request_id != s_view.pending_request_id ||
        s_view.pending_id == nullptr) {
        ESP_LOGE(kTag, "Stale look save completion: request=%lu (%s)",
                 static_cast<unsigned long>(request_id), esp_err_to_name(ESP_ERR_INVALID_STATE));
        result.presentation_error = ESP_ERR_INVALID_STATE;
        return result;
    }
    s_view.pending_request_id = 0;
    s_view.save_error = save_error;
    s_view.status = {};
    s_view.hint = {};
    if (save_error == ESP_OK) {
        s_view.selected_look_id = s_view.pending_id;
        s_view.choice_confirmed = true;
        result.presentation_error = prepare_done_control("Done", true);
    } else {
        // This is an optional user preference failure, not a renderer failure.
        // The worker has published the original result in DeviceSnapshotService.
        ESP_LOGE(kTag, "Look save failed; previous selection retained (%s)", esp_err_to_name(save_error));
        std::snprintf(s_view.status_text, sizeof(s_view.status_text), "Error: 0x%x",
                      static_cast<unsigned>(save_error));
        // Keep the real error visible without overlapping the fixed Done footer.
        result.presentation_error = prepare_done_control(s_view.status_text, false);
    }
    s_view.pending_id = nullptr;
    if (result.presentation_error == ESP_OK) {
        result.presentation_error = advance_text_generation();
    }
    if (result.presentation_error == ESP_OK) {
        result.presentation_error = attach_look_content(scene, scene.content.control_revision);
    }
    return result;
}
} // namespace CODY
