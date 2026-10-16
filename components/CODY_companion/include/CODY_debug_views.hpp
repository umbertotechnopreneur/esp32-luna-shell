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

#include "CODY2D.hpp"

namespace CODY {

// strip: Borrowed native RGB565 tile for the reversible debug view.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns real raster errors; full Back hit target is retained separately.
esp_err_t draw_debug_view_back_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

// point: Logical acquired touch coordinate; the full 60x60 lateral circle is active.
bool debug_view_back_contains(CODY2D::Point point);

}  // namespace CODY
