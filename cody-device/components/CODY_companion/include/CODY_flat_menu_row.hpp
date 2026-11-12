#pragma once

#include <algorithm>
#include <cstring>

#include "CODY2D_controls.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

// Shared flat row layout and raster behavior. The caller owns text until retirement.
class FlatMenuRow {
public:
// font: Immutable glyph metrics for the row label.
// text: Complete printable caption.
// bounds: Single-line slot; touch geometry belongs to the control.
// color: Caption color from the shared palette.
// storage: Caller-owned bounded buffer retained through frame retirement.
// capacity: Storage size including the terminator.
// line: Receives the measured and, when necessary, ellipsized label.
// Exception: Preserves text measurement failures and rejects undersized storage or slots.
static esp_err_t prepare_label(const CODY2D::MonoFont &font, const char *text, CODY2D::Rect bounds,
    uint16_t color, char *storage, size_t capacity, CODY2D::TextLine &line)
{
    if (text == nullptr || storage == nullptr || capacity < 5U || bounds.height < font.height || text[0] == '\0')
        return ESP_ERR_INVALID_ARG;
    const size_t original = std::strlen(text);
    size_t length = std::min(original, capacity - 1U);
    std::memcpy(storage, text, length);
    storage[length] = '\0';
    bool truncated = length < original;
    for (;;) {
        if (truncated) {
            if (length < 3U) return ESP_ERR_INVALID_SIZE;
            std::memcpy(storage + length - 3U, "...", 3U);
        }
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(font, {storage, static_cast<uint16_t>(length)}, width);
        if (error != ESP_OK) return error;
        if (width <= bounds.width) {
            return CODY2D::prepare_text_line(font, {storage, static_cast<uint16_t>(length)},
                {static_cast<int16_t>(bounds.x), static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2U)},
                color, line);
        }
        if (length <= 3U) return ESP_ERR_INVALID_SIZE;
        --length;
        // Restore the prefix from the original before replacing its last three characters.
        std::memcpy(storage, text, length);
        storage[length] = '\0';
        truncated = true;
    }
}

// control: Prepared semantic icon, optional caption and unchanged touch bounds.
// icon_size: Width and height of the standalone icon in logical pixels.
// strip: Borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original icon, caption or separator drawing error.
static esp_err_t draw(const CODY2D::CapsuleControl &control, uint16_t icon_size,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (control.bounds.width <= 48U || control.bounds.height < 2U || icon_size == 0 ||
        control.icon_center.x < static_cast<int>(icon_size / 2U) ||
        control.icon_center.y < static_cast<int>(icon_size / 2U)) return ESP_ERR_INVALID_ARG;
    const CODY2D::Rect icon_bounds{
        static_cast<uint16_t>(control.icon_center.x - icon_size / 2U),
        static_cast<uint16_t>(control.icon_center.y - icon_size / 2U), icon_size, icon_size};
    const auto color = control.enabled ? control.icon_color : Theme::text_muted;
    auto error = CODY2D::draw_action_icon_rows(control.icon, icon_bounds, color, strip, first_row, first_column);
    if (error == ESP_OK && control.caption.valid) {
        error = CODY2D::draw_text_rows(control.caption, control.caption.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        const auto &bounds = control.bounds;
        const CODY2D::CapsuleControl divider{
            {static_cast<uint16_t>(bounds.x + 48U), static_cast<uint16_t>(bounds.y + bounds.height - 2U),
             static_cast<uint16_t>(bounds.width - 48U), 2}, {}, Theme::chrome_row_separator,
             Theme::chrome_row_separator, true};
        error = CODY2D::draw_capsule_rows(divider, strip, first_row, first_column);
    }
    return error;
}
};

} // namespace CODY
