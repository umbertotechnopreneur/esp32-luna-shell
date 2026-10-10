#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "CODY2D_text.hpp"
#include "esp_log.h"

namespace CODY {

// Serialized route-owned text; prepared lines borrow these stable buffers through retirement.
struct ActivityIdentityState {
    char project[80]{};
    char branch[80]{};
    char timer[24]{};
    CODY2D::TextLine project_line{};
    CODY2D::TextLine branch_line{};
    CODY2D::TextLine timer_line{};
    int64_t started_at_us = 0;
    uint64_t timer_seconds = 0;
    uint16_t project_width = 0;
    uint16_t branch_width = 0;
    uint16_t project_length = 0;
    uint16_t branch_length = 0;
    uint16_t project_offset = 0;
    uint16_t branch_offset = 0;
    uint8_t opacity = 255;
    bool branch_visible = false;
    bool timer_visible = false;
    bool attention_active = false;
    bool compaction_timer = false;
};

constexpr CODY2D::Rect kActivityIdentityBounds{73, 300, 320, 28};
constexpr CODY2D::Rect kActivityTimerBounds{153, 330, 160, 28};

// font: Validated native font whose measured advance determines the whole caption capacity.
// source: Printable ASCII project or branch, bounded to 64 bytes by its producer.
// prefix: Authored ASCII prefix; empty for projects, lowercase branch label otherwise.
// destination: Stable route-owned full caption, whose scrolling window borrows it.
// line: Receives the native centered line after its caption is prepared.
// color: Native white or WhiteSmoke semantic text color.
// length: Receives the complete bounded caption byte count, including its prefix.
// width: Receives the actual native width of the full caption before viewport clipping.
// Exception: Returns genuine metadata or text layout errors without silently repairing input.
inline esp_err_t prepare_activity_identity_caption(const CODY2D::MonoFont &font,
    const char *source, const char *prefix, char (&destination)[80], CODY2D::TextLine &line,
    uint16_t color, uint16_t &length, uint16_t &width)
{
    if (!source || !prefix) {
        ESP_LOGE("CODY_identity", "Caption source rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const size_t source_length = std::strlen(source);
    const size_t prefix_length = std::strlen(prefix);
    if (source_length > 64U || source_length + prefix_length >= sizeof(destination)) {
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
        error = prepare_activity_identity_caption(font, project, "", state.project, state.project_line,
            0xFFFF, state.project_length, state.project_width);
    }
    if (error == ESP_OK) {
        error = prepare_activity_identity_caption(font, branch, "branch: ", state.branch, state.branch_line,
            0xF7BE, state.branch_length, state.branch_width);
    }
    if (error != ESP_OK) {
        return error;
    }
    state.started_at_us = active_time_us;
    state.opacity = 255;
    state.branch_visible = false;
    state.project_offset = 0;
    state.branch_offset = 0;
    state.timer_visible = false;
    state.attention_active = false;
    state.compaction_timer = false;
    state.timer_line = {};
    return ESP_OK;
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
    const bool old_branch = state.branch_visible;
    const uint8_t old_opacity = state.opacity;
    const bool old_timer = state.timer_visible;
    const uint64_t old_seconds = state.timer_seconds;
    const uint16_t old_project_offset = state.project_offset;
    const uint16_t old_branch_offset = state.branch_offset;
    if (attention != state.attention_active) {
        // Attention freezes selection on the project, while overflow remains readable.
        // Leaving attention resumes with a complete project dwell rather than flashing a branch.
        state.started_at_us = active_time_us;
        state.attention_active = attention;
    }
    const uint16_t project_overflow = state.project_width > kActivityIdentityBounds.width ?
        state.project_width - kActivityIdentityBounds.width : 0;
    const uint16_t branch_overflow = state.branch_width > kActivityIdentityBounds.width ?
        state.branch_width - kActivityIdentityBounds.width : 0;
    constexpr int64_t pixel_step_us = 50000;
    constexpr int64_t start_hold_us = 1200000;
    constexpr int64_t end_hold_us = 1800000;
    const int64_t project_hold_us = std::max<int64_t>(5000000,
        start_hold_us + project_overflow * pixel_step_us + end_hold_us);
    const int64_t branch_hold_us = std::max<int64_t>(3000000,
        start_hold_us + branch_overflow * pixel_step_us + end_hold_us);
    const int64_t branch_fade_in_us = project_hold_us + 300000;
    const int64_t branch_hold_start_us = project_hold_us + 500000;
    const int64_t branch_fade_out_us = branch_hold_start_us + branch_hold_us;
    const int64_t project_fade_in_us = branch_fade_out_us + 300000;
    const int64_t cycle_us = project_hold_us + branch_hold_us + 1000000;
    const bool alternates = !attention && state.project_line.valid && state.branch_line.valid;
    const int64_t elapsed = std::max<int64_t>(0, active_time_us - state.started_at_us) %
        (alternates ? cycle_us : project_hold_us);
    state.branch_visible = false;
    state.opacity = 255;
    if (alternates) {
        if (elapsed >= project_hold_us && elapsed < project_hold_us + 200000) {
            state.opacity = static_cast<uint8_t>((project_hold_us + 200000 - elapsed) * 255 / 200000);
        } else if (elapsed >= project_hold_us + 200000 && elapsed < branch_fade_in_us) {
            state.opacity = 0;
        } else if (elapsed >= branch_fade_in_us && elapsed < branch_hold_start_us) {
            state.branch_visible = true;
            state.opacity = static_cast<uint8_t>((elapsed - branch_fade_in_us) * 255 / 200000);
        } else if (elapsed >= branch_hold_start_us && elapsed < branch_fade_out_us) {
            state.branch_visible = true;
        } else if (elapsed >= branch_fade_out_us && elapsed < branch_fade_out_us + 200000) {
            state.branch_visible = true;
            state.opacity = static_cast<uint8_t>((branch_fade_out_us + 200000 - elapsed) * 255 / 200000);
        } else if (elapsed >= branch_fade_out_us + 200000 && elapsed < project_fade_in_us) {
            state.opacity = 0;
        } else if (elapsed >= project_fade_in_us) {
            state.opacity = static_cast<uint8_t>((elapsed - project_fade_in_us) * 255 / 200000);
        }
    }
    // A long caption completes its whole measured scroll before another identity fades in.
    // Without branch metadata the project alone repeats its dwell/scroll cycle.
    const int64_t project_scroll_us = alternates && elapsed >= project_fade_in_us ? 0 :
        std::min(elapsed, project_hold_us);
    const int64_t branch_scroll_us = std::max<int64_t>(0,
        std::min(elapsed - branch_hold_start_us, branch_hold_us));
    state.project_offset = project_scroll_us < start_hold_us ? 0 :
        static_cast<uint16_t>(std::min<int64_t>(project_overflow,
            (project_scroll_us - start_hold_us) / pixel_step_us));
    state.branch_offset = branch_scroll_us < start_hold_us ? 0 :
        static_cast<uint16_t>(std::min<int64_t>(branch_overflow,
            (branch_scroll_us - start_hold_us) / pixel_step_us));
    const bool selection_changed = old_branch != state.branch_visible;
    if (selection_changed || old_project_offset != state.project_offset || old_branch_offset != state.branch_offset) {
        esp_err_t error = prepare_activity_identity_window(font, state.project, state.project_length,
            state.project_width, state.project_offset, 0xFFFF, state.project_line);
        if (error == ESP_OK) {
            error = prepare_activity_identity_window(font, state.branch, state.branch_length,
                state.branch_width, state.branch_offset, 0xF7BE, state.branch_line);
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    state.project_line.color = CODY2D::fade_rgb565_to_black(0xFFFF, state.opacity);
    state.branch_line.color = CODY2D::fade_rgb565_to_black(0xF7BE, state.opacity);
    state.timer_visible = state.project_line.valid &&
        (state.compaction_timer ? operation_elapsed_us >= 0 : !attention && operation_elapsed_us >= 10000000);
    if (state.timer_visible) {
        state.timer_seconds = static_cast<uint64_t>(operation_elapsed_us / 1000000);
        if (!old_timer || old_seconds != state.timer_seconds || !state.timer_line.valid) {
            const int count = std::snprintf(state.timer, sizeof(state.timer), "%02llu:%02llu",
                static_cast<unsigned long long>(state.timer_seconds / 60U),
                static_cast<unsigned long long>(state.timer_seconds % 60U));
            if (count <= 0 || count >= static_cast<int>(sizeof(state.timer))) {
                ESP_LOGE("CODY_identity", "Elapsed caption formatting failed (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            const uint16_t width = (count - 1U) * font.advance + font.width;
            if (width > kActivityTimerBounds.width) {
                ESP_LOGE("CODY_identity", "Elapsed caption width rejected: width=%u (%s)", width, esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            const esp_err_t error = CODY2D::prepare_text_line(font,
                {state.timer, static_cast<uint16_t>(count)},
                {static_cast<int16_t>((466U - width) / 2U), 330}, 0xC618, state.timer_line);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    changed = old_branch != state.branch_visible || old_opacity != state.opacity ||
        old_timer != state.timer_visible || (state.timer_visible && old_seconds != state.timer_seconds) ||
        (!state.branch_visible && old_project_offset != state.project_offset) ||
        (state.branch_visible && old_branch_offset != state.branch_offset);
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
    const auto &line = state.branch_visible ? state.branch_line : state.project_line;
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
    return error;
}

} // namespace CODY
