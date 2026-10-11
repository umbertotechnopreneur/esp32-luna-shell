#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "CODY2D_text.hpp"
#include "CODY2D_controls.hpp"
#include "CODY_cost_badge.hpp"
#include "esp_log.h"

namespace CODY {

enum class ActivityIdentityKind : uint8_t { Project, Branch, Model };
inline constexpr char kActivityModelPrefix[] = "Model: ";

// Serialized route-owned text; prepared lines borrow these stable buffers through retirement.
struct ActivityIdentityState {
    char project[80]{};
    char branch[80]{};
    char model[144]{};
    char cost_caption[24]{};
    char timer[24]{};
    CODY2D::TextLine project_line{};
    CODY2D::TextLine branch_line{};
    CODY2D::TextLine model_line{};
    CODY2D::TextLine cost_line{};
    CODY2D::TextLine timer_line{};
    int64_t started_at_us = 0;
    uint64_t timer_seconds = 0;
    uint16_t project_width = 0;
    uint16_t branch_width = 0;
    uint16_t model_width = 0;
    uint16_t project_length = 0;
    uint16_t branch_length = 0;
    uint16_t model_length = 0;
    uint16_t project_offset = 0;
    uint16_t branch_offset = 0;
    uint16_t model_offset = 0;
    uint8_t opacity = 255;
    ActivityIdentityKind selected = ActivityIdentityKind::Project;
    LUNASHELL::CodexCostBand cost_band = LUNASHELL::CodexCostBand::Unknown;
    bool timer_visible = false;
    bool attention_active = false;
    bool compaction_timer = false;
};
static_assert(sizeof(ActivityIdentityState) <= 896,
    "Home identity/model/cost captions must stay in one bounded route-owned buffer");

constexpr CODY2D::Rect kActivityIdentityBounds{73, 300, 320, 28};
constexpr CODY2D::Rect kActivityTimerBounds{153, 330, 160, 28};
constexpr CODY2D::Rect kActivityCostRowBounds{73, 330, 320, 28};
constexpr uint16_t kActivityCostChipWidth = 196;
constexpr uint16_t kActivityCostTimerWidth = 116;
constexpr uint16_t kActivityCostTimerCenter = 335;

// font: Validated native font whose measured advance determines the whole caption capacity.
// source: Printable ASCII identity, bounded by its canonical producer.
// prefix: Authored ASCII prefix; empty for projects, lowercase branch label otherwise.
// destination: Stable route-owned full caption, whose scrolling window borrows it.
// line: Receives the native centered line after its caption is prepared.
// color: Native white or WhiteSmoke semantic text color.
// length: Receives the complete bounded caption byte count, including its prefix.
// width: Receives the actual native width of the full caption before viewport clipping.
// maximum: Explicit source-byte limit for a project/branch or a full model identity.
// Exception: Returns genuine metadata or text layout errors without silently repairing input.
template <size_t Size>
inline esp_err_t prepare_activity_identity_caption(const CODY2D::MonoFont &font,
    const char *source, const char *prefix, char (&destination)[Size], CODY2D::TextLine &line,
    uint16_t color, uint16_t &length, uint16_t &width, size_t maximum)
{
    if (!source || !prefix) {
        ESP_LOGE("CODY_identity", "Caption source rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const size_t source_length = std::strlen(source);
    const size_t prefix_length = std::strlen(prefix);
    if (source_length > maximum || source_length + prefix_length >= sizeof(destination)) {
        ESP_LOGE("CODY_identity", "Caption length rejected: source=%u prefix=%u (%s)",
            static_cast<unsigned>(source_length), static_cast<unsigned>(prefix_length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0; index < source_length; ++index) {
        if (source[index] < 32 || source[index] > 126) {
            ESP_LOGE("CODY_identity", "Caption byte rejected: offset=%u (%s)",
                static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (source_length == 0) {
        destination[0] = '\0';
        line = {};
        length = 0;
        width = 0;
        return ESP_OK;
    }
    std::memcpy(destination, prefix, prefix_length);
    std::memcpy(destination + prefix_length, source, source_length + 1U);
    length = static_cast<uint16_t>(source_length + prefix_length);
    width = (length - 1U) * font.advance + font.width;
    const uint16_t capacity = (kActivityIdentityBounds.width + font.advance - 1U) / font.advance + 1U;
    const uint16_t count = std::min(length, capacity);
    const int16_t left = width > kActivityIdentityBounds.width ? kActivityIdentityBounds.x :
        static_cast<int16_t>((466U - width) / 2U);
    return CODY2D::prepare_text_line(font, {destination, count},
        {left, 300}, color, line);
}

// font: Immutable native metrics used to measure each visible glyph window.
// text: Complete stable route-owned caption retained through retirement.
// length: Complete bounded caption count, including its genuine branch prefix.
// width: Full native caption width, measured before clipping.
// offset: Horizontal measured pixel offset; no character estimate replaces native metrics.
// color: Current semantic foreground color including the admitted fade opacity.
// line: Receives the bounded native window without allocating or modifying full text.
// Exception: Returns the original native font or layout failure.
inline esp_err_t prepare_activity_identity_window(const CODY2D::MonoFont &font, const char *text,
    uint16_t length, uint16_t width, uint16_t offset, uint16_t color, CODY2D::TextLine &line)
{
    if (length == 0) {
        line = {};
        return ESP_OK;
    }
    const bool overflow = width > kActivityIdentityBounds.width;
    const uint16_t first = overflow ? offset / font.advance : 0;
    const uint16_t fraction = overflow ? offset % font.advance : 0;
    const uint16_t capacity = (kActivityIdentityBounds.width + font.advance - 1U) / font.advance + 1U;
    const uint16_t count = std::min<uint16_t>(length - first, capacity);
    const int16_t left = overflow ? static_cast<int16_t>(kActivityIdentityBounds.x - fraction) :
        static_cast<int16_t>((466U - width) / 2U);
    return CODY2D::prepare_text_line(font, {text + first, count}, {left, 300}, color, line);
}

// state: Stable route-owned identity buffers, never an independent snapshot cache.
// font: Actual native body font used for both measured captions and elapsed text.
// project: Authenticated project or visibly SIMULATED metadata; empty clears this layer.
// branch: Authenticated selected branch, empty when genuinely unavailable.
// active_time_us: Nonnegative presentation time when the identity changes.
// Exception: Returns original font, metadata or native layout errors.
inline esp_err_t prepare_activity_identity(ActivityIdentityState &state, const CODY2D::MonoFont &font,
    const char *project, const char *branch, int64_t active_time_us)
{
    if (active_time_us < 0 || font.height > kActivityIdentityBounds.height) {
        ESP_LOGE("CODY_identity", "Identity clock or font height rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = CODY2D::validate_mono_font(font);
    if (error == ESP_OK) {
        error = prepare_activity_identity_caption(font, project, "Project: ", state.project, state.project_line,
            0xFFFF, state.project_length, state.project_width, 64);
    }
    if (error == ESP_OK) {
        error = prepare_activity_identity_caption(font, branch, "Branch: ", state.branch, state.branch_line,
            0xF7BE, state.branch_length, state.branch_width, 64);
    }
    if (error != ESP_OK) {
        return error;
    }
    state.started_at_us = active_time_us;
    state.opacity = 255;
    state.selected = ActivityIdentityKind::Project;
    state.project_offset = 0;
    state.branch_offset = 0;
    state.model_offset = 0;
    state.model_width = 0;
    state.model_length = 0;
    state.model[0] = '\0';
    state.model_line = {};
    state.cost_caption[0] = '\0';
    state.cost_line = {};
    state.cost_band = LUNASHELL::CodexCostBand::Unknown;
    state.timer_visible = false;
    state.attention_active = false;
    state.compaction_timer = false;
    state.timer_line = {};
    return ESP_OK;
}

// state: Persistent Home identity owner; changing only the band never restarts its carousel.
// font: Actual native metrics shared with project/branch, chip and timer.
// model: Exact current hook model, empty when unavailable; never inferred from a price row.
// band: Independently current Standard band, or Unknown.
// active_time_us: Nonnegative presentation time; a changed model starts a complete project dwell.
// changed: Receives whether model/cost artwork changed.
// Exception: Returns genuine model, enum, font or text layout failures.
inline esp_err_t set_activity_model_cost(ActivityIdentityState &state, const CODY2D::MonoFont &font,
    const char *model, LUNASHELL::CodexCostBand band, int64_t active_time_us, bool &changed)
{
    changed = false;
    if (!model || active_time_us < 0 || static_cast<uint8_t>(band) > 3 ||
        (model[0] == '\0' && band != LUNASHELL::CodexCostBand::Unknown)) {
        ESP_LOGE("CODY_identity", "Model/cost identity rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const char *previous_model = state.model_length == 0 ? "" : state.model + sizeof(kActivityModelPrefix) - 1U;
    const bool model_changed = std::strcmp(model, previous_model) != 0;
    if (model_changed) {
        const esp_err_t error = prepare_activity_identity_caption(font, model, kActivityModelPrefix,
            state.model, state.model_line, 0xFD20, state.model_length, state.model_width, 128);
        if (error != ESP_OK) { return error; }
        state.model_offset = 0;
        state.started_at_us = active_time_us;
    }
    changed = model_changed || state.cost_band != band || (state.project_line.valid && !state.cost_line.valid);
    state.cost_band = band;
    if (changed && state.project_line.valid) {
        std::strcpy(state.cost_caption, cost_band_text(band));
        const uint16_t length = static_cast<uint16_t>(std::strlen(state.cost_caption));
        const uint16_t width = (length - 1U) * font.advance + font.width;
        if (width > kActivityCostChipWidth - 16 || font.height > 28) {
            ESP_LOGE("CODY_identity", "Cost chip size rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        return CODY2D::prepare_text_line(font, {state.cost_caption, length},
            {static_cast<int16_t>((466U - width) / 2U), static_cast<int16_t>(330 + (28 - font.height) / 2)},
            cost_band_color(band), state.cost_line);
    }
    return ESP_OK;
}

// state: Current typed selection; returns its measured offset for frame retirement/damage.
inline uint16_t activity_identity_offset(const ActivityIdentityState &state)
{
    switch (state.selected) {
    case ActivityIdentityKind::Branch: return state.branch_offset;
    case ActivityIdentityKind::Model: return state.model_offset;
    default: return state.project_offset;
    }
}

// state: Route-owned measured captions with stable native borrowed text storage.
// font: The immutable native font used during identity preparation.
// active_time_us: Pausable UI time; identity changes establish its baseline.
// attention: Genuine question, paused permission or authoritative compaction keeping the project selected.
// operation_elapsed_us: Actual observed work interval or authoritative compaction duration, negative when unavailable.
// changed: Receives whether identity opacity/selection or the visible elapsed second changed.
// Exception: Returns real clock, timer formatting or native text layout errors.
inline esp_err_t advance_activity_identity(ActivityIdentityState &state, const CODY2D::MonoFont &font,
    int64_t active_time_us, bool attention, int64_t operation_elapsed_us, bool &changed)
{
    changed = false;
    if (active_time_us < 0) {
        ESP_LOGE("CODY_identity", "Identity clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto old_selected = state.selected;
    const uint8_t old_opacity = state.opacity;
    const bool old_timer = state.timer_visible;
    const uint64_t old_seconds = state.timer_seconds;
    const uint16_t old_offset = activity_identity_offset(state);
    if (attention != state.attention_active) {
        // Attention freezes selection on the project, while overflow remains readable.
        // Leaving attention resumes with a complete project dwell rather than flashing a branch.
        state.started_at_us = active_time_us;
        state.attention_active = attention;
    }
    constexpr int64_t pixel_step_us = 50000;
    constexpr int64_t start_hold_us = 1200000;
    constexpr int64_t end_hold_us = 1800000;
    // Three bounded borrowed descriptors stay on the stack; all text/glyph storage belongs to state.
    struct Caption {
        ActivityIdentityKind kind;
        const char *text;
        CODY2D::TextLine *line;
        uint16_t *offset;
        uint16_t length;
        uint16_t width;
        uint16_t color;
        int64_t hold_us;
    };
    static_assert(sizeof(Caption) * 3 <= 192, "Home carousel descriptors must remain bounded");
    Caption captions[] = {
        {ActivityIdentityKind::Project, state.project, &state.project_line, &state.project_offset,
            state.project_length, state.project_width, 0xFFFF, 5000000},
        {ActivityIdentityKind::Branch, state.branch, &state.branch_line, &state.branch_offset,
            state.branch_length, state.branch_width, 0xF7BE, 3000000},
        {ActivityIdentityKind::Model, state.model, &state.model_line, &state.model_offset,
            state.model_length, state.model_width, 0xFD20, 4000000}};
    Caption *sequence[3]{};
    size_t count = 0;
    int64_t cycle_us = 0;
    for (auto &caption : captions) {
        *caption.offset = 0;
        if (caption.kind != ActivityIdentityKind::Project && (attention || !state.project_line.valid)) { continue; }
        if (caption.length == 0) { continue; }
        const uint16_t overflow = caption.width > kActivityIdentityBounds.width ? caption.width - kActivityIdentityBounds.width : 0;
        caption.hold_us = std::max(caption.hold_us, start_hold_us + overflow * pixel_step_us + end_hold_us);
        sequence[count++] = &caption;
        cycle_us += caption.hold_us;
    }
    if (count > 1) { cycle_us += static_cast<int64_t>(count) * 500000; }
    int64_t elapsed = cycle_us > 0 ? std::max<int64_t>(0, active_time_us - state.started_at_us) % cycle_us : 0;
    state.opacity = 255;
    Caption *shown = &captions[0];
    int64_t scroll_us = 0;
    for (size_t index = 0; index < count; ++index) {
        auto &caption = *sequence[index];
        const int64_t interval = caption.hold_us + (count > 1 ? 500000 : 0);
        if (elapsed >= interval) { elapsed -= interval; continue; }
        shown = &caption;
        scroll_us = std::min(elapsed, caption.hold_us);
        if (elapsed >= caption.hold_us + 300000) {
            shown = sequence[(index + 1) % count];
            scroll_us = 0;
            state.opacity = static_cast<uint8_t>((elapsed - caption.hold_us - 300000) * 255 / 200000);
        } else if (elapsed >= caption.hold_us + 200000) {
            state.opacity = 0;
        } else if (elapsed >= caption.hold_us) {
            state.opacity = static_cast<uint8_t>((caption.hold_us + 200000 - elapsed) * 255 / 200000);
        }
        break;
    }
    state.selected = shown->kind;
    const uint16_t overflow = shown->width > kActivityIdentityBounds.width ? shown->width - kActivityIdentityBounds.width : 0;
    *shown->offset = scroll_us < start_hold_us ? 0 :
        static_cast<uint16_t>(std::min<int64_t>(overflow, (scroll_us - start_hold_us) / pixel_step_us));
    if (old_selected != state.selected || old_offset != *shown->offset) {
        const esp_err_t error = prepare_activity_identity_window(font, shown->text, shown->length,
            shown->width, *shown->offset, shown->color, *shown->line);
        if (error != ESP_OK) { return error; }
    }
    shown->line->color = CODY2D::fade_rgb565_to_black(shown->color, state.opacity);
    state.timer_visible = state.project_line.valid &&
        (state.compaction_timer ? operation_elapsed_us >= 0 : !attention && operation_elapsed_us >= 10000000);
    if (state.timer_visible) {
        state.timer_seconds = static_cast<uint64_t>(operation_elapsed_us / 1000000);
        // Reposition on the same frame as Model/chip visibility changes, even within one timer second.
        if (!old_timer || old_seconds != state.timer_seconds || !state.timer_line.valid ||
            old_selected != state.selected || (old_opacity == 0) != (state.opacity == 0)) {
            const int count = std::snprintf(state.timer, sizeof(state.timer), "%02llu:%02llu",
                static_cast<unsigned long long>(state.timer_seconds / 60U),
                static_cast<unsigned long long>(state.timer_seconds % 60U));
            if (count <= 0 || count >= static_cast<int>(sizeof(state.timer))) {
                ESP_LOGE("CODY_identity", "Elapsed caption formatting failed (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            const uint16_t width = (count - 1U) * font.advance + font.width;
            const bool cost_visible = state.selected == ActivityIdentityKind::Model && state.opacity != 0 &&
                state.cost_line.valid && state.project_line.valid;
            if (width > (cost_visible ? kActivityCostTimerWidth : kActivityTimerBounds.width)) {
                ESP_LOGE("CODY_identity", "Elapsed caption width rejected: width=%u (%s)", width, esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            const esp_err_t error = CODY2D::prepare_text_line(font,
                {state.timer, static_cast<uint16_t>(count)},
                {static_cast<int16_t>((cost_visible ? kActivityCostTimerCenter : 233U) - width / 2U), 330},
                0xC618, state.timer_line);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    changed = old_selected != state.selected || old_opacity != state.opacity ||
        old_timer != state.timer_visible || (state.timer_visible && old_seconds != state.timer_seconds) ||
        old_offset != activity_identity_offset(state);
    return ESP_OK;
}

// state: Immutable scene-owned identity captured with its current presentation revision.
// strip: Native RGB565 tile whose background and artwork are already composited.
// first_row: Logical y origin shared with the circle and rotation owner.
// first_column: Logical x origin shared with the circle and rotation owner.
// Exception: Returns the original native glyph rendering error.
inline esp_err_t draw_activity_identity(const ActivityIdentityState &state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const auto &line = state.selected == ActivityIdentityKind::Branch ? state.branch_line :
        state.selected == ActivityIdentityKind::Model ? state.model_line : state.project_line;
    esp_err_t error = ESP_OK;
    if (state.opacity != 0 && line.valid) {
        const auto &bounds = kActivityIdentityBounds;
        const uint16_t left = std::max(first_column, bounds.x);
        const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
        const uint16_t top = std::max(first_row, bounds.y);
        const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
        if (left < right && top < bottom) {
            const uint32_t offset = (top - first_row) * strip.stride + left - first_column;
            CODY2D::Surface clipped = strip;
            clipped.pixels += offset;
            clipped.pixel_capacity -= offset;
            clipped.width = right - left;
            clipped.height = bottom - top;
            error = CODY2D::draw_text_rows(line, line.text.length, clipped, top, left);
        }
    }
    if (error == ESP_OK && state.timer_visible && state.timer_line.valid) {
        error = CODY2D::draw_text_rows(state.timer_line, state.timer_line.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK && state.selected == ActivityIdentityKind::Model && state.opacity != 0 &&
        state.project_line.valid && state.cost_line.valid) {
        CODY2D::CapsuleControl chip{};
        // Keep the complete price phrase beside the independent timer within the existing damage row.
        chip.bounds = {static_cast<uint16_t>(state.timer_visible ? 73 : (466 - kActivityCostChipWidth) / 2),
            330, kActivityCostChipWidth, 28};
        const uint16_t color = CODY2D::blend_rgb565(cost_band_color(state.cost_band), 0, state.opacity);
        chip.top_color = CODY2D::blend_rgb565(color, 0, 64);
        chip.bottom_color = CODY2D::blend_rgb565(color, 0, 32);
        chip.border_color = color;
        error = CODY2D::draw_capsule_rows(chip, strip, first_row, first_column);
        if (error == ESP_OK) {
            // One small borrowed line relocates with the timer; its counted text stays in the owner.
            static_assert(sizeof(CODY2D::TextLine) <= 64, "Cost placement must not copy caption storage");
            CODY2D::TextLine caption = state.cost_line;
            caption.color = CODY2D::blend_rgb565(caption.color, 0, state.opacity);
            const uint16_t width = (caption.text.length - 1U) * caption.font->advance + caption.font->width;
            caption.origin.x = static_cast<int16_t>(chip.bounds.x + (chip.bounds.width - width) / 2);
            caption.bounds.x = caption.origin.x;
            error = CODY2D::draw_text_rows(caption, caption.text.length, strip, first_row, first_column);
        }
    }
    return error;
}

} // namespace CODY
