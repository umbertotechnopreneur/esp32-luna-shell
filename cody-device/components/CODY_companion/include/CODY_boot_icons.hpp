#pragma once

#include <algorithm>
#include <cstdint>

#include "CODY2D.hpp"
#include "../../../assets/cody/boot-menu-icons/generated/boot_icons.hpp"

namespace CODY {

enum class BootIcon : uint8_t { Settings, Events, Counters, About };

// icon: Explicit menu action artwork, never inferred from a label.
// bounds: Square logical icon area; the radial menu displays the artwork at 36x36.
// enabled: Disabled artwork is dimmed while its existing hit target remains inactive.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves surface failures and rejects invalid identities or geometry.
inline esp_err_t draw_boot_icon_rows(BootIcon icon, CODY2D::Rect bounds, bool enabled,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    if (bounds.width == 0 || bounds.width != bounds.height || bounds.width > 64 ||
        static_cast<uint32_t>(bounds.x) + bounds.width > 466U ||
        static_cast<uint32_t>(bounds.y) + bounds.height > 466U ||
        static_cast<uint32_t>(first_column) + strip.width > 466U ||
        static_cast<uint32_t>(first_row) + strip.height > 466U) return ESP_ERR_INVALID_ARG;
    const uint8_t *cells = nullptr;
    const uint16_t *palette = nullptr;
    switch (icon) {
    case BootIcon::Settings: cells = BootIconData::settings_cells; palette = BootIconData::settings_palette; break;
    case BootIcon::Events: cells = BootIconData::events_cells; palette = BootIconData::events_palette; break;
    case BootIcon::Counters: cells = BootIconData::counters_cells; palette = BootIconData::counters_palette; break;
    case BootIcon::About: cells = BootIconData::about_cells; palette = BootIconData::about_palette; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    const uint32_t left = std::max<uint32_t>(bounds.x, first_column);
    const uint32_t top = std::max<uint32_t>(bounds.y, first_row);
    const uint32_t right = std::min<uint32_t>(bounds.x + bounds.width, first_column + strip.width);
    const uint32_t bottom = std::min<uint32_t>(bounds.y + bounds.height, first_row + strip.height);
    for (uint32_t y = top; y < bottom; ++y) {
        auto *destination = strip.pixels + (y - first_row) * strip.stride;
        const uint32_t source_y = (y - bounds.y) * 32U / bounds.height;
        for (uint32_t x = left; x < right; ++x) {
            const uint32_t source_x = (x - bounds.x) * 32U / bounds.width;
            const uint32_t cell = source_y * 32U + source_x;
            const uint8_t packed = cells[cell / 2U];
            const uint8_t index = cell % 2U == 0 ? packed >> 4U : packed & 0x0fU;
            if (index == 0) continue;
            auto &pixel = destination[x - first_column];
            const uint16_t back = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            const auto color = enabled ? palette[index] : CODY2D::blend_rgb565(palette[index], back, 96);
            pixel = CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
    return ESP_OK;
}

} // namespace CODY
