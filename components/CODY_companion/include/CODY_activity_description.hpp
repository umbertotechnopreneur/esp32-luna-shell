#pragma once

#include <algorithm>
#include <cstring>
#include "CODY2D_text.hpp"
#include "esp_log.h"

namespace CODY {

// One serialized scene owner retains its text and prepared borrowed window through frame retirement.
struct ActivityDescriptionState {
    char text[193]{};
    CODY2D::TextLine window{};
    uint16_t length = 0;
    uint16_t width = 0;
    uint16_t offset = 0;
    int64_t started_at_us = 0;
};

constexpr CODY2D::Rect kActivityDescriptionBounds{73, 360, 320, 28};

// state: Serialized scene-owned text; it must not move while a prepared window borrows it.
// font: Validated immutable native body font, shared by firmware and host rendering.
// Exception: Returns the original native text layout error for its bounded visible window.
inline esp_err_t prepare_activity_description_window(ActivityDescriptionState &state, const CODY2D::MonoFont &font)
{
    if (state.length == 0) {
        state.window = {};
        return ESP_OK;
    }
    const bool overflow = state.width > kActivityDescriptionBounds.width;
    const uint16_t first = overflow ? state.offset / font.advance : 0;
    const uint16_t fraction = overflow ? state.offset % font.advance : 0;
    const uint16_t capacity = (kActivityDescriptionBounds.width + font.advance - 1U) / font.advance + 1U;
    const uint16_t count = std::min<uint16_t>(state.length - first, capacity);
    const int16_t left = overflow ? static_cast<int16_t>(kActivityDescriptionBounds.x - fraction) :
        static_cast<int16_t>((466U - state.width) / 2U);
    return CODY2D::prepare_text_line(font, {state.text + first, count},
        {left, static_cast<int16_t>(kActivityDescriptionBounds.y)}, 0xFFFF, state.window);
}

// state: Serialized persistent scene owner, never an additional canonical activity cache.
// font: Actual native body font whose metrics determine overflow and text layout.
// text: Authenticated or explicitly SIMULATED bounded ASCII purpose; an empty string clears it.
// active_time_us: Pausable nonnegative scene clock, independent of activity freshness.
// Exception: Returns genuine metadata, clock, font or native text errors without truncation.
inline esp_err_t prepare_activity_description(ActivityDescriptionState &state, const CODY2D::MonoFont &font,
                                              const char *text, int64_t active_time_us)
{
    if (text == nullptr || active_time_us < 0) {
        ESP_LOGE("CODY_progress", "Description rejected: invalid text or active clock (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = std::strlen(text);
    if (length >= sizeof(state.text) || font.height > kActivityDescriptionBounds.height) {
        ESP_LOGE("CODY_progress", "Description rejected: length=%u font_height=%u (%s)",
            static_cast<unsigned>(length), font.height, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t font_error = CODY2D::validate_mono_font(font);
    if (font_error != ESP_OK) {
        return font_error;
    }
    for (size_t index = 0; index < length; ++index) {
        if (text[index] < 32 || text[index] > 126) {
            ESP_LOGE("CODY_progress", "Description rejected: byte_offset=%u (%s)",
                static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (state.length == length && std::strcmp(state.text, text) == 0) {
        // Phase heartbeats and headline changes do not restart the existing purpose's marquee.
        return ESP_OK;
    }
    std::memcpy(state.text, text, length + 1U);
    state.length = static_cast<uint16_t>(length);
    state.width = length == 0 ? 0 : static_cast<uint16_t>((length - 1U) * font.advance + font.width);
    state.offset = 0;
    state.started_at_us = active_time_us;
    return prepare_activity_description_window(state, font);
}

// state: Scene-owned measured purpose; its native window remains borrowed from this owner.
// font: The same immutable font used to prepare this description.
// active_time_us: Pausable nonnegative scene clock; animation does not renew real observation.
// changed: Receives whether the visible native window moved by at least one pixel.
// Exception: Returns invalid clocks or the original native text preparation error.
inline esp_err_t advance_activity_description(ActivityDescriptionState &state, const CODY2D::MonoFont &font,
                                               int64_t active_time_us, bool &changed)
{
    changed = false;
    if (active_time_us < 0) {
        ESP_LOGE("CODY_progress", "Description clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (state.length == 0 || state.width <= kActivityDescriptionBounds.width) {
        return ESP_OK;
    }
    const uint16_t overflow = state.width - kActivityDescriptionBounds.width;
    constexpr int64_t pixel_step_us = 50000;
    constexpr int64_t start_hold_us = 1200000;
    constexpr int64_t end_hold_us = 1800000;
    const int64_t cycle_us = start_hold_us + overflow * pixel_step_us + end_hold_us;
    const int64_t elapsed = std::max<int64_t>(0, active_time_us - state.started_at_us) % cycle_us;
    const uint16_t offset = elapsed < start_hold_us ? 0 :
        static_cast<uint16_t>(std::min<int64_t>(overflow, (elapsed - start_hold_us) / pixel_step_us));
    if (offset == state.offset) {
        return ESP_OK;
    }
    state.offset = offset;
    const esp_err_t error = prepare_activity_description_window(state, font);
    changed = error == ESP_OK;
    return error;
}

// state: Persistent scene-owned native window; rastering never advances its clock.
// strip: Valid RGB565 tile retaining its byte order and stride.
// first_row: Logical tile y origin, shared with rotation and clipping.
// first_column: Logical tile x origin, shared with rotation and clipping.
// Exception: Returns the original native text drawing error, with no writes outside the viewport.
inline esp_err_t draw_activity_description(const ActivityDescriptionState &state, CODY2D::Surface strip,
                                           uint16_t first_row, uint16_t first_column)
{
    if (!state.window.valid) {
        return ESP_OK;
    }
    const auto &bounds = kActivityDescriptionBounds;
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right || top >= bottom) {
        return ESP_OK;
    }
    CODY2D::Surface clipped = strip;
    const size_t skipped = (top - first_row) * strip.stride + left - first_column;
    clipped.pixels += skipped;
    clipped.pixel_capacity -= skipped;
    clipped.width = right - left;
    clipped.height = bottom - top;
    return CODY2D::draw_text_rows(state.window, state.window.text.length, clipped, top, left);
}

} // namespace CODY
