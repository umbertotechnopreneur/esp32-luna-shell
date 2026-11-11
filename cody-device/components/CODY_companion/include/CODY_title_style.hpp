#pragma once

#include <algorithm>
#include <cstdint>

#include "CODY2D_text.hpp"
#include "CODY_pixel_glyphs.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

// One immutable, borrowed heading for the display owner's retired frame.
struct StyledTitle {
    CODY2D::TextSpan text{};
    CODY2D::Rect bounds{};
    uint16_t shown = 0;
    uint8_t scale = 0;
    bool truncated = false;
};

namespace TitleStyleDetail {

// length: Number of glyphs including spaces.
// scale: Integer pixel size of one source cell.
inline uint32_t width(uint16_t length, uint8_t scale)
{
    return length == 0 ? 0 : (static_cast<uint32_t>(length) * 6U - 1U) * scale;
}

// character: Printable ASCII not covered by the shared device pixel alphabet.
// Returns a matching punctuation glyph, or null for a non-printable character.
inline const pixel_glyphs::MonoGlyph *punctuation(char character)
{
    static constexpr pixel_glyphs::MonoGlyph glyphs[] = {
        {'!', {0, 0, 0x5F, 0, 0}}, {'"', {0, 7, 0, 7, 0}},
        {'#', {0x14, 0x7F, 0x14, 0x7F, 0x14}}, {'$', {0x24, 0x2A, 0x7F, 0x2A, 0x12}},
        {'&', {0x36, 0x49, 0x55, 0x22, 0x50}}, {'\'', {0, 5, 3, 0, 0}},
        {'(', {0, 0x1C, 0x22, 0x41, 0}}, {')', {0, 0x41, 0x22, 0x1C, 0}},
        {'*', {0x14, 8, 0x3E, 8, 0x14}}, {'+', {8, 8, 0x3E, 8, 8}},
        {',', {0, 0x50, 0x30, 0, 0}}, {'.', {0, 0x60, 0x60, 0, 0}},
        {'/', {0x20, 0x10, 8, 4, 2}}, {';', {0, 0x56, 0x36, 0, 0}},
        {'<', {8, 0x14, 0x22, 0x41, 0}}, {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
        {'>', {0, 0x41, 0x22, 0x14, 8}}, {'?', {2, 1, 0x51, 9, 6}},
        {'@', {0x3E, 0x41, 0x5D, 0x55, 0x1E}}, {'[', {0, 0x7F, 0x41, 0x41, 0}},
        {'\\', {2, 4, 8, 0x10, 0x20}}, {']', {0, 0x41, 0x41, 0x7F, 0}},
        {'^', {4, 2, 1, 2, 4}}, {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
        {'`', {0, 1, 2, 4, 0}}, {'{', {0, 8, 0x36, 0x41, 0}},
        {'|', {0, 0, 0x7F, 0, 0}}, {'}', {0, 0x41, 0x36, 8, 0}},
        {'~', {8, 4, 8, 0x10, 8}},
    };
    for (const auto &glyph : glyphs) {
        if (glyph.character == character) return &glyph;
    }
    return nullptr;
}

// character: Printable ASCII glyph to resolve without allocating or copying it.
inline const pixel_glyphs::MonoGlyph *glyph(char character)
{
    const auto *value = pixel_glyphs::find_glyph(character);
    return value != nullptr ? value : punctuation(character);
}

} // namespace TitleStyleDetail

// text: Borrowed printable ASCII caption, kept alive until the display frame retires.
// bounds: Half-open centering and clipping area in logical 466 x 466 coordinates.
// title: Receives the scale and visible layout without copying the caption.
// max_scale: Largest integer glyph scale for this typography role.
// scroll: Keep the full caption at the largest height-admitted scale.
// Exception: Returns invalid argument for invalid input, or invalid size for an unsupported extent.
inline esp_err_t prepare_styled_title(CODY2D::TextSpan text, CODY2D::Rect bounds, StyledTitle &title,
    uint8_t max_scale = 3, bool scroll = true)
{
    title = {};
    if (text.characters == nullptr || text.length == 0 || bounds.width == 0 || bounds.height == 0 ||
        static_cast<uint32_t>(bounds.x) + bounds.width > 466U ||
        static_cast<uint32_t>(bounds.y) + bounds.height > 466U || max_scale == 0 || max_scale > 3)
        return ESP_ERR_INVALID_ARG;
    for (uint16_t index = 0; index < text.length; ++index) {
        if (TitleStyleDetail::glyph(text.characters[index]) == nullptr) return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t scale = max_scale; scale > 0; --scale) {
        if (7U * scale > bounds.height) continue;
        const uint32_t full_width = TitleStyleDetail::width(text.length, scale);
        if (scroll && full_width > UINT16_MAX) return ESP_ERR_INVALID_SIZE;
        if (scroll || full_width <= bounds.width) {
            title = {text, bounds, text.length, scale, false};
            return ESP_OK;
        }
        if (scale == 1) {
            const uint16_t capacity = static_cast<uint16_t>((bounds.width + scale) / (6U * scale));
            if (capacity < 4U) return ESP_ERR_INVALID_SIZE;
            title = {text, bounds, static_cast<uint16_t>(capacity - 3U), scale, true};
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_SIZE;
}

// width: Full glyph extent in logical pixels.
// viewport_width: Width of the admitted title slot.
// elapsed_us: Active time since the current heading became readable.
inline uint16_t styled_title_scroll_offset(uint32_t width, uint16_t viewport_width, uint64_t elapsed_us)
{
    if (width <= viewport_width) return 0;
    constexpr uint64_t start_hold_us = 1200000;
    constexpr uint64_t end_hold_us = 1800000;
    constexpr uint64_t pixel_us = 25000;
    const uint64_t overflow = width - viewport_width;
    const uint64_t travel_us = overflow * pixel_us;
    const uint64_t phase = elapsed_us % (start_hold_us + travel_us + end_hold_us + travel_us);
    if (phase < start_hold_us) return 0;
    const uint64_t motion = phase - start_hold_us;
    if (motion < travel_us) return static_cast<uint16_t>(motion / pixel_us);
    if (motion < travel_us + end_hold_us) return static_cast<uint16_t>(overflow);
    return static_cast<uint16_t>(overflow - (motion - travel_us - end_hold_us) / pixel_us);
}

// title: Prepared heading whose borrowed text remains valid for the complete draw.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical y origin of this clipped tile.
// first_column: Logical x origin of this clipped tile.
// visible: Original text prefix admitted by the scene's existing typewriter clock.
// subtitle: Draw the secondary role in its dedicated constant color.
// horizontal_offset: Number of clipped leading pixels on an overflowing heading.
// Exception: Returns the original surface error or invalid argument for an unprepared heading or out-of-panel tile.
inline esp_err_t draw_styled_title_rows(const StyledTitle &title, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t visible, bool subtitle = false,
    uint16_t horizontal_offset = 0)
{
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    if (title.scale == 0 || title.text.characters == nullptr || title.shown == 0 ||
        title.shown > title.text.length || visible > title.text.length || first_row + strip.height > 466U ||
        first_column + strip.width > 466U) return ESP_ERR_INVALID_ARG;
    constexpr uint16_t colors[]{Theme::text_heading_gradient_0, Theme::text_heading_gradient_1,
        Theme::text_heading_gradient_2, Theme::text_heading_gradient_3, Theme::text_heading_gradient_4,
        Theme::text_heading_gradient_5, Theme::text_heading_gradient_6};
    const uint16_t count = static_cast<uint16_t>(title.shown + (title.truncated ? 3U : 0U));
    const uint16_t visible_count = visible <= title.shown ? visible :
        static_cast<uint16_t>(title.shown + (title.truncated ? std::min<uint16_t>(3U, visible - title.shown) : 0U));
    const int width = static_cast<int>(TitleStyleDetail::width(count, title.scale));
    const uint16_t height = static_cast<uint16_t>(7U * title.scale);
    const int spare_width = static_cast<int>(title.bounds.width) - width;
    if (height > title.bounds.height || horizontal_offset > std::max(0, -spare_width))
        return ESP_ERR_INVALID_SIZE;
    const int left = title.bounds.x + std::max(0, spare_width) / 2 - horizontal_offset;
    const int top = title.bounds.y + (title.bounds.height - height) / 2U;
    const int clip_left = std::max<int>(title.bounds.x, first_column);
    const int clip_right = std::min<int>(title.bounds.x + title.bounds.width, first_column + strip.width);
    if (clip_left >= clip_right) return ESP_OK;
    const int pitch = 6 * title.scale;
    const int first_glyph = std::max(0, clip_left - left) / pitch;
    const int end_glyph = std::min<int>(visible_count, (clip_right - left + pitch - 1) / pitch);
    const int y0 = std::max<int>(top, first_row);
    const int y1 = std::min<int>(top + height, first_row + strip.height);
    for (int y = y0; y < y1; ++y) {
        const uint8_t glyph_row = static_cast<uint8_t>((y - top) / title.scale);
        const auto color = CODY2D::rgb565_native_to_stored(
            subtitle ? Theme::text_subtitle : colors[glyph_row], strip.byte_order);
        auto *pixels = strip.pixels + static_cast<size_t>(y - first_row) * strip.stride;
        for (int index = first_glyph; index < end_glyph; ++index) {
            const char character = index < title.shown ? title.text.characters[index] : '.';
            const auto *glyph = TitleStyleDetail::glyph(character);
            if (glyph == nullptr) return ESP_ERR_INVALID_ARG;
            for (uint8_t column = 0; column < 5; ++column) {
                if ((glyph->columns[column] & (1U << glyph_row)) == 0) continue;
                const int x = left + index * pitch + column * title.scale;
                const int x0 = std::max(x, clip_left);
                const int x1 = std::min(x + title.scale, clip_right);
                if (x0 < x1) std::fill_n(pixels + x0 - first_column, x1 - x0, color);
            }
        }
    }
    return ESP_OK;
}

// line: Existing complete supporting caption and its admitted text bounds.
// strip: Borrowed RGB565 tile with its original stride and byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// visible: Number of source characters admitted by the original reveal clock.
// Exception: Returns the original layout, text or tile error.
inline esp_err_t draw_styled_subtitle_line(const CODY2D::TextLine &line, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, uint16_t visible)
{
    if (!line.valid) return ESP_ERR_INVALID_ARG;
    StyledTitle title{};
    const auto error = prepare_styled_title(line.text, line.bounds, title, 2, false);
    return error == ESP_OK ? draw_styled_title_rows(title, strip, first_row, first_column, visible, true) : error;
}

} // namespace CODY
