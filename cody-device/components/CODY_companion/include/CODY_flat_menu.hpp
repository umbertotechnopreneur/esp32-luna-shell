#pragma once

#include <cstddef>

#include "CODY_flat_menu_row.hpp"
#include "CODY_ui_components.hpp"

namespace CODY {

// A menu groups independently prepared rows and footer actions without owning them.
class FlatMenu {
public:
    // count: Number of presenter-owned rows.
    // visible: Whether the menu is currently presented.
    // draw_row: Renders one indexed row, including presenter-specific values and animations.
    // Exception: Returns the first native row error.
    template<typename DrawRow>
    static esp_err_t draw_custom(std::size_t count, bool visible, DrawRow draw_row)
    {
        if (!visible) return ESP_OK;
        for (std::size_t index = 0; index < count; ++index) {
            const auto error = draw_row(index);
            if (error != ESP_OK) return error;
        }
        return ESP_OK;
    }

    // controls: Immutable controls retained by the presenter through frame retirement.
    // count: Number of available controls, including optional footer actions.
    // row_count: Prefix drawn as flat icon-and-separator rows.
    // strip: Borrowed logical RGB565 tile.
    // first_row: Logical y origin of the tile.
    // first_column: Logical x origin of the tile.
    // visible: Whether the menu is admitted for drawing; callers also gate its hit targets.
    // Exception: Returns invalid arguments or the first native raster error.
    static esp_err_t draw(const CODY2D::CapsuleControl *controls, std::size_t count, std::size_t row_count,
        CODY2D::Surface strip, uint16_t first_row, uint16_t first_column, bool visible = true)
    {
        if (!visible) return CODY2D::validate_surface(strip);
        if (controls == nullptr || row_count > count) return ESP_ERR_INVALID_ARG;
        for (std::size_t index = 0; index < count; ++index) {
            const auto &control = controls[index];
            if (control.bounds.width == 0) continue;
            const auto error = index < row_count
                ? FlatMenuRow::draw(control, 30, strip, first_row, first_column)
                : UI::draw_text_action(control, strip, first_row, first_column, 16);
            if (error != ESP_OK) return error;
        }
        return ESP_OK;
    }
};

} // namespace CODY
