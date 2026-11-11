#pragma once

#include "CODY2D_controls.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

// selected: Zero-based selected page.
// count: Number of pages represented by the centered marks.
// strip: Borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// y: Logical top of the four-pixel marks.
// compact: Use narrow marks when the page count belongs to the outer carousel.
// Exception: Returns invalid arguments or the original raster failure.
inline esp_err_t draw_page_marks(uint8_t selected, uint8_t count, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t y = 370, bool compact = false)
{
    if (count == 0 || count > 16 || selected >= count || y > 462) return ESP_ERR_INVALID_ARG;
    const uint16_t mark_width = compact ? 8 : count <= 8 ? 24 : 16;
    const uint16_t width = static_cast<uint16_t>(count * mark_width + (count - 1U) * 3U);
    const uint16_t left = static_cast<uint16_t>((466U - width) / 2U);
    for (uint8_t mark = 0; mark < count; ++mark) {
        const auto color = mark == selected ? Theme::primary_indicator_active : Theme::primary_indicator_inactive;
        const CODY2D::CapsuleControl indicator{{static_cast<uint16_t>(left + mark * (mark_width + 3U)), y, mark_width, 4},
            {}, color, color, true};
        const auto error = CODY2D::draw_capsule_rows(indicator, strip, first_row, first_column);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}

} // namespace CODY
