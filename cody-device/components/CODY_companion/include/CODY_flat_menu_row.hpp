#pragma once

#include "CODY2D_controls.hpp"

namespace CODY {

// control: Prepared semantic icon, optional caption and unchanged touch bounds.
// icon_size: Width and height of the standalone icon in logical pixels.
// strip: Borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original icon, caption or separator drawing error.
inline esp_err_t draw_flat_menu_row(const CODY2D::CapsuleControl &control, uint16_t icon_size,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (control.bounds.width <= 48U || control.bounds.height < 2U || icon_size == 0 ||
        control.icon_center.x < static_cast<int>(icon_size / 2U) ||
        control.icon_center.y < static_cast<int>(icon_size / 2U)) return ESP_ERR_INVALID_ARG;
    const CODY2D::Rect icon_bounds{
        static_cast<uint16_t>(control.icon_center.x - icon_size / 2U),
        static_cast<uint16_t>(control.icon_center.y - icon_size / 2U), icon_size, icon_size};
    const auto color = control.enabled ? control.icon_color : static_cast<uint16_t>(0x9CF3);
    auto error = CODY2D::draw_action_icon_rows(control.icon, icon_bounds, color, strip, first_row, first_column);
    if (error == ESP_OK && control.caption.valid) {
        error = CODY2D::draw_text_rows(control.caption, control.caption.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        const auto &bounds = control.bounds;
        const CODY2D::CapsuleControl divider{
            {static_cast<uint16_t>(bounds.x + 48U), static_cast<uint16_t>(bounds.y + bounds.height - 2U),
             static_cast<uint16_t>(bounds.width - 48U), 2}, {}, 0x2106, 0x2106, true};
        error = CODY2D::draw_capsule_rows(divider, strip, first_row, first_column);
    }
    return error;
}

} // namespace CODY
