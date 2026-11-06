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

#include "CODY2D_text.hpp"

#include <algorithm>
#include "esp_log.h"

namespace CODY2D {
namespace {
constexpr char kTag[] = "CODY2D_text";
constexpr uint16_t kMaximumTextLength = 64;

// source: Native RGB565 foreground.
// destination: Native RGB565 background beneath the glyph.
// level: Valid font coverage, strictly between transparent and opaque.
// maximum: Opaque coverage level for the font's explicit two- or four-bit format.
uint16_t blend_coverage(uint16_t source, uint16_t destination, uint8_t level, uint8_t maximum)
{
    const uint32_t inverse = maximum - level;
    const uint32_t rounding = maximum / 2U;
    const uint32_t red = (((source >> 11U) & 31U) * level +
                          ((destination >> 11U) & 31U) * inverse + rounding) / maximum;
    const uint32_t green = (((source >> 5U) & 63U) * level +
                            ((destination >> 5U) & 63U) * inverse + rounding) / maximum;
    const uint32_t blue = ((source & 31U) * level + (destination & 31U) * inverse + rounding) / maximum;
    return static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
}

// font: Previously validated immutable font storage and metric-table extent.
// character: Original ASCII byte, never a substituted character.
// metrics: Receives the source crop and logical spacing for this glyph.
// Exception: Rejects unsupported characters or malformed proportional metrics.
esp_err_t glyph_metrics(const MonoFont &font, uint8_t character, GlyphMetrics &metrics)
{
    if (character < font.first_character || character >= font.first_character + font.character_count) {
        ESP_LOGE(kTag, "Text character rejected: byte=%u (%s)", character, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    metrics = font.glyph_metrics == nullptr ? GlyphMetrics{0, font.width, font.advance, 0} :
        font.glyph_metrics[character - font.first_character];
    if (metrics.advance == 0 || metrics.advance > 32 || metrics.source_left + metrics.width > font.width ||
        (font.glyph_metrics != nullptr && metrics.left_padding + metrics.width > metrics.advance)) {
        ESP_LOGE(kTag, "Glyph metrics rejected: byte=%u left=%u width=%u advance=%u padding=%u (%s)",
            character, metrics.source_left, metrics.width, metrics.advance, metrics.left_padding,
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
} // namespace

// font: Immutable packed coverage and exact readable storage extent.
// Exception: Returns INVALID_ARG for unsupported formats or invalid storage bounds.
esp_err_t validate_mono_font(const MonoFont &font)
{
    const uint32_t required_bytes = static_cast<uint32_t>(font.character_count) *
        font.height * font.row_bytes;
    if (font.coverage == nullptr || font.width == 0 || font.width > 32 ||
        font.height == 0 || font.height > 64 || font.advance == 0 || font.advance > 32 ||
        (font.coverage_bits != 2 && font.coverage_bits != 4) ||
        font.row_bytes != (font.width * font.coverage_bits + 7U) / 8U || font.character_count == 0 ||
        font.first_character < 32 || font.first_character + font.character_count > 127 ||
        required_bytes != font.coverage_bytes ||
        (font.glyph_metrics == nullptr ? font.glyph_metrics_count != 0 :
            font.glyph_metrics_count != font.character_count)) {
        ESP_LOGE(kTag, "Font rejected: glyph=%ux%u advance=%u coverage_bits=%u bytes=%lu required=%lu (%s)",
            font.width, font.height, font.advance, font.coverage_bits, static_cast<unsigned long>(font.coverage_bytes),
            static_cast<unsigned long>(required_bytes), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// font: Immutable coverage and optional per-glyph spacing.
// text: Counted readable ASCII bytes; an empty span measures zero.
// width: Receives complete layout width only after every glyph is accepted.
// Exception: Preserves font errors and rejects unsupported or malformed glyph metrics.
esp_err_t measure_text_width(const MonoFont &font, TextSpan text, uint32_t &width)
{
    const esp_err_t font_error = validate_mono_font(font);
    if (font_error != ESP_OK) { return font_error; }
    if (text.length != 0 && text.characters == nullptr) {
        ESP_LOGE(kTag, "Text measurement rejected: null source (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t cursor = 0;
    uint32_t measured = 0;
    for (uint16_t index = 0; index < text.length; ++index) {
        GlyphMetrics metrics{};
        const esp_err_t error = glyph_metrics(font, static_cast<uint8_t>(text.characters[index]), metrics);
        if (error != ESP_OK) { return error; }
        const uint32_t extent = font.glyph_metrics == nullptr ? font.width : metrics.advance;
        measured = std::max(measured, cursor + extent);
        cursor += metrics.advance;
    }
    width = measured;
    return ESP_OK;
}

// font: Immutable coverage and optional per-glyph spacing shared with rasterization.
// text: Complete counted ASCII text retained by the caller.
// pixel_offset: Nonnegative horizontal scroll offset in the complete measured line.
// viewport_width: Logical width of the clipped presentation window.
// window: Receives the bounded borrowed glyph span intersecting the viewport.
// origin_offset: Receives the first glyph's signed offset from the viewport's left edge.
// Exception: Preserves source errors and rejects invalid or oversized windows.
esp_err_t select_text_window(const MonoFont &font, TextSpan text, uint16_t pixel_offset,
    uint16_t viewport_width, TextSpan &window, int16_t &origin_offset)
{
    uint32_t measured = 0;
    const esp_err_t measurement_error = measure_text_width(font, text, measured);
    if (measurement_error != ESP_OK) { return measurement_error; }
    if (viewport_width == 0 || viewport_width > kMaxDimension || pixel_offset >= measured) {
        ESP_LOGE(kTag, "Text window rejected: offset=%u width=%u measured=%lu (%s)",
            pixel_offset, viewport_width, static_cast<unsigned long>(measured), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t cursor = 0;
    uint16_t first = text.length;
    uint16_t count = 0;
    int16_t selected_origin = 0;
    const uint32_t right = static_cast<uint32_t>(pixel_offset) + viewport_width;
    for (uint16_t index = 0; index < text.length && cursor < right; ++index) {
        GlyphMetrics metrics{};
        const esp_err_t error = glyph_metrics(font, static_cast<uint8_t>(text.characters[index]), metrics);
        if (error != ESP_OK) { return error; }
        const uint32_t extent = font.glyph_metrics == nullptr ? font.width : metrics.advance;
        if (cursor + extent > pixel_offset) {
            if (first == text.length) {
                first = index;
                selected_origin = static_cast<int16_t>(static_cast<int32_t>(cursor) - pixel_offset);
            }
            ++count;
        }
        cursor += metrics.advance;
    }
    if (count == 0 || count > kMaximumTextLength) {
        ESP_LOGE(kTag, "Text window rejected: characters=%u (%s)", count, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    window = {text.characters + first, count};
    origin_offset = selected_origin;
    return ESP_OK;
}

// font: Flash-owned glyphs retained for the lifetime of the prepared line.
// text: Counted readable ASCII bytes; unsupported characters are errors, not substitutions.
// origin: Nonnegative full-canvas top-left corner of the line.
// color: Native RGB565 foreground color.
// line: Receives the prepared layout only after every input is accepted.
// Exception: Returns the original font error or INVALID_ARG/INVALID_SIZE for rejected text geometry.
esp_err_t prepare_text_line(const MonoFont &font, TextSpan text, Point origin,
                            uint16_t color, TextLine &line)
{
    const esp_err_t font_error = validate_mono_font(font);
    if (font_error != ESP_OK) {
        return font_error;
    }
    if (text.characters == nullptr || text.length == 0 || text.length > kMaximumTextLength ||
        origin.x < 0 || origin.y < 0) {
        ESP_LOGE(kTag, "Text layout rejected: length=%u origin=%d,%d (%s)", text.length,
            origin.x, origin.y, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t width = 0;
    const esp_err_t measurement_error = measure_text_width(font, text, width);
    if (measurement_error != ESP_OK) { return measurement_error; }
    if (static_cast<uint32_t>(origin.x) + width > kMaxDimension ||
        static_cast<uint32_t>(origin.y) + font.height > kMaxDimension) {
        ESP_LOGE(kTag, "Text extent rejected: width=%lu (%s)",
            static_cast<unsigned long>(width), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    line = {&font, text, origin, color,
        {static_cast<uint16_t>(origin.x), static_cast<uint16_t>(origin.y),
         static_cast<uint16_t>(width), font.height}, true};
    return ESP_OK;
}

// line: Successfully prepared immutable layout.
// visible_characters: Prefix length; it cannot exceed the prepared text length.
// strip: Caller-owned RGB565 tile, including its explicit native/panel byte order.
// first_row: Logical canvas y origin of this tile.
// first_column: Logical canvas x origin of this tile.
// Exception: Returns the original surface/font error or a specific invalid text/tile error.
esp_err_t draw_text_rows(const TextLine &line, uint16_t visible_characters,
                         Surface strip, uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (!line.valid || line.font == nullptr || line.text.characters == nullptr ||
        line.text.length == 0 || line.text.length > kMaximumTextLength ||
        line.origin.x < 0 || line.origin.y < 0 || visible_characters > line.text.length ||
        static_cast<uint32_t>(first_row) + strip.height > kMaxDimension ||
        static_cast<uint32_t>(first_column) + strip.width > kMaxDimension) {
        ESP_LOGE(kTag, "Text draw rejected: valid=%u visible=%u length=%u (%s)",
            line.valid, visible_characters, line.text.length, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const MonoFont &font = *line.font;
    const esp_err_t font_error = validate_mono_font(font);
    if (font_error != ESP_OK) {
        return font_error;
    }
    const int top = std::max<int>(first_row, line.origin.y);
    const int bottom = std::min<int>(first_row + strip.height, line.origin.y + font.height);
    const int right = first_column + strip.width;
    if (top >= bottom || visible_characters == 0 || right <= line.origin.x) {
        return ESP_OK;
    }
    // Fixed-cell fonts retain their direct skip. Proportional fonts walk at most
    // the prepared 64-glyph window; no glyph coverage scan or temporary bitmap.
    const uint16_t first_glyph = font.glyph_metrics != nullptr ? 0 : static_cast<uint16_t>(
        std::max<int>(0, static_cast<int>(first_column) - line.origin.x - (font.width - 1)) / font.advance
    );
    uint32_t cursor = static_cast<uint32_t>(first_glyph) * font.advance;
    const uint16_t stored_color = rgb565_native_to_stored(line.color, strip.byte_order);
    const uint8_t maximum_coverage = static_cast<uint8_t>((1U << font.coverage_bits) - 1U);
    for (uint16_t index = first_glyph; index < visible_characters; ++index) {
        const uint8_t character = static_cast<uint8_t>(line.text.characters[index]);
        GlyphMetrics metrics{};
        const esp_err_t metrics_error = glyph_metrics(font, character, metrics);
        if (metrics_error != ESP_OK) { return metrics_error; }
        const int glyph_left = line.origin.x + static_cast<int>(cursor) + metrics.left_padding;
        cursor += metrics.advance;
        if (glyph_left >= right) {
            break;
        }
        const int left = std::max<int>(first_column, glyph_left);
        const int last = std::min<int>(right, glyph_left + metrics.width);
        if (left >= last) { continue; }
        const uint32_t glyph_offset = static_cast<uint32_t>(character - font.first_character) *
            font.height * font.row_bytes;
        for (int row = top; row < bottom; ++row) {
            const uint8_t *coverage = font.coverage + glyph_offset +
                (row - line.origin.y) * font.row_bytes;
            uint16_t *destination = strip.pixels + (row - first_row) * strip.stride + left - first_column;
            for (int column = left; column < last; ++column, ++destination) {
                const uint16_t glyph_column = static_cast<uint16_t>(column - glyph_left + metrics.source_left);
                // Both supported formats divide a byte exactly; bit addressing
                // avoids a variable division in the per-pixel glyph loop.
                const uint16_t bit_offset = glyph_column * font.coverage_bits;
                const uint8_t shift = static_cast<uint8_t>(8U - font.coverage_bits - (bit_offset & 7U));
                const uint8_t level = (coverage[bit_offset >> 3U] >> shift) & maximum_coverage;
                if (level == maximum_coverage) {
                    *destination = stored_color;
                } else if (level != 0) {
                    const uint16_t background = rgb565_stored_to_native(*destination, strip.byte_order);
                    *destination = rgb565_native_to_stored(
                        blend_coverage(line.color, background, level, maximum_coverage), strip.byte_order
                    );
                }
            }
        }
    }
    return ESP_OK;
}
} // namespace CODY2D
