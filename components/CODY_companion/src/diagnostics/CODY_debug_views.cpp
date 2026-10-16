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

#include "CODY_debug_views.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_debug_views";
constexpr uint16_t kTextColor = 0xF7BE;
constexpr uint16_t kUsedColor = 0x347F;
constexpr uint16_t kLargestColor = 0x4F39;
constexpr uint16_t kOtherFreeColor = 0xFDA5;
constexpr CODY2D::Rect kMemoryButtonBounds{133, 336, 200, 44};

// text: Bounded printable ASCII line.
// top: Logical top of this centered line.
// font: Existing immutable native font.
// strip: Validated borrowed tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// color: Native RGB565 foreground.
// Exception: Logs and returns the original font/raster error or a circular layout rejection.
esp_err_t draw_centered_line(const char *text, uint16_t top, const CODY2D::MonoFont &font,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column, uint16_t color)
{
    if (first_row >= top + font.height || first_row + strip.height <= top) {
        return ESP_OK;
    }
    const size_t length = std::strlen(text);
    const unsigned width = length == 0 ? 0 : (length - 1) * font.advance + font.width;
    const int left = (466 - static_cast<int>(width)) / 2;
    const int half_width = 233 - left;
    const int upper = static_cast<int>(top) - 233;
    const int lower = static_cast<int>(top + font.height - 1) - 233;
    if (length == 0 || width > 320 || top + font.height > 466 ||
        half_width * half_width + std::max(upper * upper, lower * lower) >= 233 * 233) {
        ESP_LOGE(kTag, "Memory text circular bounds rejected: width=%u top=%u", width, top);
        return ESP_ERR_INVALID_SIZE;
    }
    CODY2D::TextLine line;
    esp_err_t error = CODY2D::prepare_text_line(font, {text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, color, line);
    if (error == ESP_OK) {
        error = CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Memory text raster failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// heap: Validated capacity and largest-free measurements, or explicit unavailable state.
// top: Logical bar y origin, wholly within the central safe square.
// strip: Validated borrowed tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
void draw_heap_bar(const HeapDiagnosticReadout &heap, uint16_t top, CODY2D::Surface strip,
                   uint16_t first_row, uint16_t first_column)
{
    constexpr int left = 96;
    constexpr int width = 274;
    constexpr int height = 10;
    const unsigned used_width = heap.available
        ? static_cast<unsigned>((uint64_t(heap.total_bytes - heap.free_bytes) * width) / heap.total_bytes) : 0;
    const unsigned largest_width = heap.available
        ? static_cast<unsigned>((uint64_t(heap.largest_block_bytes) * width) / heap.total_bytes) : 0;
    // Categories represent capacity, never block addresses or an invented allocation order.
    for (int y = std::max<int>(top, first_row); y < std::min<int>(top + height, first_row + strip.height); ++y) {
        for (int x = std::max<int>(left, first_column); x < std::min<int>(left + width, first_column + strip.width); ++x) {
            const unsigned position = static_cast<unsigned>(x - left);
            uint16_t color = 0x3186;
            if (heap.available) {
                if (position < used_width) {
                    color = kUsedColor;
                } else if (position < used_width + largest_width) {
                    color = kLargestColor;
                } else {
                    color = kOtherFreeColor;
                }
            }
            strip.pixels[(y - first_row) * strip.stride + x - first_column] =
                CODY2D::rgb565_native_to_stored(color, strip.byte_order);
        }
    }
}

// heap: One coherent heap capacity projection; zero free bytes leaves its fragmentation index undefined.
// title: Constant ASCII heap identity.
// top: Logical section y origin.
// font: Shared native body font.
// strip: Validated borrowed tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Rejects incoherent values and returns original text/raster failures.
esp_err_t draw_heap_section(const HeapDiagnosticReadout &heap, const char *title, uint16_t top,
    const CODY2D::MonoFont &font, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (heap.available && (heap.stale || heap.total_bytes == 0 || heap.free_bytes > heap.total_bytes ||
        heap.largest_block_bytes > heap.free_bytes || heap.minimum_free_bytes > heap.free_bytes)) {
        ESP_LOGE(kTag, "Memory heap values rejected: heap=%s (%s)", title, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = draw_centered_line(title, top, font, strip, first_row, first_column, kTextColor);
    if (error != ESP_OK) {
        return error;
    }
    draw_heap_bar(heap, top + 32, strip, first_row, first_column);
    if (!heap.available) {
        return draw_centered_line(heap.stale ? "STALE" : "UNAVAILABLE", top + 52,
            font, strip, first_row, first_column, kTextColor);
    }
    char text[3][40]{};
    int lengths[3];
    lengths[0] = std::snprintf(text[0], sizeof(text[0]), "USED %luK FREE %luK",
        static_cast<unsigned long>((heap.total_bytes - heap.free_bytes) / 1024),
        static_cast<unsigned long>(heap.free_bytes / 1024));
    lengths[1] = std::snprintf(text[1], sizeof(text[1]), "BIGGEST %luK MIN %luK",
        static_cast<unsigned long>(heap.largest_block_bytes / 1024),
        static_cast<unsigned long>(heap.minimum_free_bytes / 1024));
    if (heap.free_bytes == 0) {
        lengths[2] = std::snprintf(text[2], sizeof(text[2]), "FRAG N/A FREE BLK %lu",
            static_cast<unsigned long>(heap.free_blocks));
    } else {
        const unsigned fragmentation = static_cast<unsigned>(
            (uint64_t(heap.free_bytes - heap.largest_block_bytes) * 100 + heap.free_bytes / 2) / heap.free_bytes);
        lengths[2] = std::snprintf(text[2], sizeof(text[2]), "FRAG %u%% FREE BLK %lu", fragmentation,
            static_cast<unsigned long>(heap.free_blocks));
    }
    for (unsigned index = 0; index < 3; ++index) {
        if (lengths[index] <= 0 || static_cast<size_t>(lengths[index]) >= sizeof(text[index])) {
            ESP_LOGE(kTag, "Memory heap text rejected: heap=%s row=%u (%s)", title, index,
                esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        error = draw_centered_line(text[index], static_cast<uint16_t>(top + 52 + index * 28),
            font, strip, first_row, first_column, kTextColor);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}
}  // namespace

// font: Existing immutable native body font.
// strip: Borrowed RGB565 tile.
// first_row: Logical y origin.
// first_column: Logical x origin.
// Exception: Returns original caption preparation or capsule raster errors.
esp_err_t draw_debug_memory_button_rows(const CODY2D::MonoFont &font, CODY2D::Surface strip,
                                       uint16_t first_row, uint16_t first_column)
{
    CODY2D::CapsuleControl button;
    button.bounds = kMemoryButtonBounds;
    button.top_color = 0x22CA;
    button.bottom_color = 0x11C6;
    button.corner_radius = 22;
    constexpr char caption[] = "Memory";
    const int width = (sizeof(caption) - 2) * font.advance + font.width;
    const CODY2D::Point origin{static_cast<int16_t>(233 - width / 2),
        static_cast<int16_t>(kMemoryButtonBounds.y + (kMemoryButtonBounds.height - font.height) / 2)};
    esp_err_t error = CODY2D::prepare_text_line(font, {caption, sizeof(caption) - 1}, origin, kTextColor, button.caption);
    if (error == ESP_OK) {
        error = CODY2D::draw_capsule_rows(button, strip, first_row, first_column);
    }
    return error;
}

// point: Logical acquired contact in the shared rendered Memory-button rectangle.
bool debug_memory_button_contains(CODY2D::Point point)
{
    CODY2D::CapsuleControl button;
    button.bounds = kMemoryButtonBounds;
    button.corner_radius = 22;
    return CODY2D::capsule_contains(button, point);
}

// readout: Compact current canonical projection, retained unchanged throughout this frame.
// font: Existing body font, shared with the host renderer.
// strip: Borrowed RGB565 tile.
// first_row: Logical y origin.
// first_column: Logical x origin.
// Exception: Returns actual surface, circular layout, heap-value or raster errors.
esp_err_t draw_memory_diagnostics_rows(const MemoryDiagnosticReadout &readout,
    const CODY2D::MonoFont &font, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) {
        return error;
    }
    if (first_row + strip.height > 466 || first_column + strip.width > 466) {
        ESP_LOGE(kTag, "Memory diagnostic tile exceeds canvas (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    error = draw_centered_line("HEAP MEMORY", 48, font, strip, first_row, first_column, kLargestColor);
    if (error == ESP_OK) {
        error = draw_heap_section(readout.internal, "INTERNAL RAM", 88, font, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = draw_heap_section(readout.psram, "PSRAM", 240, font, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = draw_centered_line("BLUE USED GREEN BIGGEST", 383, font, strip, first_row, first_column, kTextColor);
    }
    if (error == ESP_OK) {
        error = draw_centered_line("AMBER OTHER FREE", 412, font, strip, first_row, first_column, kOtherFreeColor);
    }
    return error;
}

// strip: Borrowed native tile; all geometry uses the same logical circle as touch.
// first_row: Logical y origin.
// first_column: Logical x origin.
// Exception: Returns original capsule or round-cap stroke errors.
esp_err_t draw_debug_view_back_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    CODY2D::CapsuleControl back;
    back.bounds = {10, 203, 60, 60};
    back.top_color = 0x2104;
    back.bottom_color = 0x1082;
    back.corner_radius = 30;
    esp_err_t error = CODY2D::draw_capsule_rows(back, strip, first_row, first_column);
    CODY2D::PreparedStroke strokes[2];
    if (error == ESP_OK) {
        error = CODY2D::prepare_stroke({45, 223}, {35, 233}, 3, kTextColor, strokes[0]);
    }
    if (error == ESP_OK) {
        error = CODY2D::prepare_stroke({35, 233}, {45, 243}, 3, kTextColor, strokes[1]);
    }
    if (error == ESP_OK) {
        error = CODY2D::stroke_prepared_batch_rows(strip, first_row, strokes, 2, first_column);
    }
    return error;
}

// point: Same logical contact coordinate used by native rendering after rotation mapping.
bool debug_view_back_contains(CODY2D::Point point)
{
    const int x = point.x - 40;
    const int y = point.y - 233;
    return x * x + y * y <= 30 * 30;
}
}  // namespace CODY
