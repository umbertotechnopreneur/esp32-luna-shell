#pragma once

#include <algorithm>
#include <array>
#include <cstring>

#include "CODY_flat_menu.hpp"
#include "CODY_boot_icons.hpp"
#include "CODY_screen_content.hpp"

namespace CODY {

enum class BootMenuAction : uint8_t { None, Settings, Events, Counters, About };

// Display-owned controls retain their text buffers until the frame retires.
struct BootMenuView {
    std::array<CODY2D::CapsuleControl, 4> rows{};
    char labels[4][20]{};
    uint32_t revision = 1;
    bool events_available = false;
    bool routes_available = false;
    bool settings_available = false;
    bool prepared = false;
    bool animation_phase = false;
    int8_t artwork_x = 0;
    int8_t artwork_y = 0;
};

// state: Immutable menu retained by the display owner.
// strip: Destination RGB565 tile.
// first_row: Tile y origin.
// first_column: Tile x origin.
// Exception: Returns the original menu raster error.
inline esp_err_t draw_boot_menu_rows(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) return ESP_ERR_INVALID_ARG;
    const auto &view = *static_cast<const BootMenuView *>(state);
    const auto surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) return surface_error;
    // Quiet background stars move less than the foreground artwork; captions
    // and hit targets stay fixed. Coordinates remain within the menu content.
    constexpr CODY2D::Point stars[] = {{113, 120}, {351, 123}, {139, 184}, {325, 185},
        {100, 278}, {365, 281}, {132, 350}, {337, 348}, {231, 201}, {233, 297}};
    const auto star_color = CODY2D::rgb565_native_to_stored(0x2108, strip.byte_order);
    for (const auto &star : stars) {
        const int x = star.x + view.artwork_x / 2;
        const int y = star.y + view.artwork_y / 2;
        if (x >= first_column && y >= first_row && x < first_column + strip.width && y < first_row + strip.height)
            strip.pixels[(y - first_row) * strip.stride + (x - first_column)] = star_color;
    }
    return FlatMenu::draw_custom(view.rows.size(), true, [&](size_t index) {
        constexpr BootIcon icons[] = {BootIcon::Settings, BootIcon::Events, BootIcon::Counters, BootIcon::About};
        const auto &row = view.rows[index];
        constexpr int16_t icon_size = 64;
        // Keep the artwork inside its touch target and clear of its caption.
        const auto offset_y = std::clamp<int16_t>(view.artwork_y, -3, 3);
        const int16_t drift = ((index + (view.animation_phase ? 1U : 0U)) % 2U == 0U) ? 1 : -1;
        const CODY2D::Rect bounds{static_cast<uint16_t>(row.icon_center.x - icon_size / 2 + view.artwork_x + drift),
            static_cast<uint16_t>(row.icon_center.y - icon_size / 2 + offset_y), icon_size, icon_size};
        auto error = draw_boot_icon_rows(icons[index], bounds, row.enabled, strip, first_row, first_column);
        if (error == ESP_OK && row.caption.valid)
            error = CODY2D::draw_text_rows(row.caption, row.caption.text.length, strip, first_row, first_column);
        return error;
    });
}

// view: Display-owned row and text storage.
// font: Existing immutable body font.
// events_available: Current authenticated inbox is available.
// routes_available: The existing route owner admits a section change.
// settings_available: The settings owner admits its route.
// content: Receives the borrowed menu scene contract.
// Exception: Returns the original caption or title preparation failure.
inline esp_err_t prepare_boot_menu(BootMenuView &view, const CODY2D::MonoFont &font,
    bool events_available, bool routes_available, bool settings_available, SceneContent &content)
{
    if (!view.prepared || view.events_available != events_available || view.routes_available != routes_available ||
        view.settings_available != settings_available) {
        constexpr const char *labels[] = {"Settings", "Notifications", "Counters", "About"};
        constexpr CODY2D::ActionIcon icons[] = {CODY2D::ActionIcon::Gear,
            CODY2D::ActionIcon::PlanAccount, CODY2D::ActionIcon::Statistics, CODY2D::ActionIcon::PlanPro};
        constexpr CODY2D::Rect bounds[] = {{153, 103, 160, 97}, {74, 200, 151, 97},
            {241, 200, 151, 97}, {153, 297, 160, 97}};
        const bool enabled[] = {settings_available, routes_available && events_available, routes_available, routes_available};
        for (size_t i = 0; i < view.rows.size(); ++i) {
            auto &row = view.rows[i];
            row = {};
            row.bounds = bounds[i];
            row.enabled = enabled[i];
            // Artwork and label use a shared rectangular touch area; there is no drawn capsule.
            row.corner_radius = 1;
            row.icon = icons[i];
            row.icon_color = Theme::accent_cyan;
            row.icon_center = {static_cast<int16_t>(bounds[i].x + bounds[i].width / 2U),
                static_cast<int16_t>(bounds[i].y + 35U)};
            const auto error = FlatMenuRow::prepare_label(font, labels[i],
                {bounds[i].x, static_cast<uint16_t>(bounds[i].y + 71U), bounds[i].width, 26},
                enabled[i] ? Theme::text_primary : Theme::text_muted,
                view.labels[i], sizeof(view.labels[i]), row.caption);
            if (error != ESP_OK) return error;
            uint32_t width = 0;
            const auto measured = CODY2D::measure_text_width(font, row.caption.text, width);
            if (measured != ESP_OK) return measured;
            const auto x = static_cast<uint16_t>(bounds[i].x + (bounds[i].width - width) / 2U);
            row.caption.origin.x = static_cast<int16_t>(x);
            row.caption.bounds.x = x;
        }
        view.events_available = events_available;
        view.routes_available = routes_available;
        view.settings_available = settings_available;
        view.prepared = true;
        if (++view.revision == 0) ++view.revision;
    }
    ScreenHeader header{};
    header.title = {"Menu", 4};
    header.title_bounds = {73, 71, 320, 32};
    return ScreenContent::attach(content, {73, 70, 393, 400}, &view, view.revision, draw_boot_menu_rows, header);
}

// view: Last successfully retired menu controls.
// point: Logical touch position from the same display revision.
// Exception: None; disabled rows are never admitted.
inline BootMenuAction boot_menu_action_at(const BootMenuView &view, CODY2D::Point point)
{
    for (size_t i = 0; i < view.rows.size(); ++i)
        if (CODY2D::capsule_contains(view.rows[i], point)) return static_cast<BootMenuAction>(i + 1U);
    return BootMenuAction::None;
}

} // namespace CODY
