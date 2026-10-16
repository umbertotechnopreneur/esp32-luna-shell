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

#include "CODY2D_text.hpp"

namespace CODY {

enum class DiagnosticPage : uint8_t { Overview = 0, Memory = 1 };

// A bounded projection of the existing canonical heap measurements, with no addresses.
struct HeapDiagnosticReadout {
    uint32_t total_bytes = 0;
    uint32_t free_bytes = 0;
    uint32_t largest_block_bytes = 0;
    uint32_t minimum_free_bytes = 0;
    uint32_t free_blocks = 0;
    bool available = false;
    bool stale = false;
};

struct MemoryDiagnosticReadout {
    HeapDiagnosticReadout internal;
    HeapDiagnosticReadout psram;
};

// font: Existing immutable ASCII body font.
// strip: Borrowed native RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns the original text/control error; the touch target shares these bounds.
esp_err_t draw_debug_memory_button_rows(const CODY2D::MonoFont &font, CODY2D::Surface strip,
                                       uint16_t first_row, uint16_t first_column);

// point: Logical acquired contact; only the visible Memory button admits navigation.
bool debug_memory_button_contains(CODY2D::Point point);

// readout: Canonical heap capacity projection; unavailable heaps never show numeric values.
// font: Immutable native ASCII body font.
// strip: Borrowed native RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns original layout/raster errors or rejects inconsistent heap values.
esp_err_t draw_memory_diagnostics_rows(const MemoryDiagnosticReadout &readout,
    const CODY2D::MonoFont &font, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

// strip: Borrowed native RGB565 tile for the reversible debug view.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns real raster errors; full Back hit target is retained separately.
esp_err_t draw_debug_view_back_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

// point: Logical acquired touch coordinate; the full 60x60 lateral circle is active.
bool debug_view_back_contains(CODY2D::Point point);

}  // namespace CODY
