#pragma once

#include <algorithm>
#include <cstring>

#include "CODY2D_text.hpp"

namespace CODY {

// Persistent selected network caption. Prepared windows borrow this storage.
struct SelectedLabel {
    char text[65]{};
    CODY2D::TextLine line{};
    CODY2D::Rect bounds{};
    uint32_t width = 0;
    uint16_t offset = 0;
    int64_t last_update_us = -1;
    int64_t visible_elapsed_us = 0;
    bool previously_visible = false;
};

// label: Persistent selected text and current prepared window.
// font: Native font retained through the frame lifetime.
// Exception: Preserves native measurement and layout errors.
inline esp_err_t prepare_selected_label_window(SelectedLabel &label, const CODY2D::MonoFont &font)
{
    const auto length = std::strlen(label.text);
    CODY2D::TextSpan span{label.text, static_cast<uint16_t>(length)};
    int16_t x = label.bounds.x;
    if (label.width > label.bounds.width) {
        int16_t origin = 0;
        const auto error = CODY2D::select_text_window(font, span, label.offset,
            label.bounds.width, span, origin);
        if (error != ESP_OK) return error;
        x = static_cast<int16_t>(x + origin);
    } else {
        x = static_cast<int16_t>(x + (label.bounds.width - label.width) / 2U);
    }
    return CODY2D::prepare_text_line(font, span,
        {x, static_cast<int16_t>(label.bounds.y + (label.bounds.height - font.height) / 2U)},
        0xF7BE, label.line);
}

// label: Persistent display-owned caption, unchanged if the text is unchanged.
// text: Selected, nonempty printable ASCII caption of at most 64 bytes.
// font: Native font shared with the selected screen.
// bounds: Complete clipped text slot inside the selected button.
// Exception: Rejects invalid text and preserves native font errors.
inline esp_err_t set_selected_label(SelectedLabel &label, const char *text, const CODY2D::MonoFont &font,
    CODY2D::Rect bounds)
{
    if (text == nullptr || bounds.width == 0 || font.height > bounds.height) return ESP_ERR_INVALID_ARG;
    size_t length = 0;
    while (length < sizeof(label.text) && text[length] != '\0') {
        const auto byte = static_cast<unsigned char>(text[length]);
        if (byte < 32 || byte > 126) return ESP_ERR_INVALID_ARG;
        ++length;
    }
    if (length == 0 || length >= sizeof(label.text)) return ESP_ERR_INVALID_SIZE;
    if (label.line.valid && std::strcmp(label.text, text) == 0 && label.bounds.x == bounds.x &&
        label.bounds.y == bounds.y && label.bounds.width == bounds.width && label.bounds.height == bounds.height)
        return ESP_OK;
    uint32_t width = 0;
    const auto error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) return error;
    label = {};
    std::memcpy(label.text, text, length + 1U);
    label.width = width;
    label.bounds = bounds;
    return prepare_selected_label_window(label, font);
}

// label: Selected text, prepared before this update.
// font: Same native font used to prepare the selected caption.
// active_time_us: Pausable display clock, excluding standby.
// visible: Whether the selected scene was the last completely presented frame.
// changed: Receives whether the visible caption moved.
// Exception: Rejects invalid clocks and preserves native layout errors.
inline esp_err_t advance_selected_label(SelectedLabel &label, const CODY2D::MonoFont &font,
    int64_t active_time_us, bool visible, bool &changed)
{
    changed = false;
    if (!label.line.valid || active_time_us < 0) return ESP_ERR_INVALID_ARG;
    if (label.last_update_us >= 0 && active_time_us < label.last_update_us) return ESP_ERR_INVALID_ARG;
    if (visible && label.previously_visible && label.last_update_us >= 0) {
        label.visible_elapsed_us += active_time_us - label.last_update_us;
    }
    label.last_update_us = active_time_us;
    label.previously_visible = visible;
    if (!visible) return ESP_OK;
    if (label.width <= label.bounds.width) return ESP_OK;
    constexpr int64_t pixel_us = 50000;
    constexpr int64_t start_hold_us = 1200000;
    constexpr int64_t end_hold_us = 1800000;
    const int64_t travel = label.width - label.bounds.width;
    const int64_t travel_us = travel * pixel_us;
    const int64_t cycle = start_hold_us + travel_us + end_hold_us + travel_us;
    const int64_t elapsed = label.visible_elapsed_us % cycle;
    int64_t offset = 0;
    if (elapsed >= start_hold_us + travel_us + end_hold_us) {
        offset = travel - (elapsed - start_hold_us - travel_us - end_hold_us) / pixel_us;
    } else if (elapsed >= start_hold_us + travel_us) {
        offset = travel;
    } else if (elapsed >= start_hold_us) {
        offset = (elapsed - start_hold_us) / pixel_us;
    }
    if (label.offset == offset) return ESP_OK;
    label.offset = static_cast<uint16_t>(offset);
    const auto error = prepare_selected_label_window(label, font);
    changed = error == ESP_OK;
    return error;
}

// label: Prepared selected caption, borrowed until its frame retires.
// strip: Native tile in logical orientation.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Preserves the native text drawing error.
inline esp_err_t draw_selected_label(const SelectedLabel &label, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (!label.line.valid) return ESP_OK;
    const auto &bounds = label.bounds;
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right || top >= bottom) return ESP_OK;
    const size_t skipped = (top - first_row) * strip.stride + left - first_column;
    strip.pixels += skipped;
    strip.pixel_capacity -= skipped;
    strip.width = right - left;
    strip.height = bottom - top;
    return CODY2D::draw_text_rows(label.line, label.line.text.length, strip, top, left);
}

} // namespace CODY
