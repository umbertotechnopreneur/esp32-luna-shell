#pragma once

#include "CODY_scene.hpp"
#include "CODY2D_controls.hpp"

namespace CODY {

// All strings and prepared lines are borrowed from the display owner through frame retirement.
struct ScreenHeader {
    CODY2D::TextSpan title{};
    CODY2D::Rect title_bounds{};
    const CODY2D::TextLine *description = nullptr;
    CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
    CODY2D::Rect icon_bounds{};
    uint16_t icon_color = 0;
    bool visible = true;
    bool title_visible = true;
    bool description_visible = true;
    bool icon_visible = true;
};

// A screen shares SceneContent ownership and does not allocate or submit display frames.
class ScreenContent {
public:
    using DrawRows = esp_err_t (*)(const void *, CODY2D::Surface, uint16_t, uint16_t);

    // content: Receives the borrowed, immutable frame contract.
    // bounds: Damage envelope retained by the display owner.
    // state: Owner retained until the full frame retires.
    // revision: Owner generation used to detect damaged content.
    // draw: Body renderer belonging to the caller.
    // header: Optional title, description, icon and visibility policy.
    // Exception: Returns a title preparation error without inventing a fallback.
    static esp_err_t attach(SceneContent &content, DamageRect bounds, const void *state,
        uint32_t revision, DrawRows draw, const ScreenHeader &header)
    {
        content = {};
        if (!header.visible) return ESP_OK;
        if (state == nullptr || draw == nullptr) return ESP_ERR_INVALID_ARG;
        content.bounds = bounds;
        content.state = state;
        content.revision = revision;
        content.draw_rows = draw;
        if (!header.title_visible || header.title.length == 0) return ESP_OK;
        return prepare_styled_title(header.title, header.title_bounds, content.heading, 3, true);
    }

    // header: Prepared decoration whose storage belongs to the calling view.
    // strip: Borrowed logical RGB565 tile.
    // first_row: Logical y origin of the tile.
    // first_column: Logical x origin of the tile.
    // Exception: Preserves the first native icon, text or surface error.
    static esp_err_t draw_decoration(const ScreenHeader &header, CODY2D::Surface strip,
        uint16_t first_row, uint16_t first_column)
    {
        auto error = CODY2D::validate_surface(strip);
        if (error != ESP_OK || !header.visible) return error;
        if (header.icon_visible && header.icon != CODY2D::ActionIcon::None) {
            error = CODY2D::draw_action_icon_rows(header.icon, header.icon_bounds,
                header.icon_color, strip, first_row, first_column);
        }
        if (error == ESP_OK && header.description_visible && header.description != nullptr &&
            header.description->valid) {
            error = CODY2D::draw_text_rows(*header.description, header.description->text.length,
                strip, first_row, first_column);
        }
        return error;
    }
};

} // namespace CODY
