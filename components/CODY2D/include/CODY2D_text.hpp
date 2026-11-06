/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "CODY2D.hpp"

namespace CODY2D {

// Source coverage remains fixed-cell; optional metrics admit proportionally spaced ink.
struct GlyphMetrics {
    uint8_t source_left = 0;
    uint8_t width = 0;
    uint8_t advance = 0;
    uint8_t left_padding = 0;
};

// Immutable, row-padded glyph coverage, packed most-significant sample first.
// Existing eight-field initializers retain their original two-bit asset format.
struct MonoFont {
    const uint8_t *coverage = nullptr;
    uint32_t coverage_bytes = 0;
    uint8_t first_character = 32;
    uint8_t character_count = 95;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t advance = 0;
    uint8_t row_bytes = 0;
    uint8_t coverage_bits = 2;
    const GlyphMetrics *glyph_metrics = nullptr;
    uint8_t glyph_metrics_count = 0;
};

// coverage: Existing immutable row-padded glyph bytes; no bitmap regeneration is needed.
// first_character: ASCII identity of the first source cell.
// width: Width of each unchanged source cell.
// height: Height of each unchanged source cell.
// row_bytes: Packed bytes per source row.
// coverage_bits: Two or four bits per source sample.
// space_advance: Explicit width for the space glyph, independent of the widest letter.
// Invariant: Callers use a constexpr result with the exact source coverage dimensions.
template <size_t GlyphCount, size_t CoverageBytes>
constexpr std::array<GlyphMetrics, GlyphCount> make_proportional_glyph_metrics(
    const uint8_t (&coverage)[CoverageBytes], uint8_t first_character, uint8_t width,
    uint8_t height, uint8_t row_bytes, uint8_t coverage_bits, uint8_t space_advance)
{
    std::array<GlyphMetrics, GlyphCount> result{};
    if (GlyphCount == 0 || GlyphCount > 95 || width == 0 || width > 30 || height == 0 || height > 64 ||
        (coverage_bits != 2 && coverage_bits != 4) || row_bytes != (width * coverage_bits + 7U) / 8U ||
        CoverageBytes != GlyphCount * height * row_bytes || space_advance < 6 || space_advance > 32 ||
        first_character < 32 || first_character + GlyphCount > 127) {
        // An inconsistent constexpr source yields invalid metrics, rejected before presentation.
        return result;
    }
    uint8_t digit_advance = 6;
    for (size_t glyph = 0; glyph < GlyphCount; ++glyph) {
        uint8_t left = width;
        uint8_t right = 0;
        for (uint8_t row = 0; row < height; ++row) {
            for (uint8_t column = 0; column < width; ++column) {
                const size_t bit = static_cast<size_t>(column) * coverage_bits;
                const size_t index = glyph * height * row_bytes + row * row_bytes + (bit >> 3U);
                const uint8_t shift = static_cast<uint8_t>(8U - coverage_bits - (bit & 7U));
                const uint8_t level = static_cast<uint8_t>((coverage[index] >> shift) & ((1U << coverage_bits) - 1U));
                if (level != 0) {
                    if (column < left) { left = column; }
                    if (column > right) { right = column; }
                }
            }
        }
        const uint8_t ink_width = left == width ? 0 : static_cast<uint8_t>(right - left + 1U);
        const uint8_t advance = ink_width == 0 ? space_advance :
            static_cast<uint8_t>(ink_width + 2U < 6U ? 6U : ink_width + 2U);
        result[glyph] = {ink_width == 0 ? uint8_t{0} : left, ink_width, advance,
            static_cast<uint8_t>((advance - ink_width) / 2U)};
        const size_t character = first_character + glyph;
        if (character >= '0' && character <= '9' && advance > digit_advance) {
            digit_advance = advance;
        }
    }
    // Equal digit advances preserve readable counters while labels use natural ink widths.
    for (size_t glyph = 0; glyph < GlyphCount; ++glyph) {
        const size_t character = first_character + glyph;
        if (character >= '0' && character <= '9') {
            result[glyph].advance = digit_advance;
            result[glyph].left_padding = static_cast<uint8_t>((digit_advance - result[glyph].width) / 2U);
        }
    }
    return result;
}

struct TextSpan {
    const char *characters = nullptr;
    uint16_t length = 0;
};

// Prepared once when a scene changes; glyph bytes and metrics remain immutable.
struct TextLine {
    const MonoFont *font = nullptr;
    TextSpan text{};
    Point origin{};
    uint16_t color = 0;
    Rect bounds{};
    bool valid = false;
};

// font: Immutable packed coverage and exact readable storage extent.
// Exception: Returns INVALID_ARG for unsupported formats or invalid storage bounds.
esp_err_t validate_mono_font(const MonoFont &font);

// font: Immutable coverage and optional per-glyph spacing.
// text: Counted readable ASCII bytes; an empty span measures zero.
// width: Receives complete layout width only after every glyph is accepted.
// Exception: Preserves font errors and rejects unsupported or malformed glyph metrics.
esp_err_t measure_text_width(const MonoFont &font, TextSpan text, uint32_t &width);

// font: Immutable coverage and optional per-glyph spacing shared with rasterization.
// text: Complete counted ASCII text retained by the caller.
// pixel_offset: Nonnegative horizontal scroll offset in the complete measured line.
// viewport_width: Logical width of the clipped presentation window.
// window: Receives the bounded borrowed glyph span intersecting the viewport.
// origin_offset: Receives the first glyph's signed offset from the viewport's left edge.
// Exception: Preserves source errors and rejects invalid or oversized windows.
esp_err_t select_text_window(const MonoFont &font, TextSpan text, uint16_t pixel_offset,
    uint16_t viewport_width, TextSpan &window, int16_t &origin_offset);

// font: Flash-owned glyphs retained for the lifetime of the prepared line.
// text: Counted readable ASCII bytes; unsupported characters are errors, not substitutions.
// origin: Nonnegative full-canvas top-left corner of the line.
// color: Native RGB565 foreground color.
// line: Receives the prepared layout only after every input is accepted.
// Exception: Returns the original font error or INVALID_ARG/INVALID_SIZE for rejected text geometry.
esp_err_t prepare_text_line(const MonoFont &font, TextSpan text, Point origin,
                            uint16_t color, TextLine &line);

// line: Successfully prepared immutable layout.
// visible_characters: Prefix length; it cannot exceed the prepared text length.
// strip: Caller-owned RGB565 tile, including its explicit native/panel byte order.
// first_row: Logical canvas y origin of this tile.
// first_column: Logical canvas x origin of this tile.
// Exception: Returns the original surface/font error or a specific invalid text/tile error.
esp_err_t draw_text_rows(const TextLine &line, uint16_t visible_characters,
                         Surface strip, uint16_t first_row, uint16_t first_column);

} // namespace CODY2D
