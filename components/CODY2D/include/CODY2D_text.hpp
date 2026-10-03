#pragma once

#include <cstdint>
#include "CODY2D.hpp"

namespace CODY2D {

// Immutable, row-padded two-bit glyph coverage. All bytes remain in flash.
struct MonoFont {
    const uint8_t *coverage = nullptr;
    uint32_t coverage_bytes = 0;
    uint8_t first_character = 32;
    uint8_t character_count = 95;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t advance = 0;
    uint8_t row_bytes = 0;
};

struct TextSpan {
    const char *characters = nullptr;
    uint16_t length = 0;
};

// Prepared once when a scene changes, never by walking the string per frame.
struct TextLine {
    const MonoFont *font = nullptr;
    TextSpan text{};
    Point origin{};
    uint16_t color = 0;
    Rect bounds{};
    bool valid = false;
};

// font: Immutable packed coverage and exact readable storage extent.
esp_err_t validate_mono_font(const MonoFont &font);

// font: Flash-owned glyphs retained for the lifetime of the prepared line.
// text: Counted readable ASCII bytes; unsupported characters are errors, not substitutions.
// origin: Nonnegative full-canvas top-left corner of the line.
// color: Native RGB565 foreground color.
// line: Receives the prepared layout only after every input is accepted.
esp_err_t prepare_text_line(const MonoFont &font, TextSpan text, Point origin,
                            uint16_t color, TextLine &line);

// line: Successfully prepared immutable layout.
// visible_characters: Prefix length; it cannot exceed the prepared text length.
// strip: Caller-owned RGB565 tile, including its explicit native/panel byte order.
// first_row: Logical canvas y origin of this tile.
// first_column: Logical canvas x origin of this tile.
esp_err_t draw_text_rows(const TextLine &line, uint16_t visible_characters,
                         Surface strip, uint16_t first_row, uint16_t first_column);

} // namespace CODY2D
