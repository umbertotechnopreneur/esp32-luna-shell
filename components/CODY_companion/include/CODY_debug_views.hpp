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

#include "CODY_rig.hpp"
#include "CODY2D_text.hpp"

namespace CODY {

constexpr uint8_t kDebugChannelVisibleRows = 8;

// Prepared text borrows this object's own bounded strings; never copy a prepared list.
struct DebugChannelList {
    char text[kDebugChannelVisibleRows + 2][40]{};
    CODY2D::TextLine lines[kDebugChannelVisibleRows + 2]{};
    uint8_t line_count = 0;
    bool valid = false;
};

// face: Actual composed values from the single current frame; no sensor acquisition occurs.
// first_channel: Absolute first catalog row, rejected when outside the real channel count.
// font: Native ASCII body font shared with the simulator's C++ raster.
// list: Stable owner for strings/layout; prepared lines borrow its own storage.
// Exception: Returns real layout/finite-value errors; never repairs channel values.
esp_err_t prepare_debug_channel_list(const Channels &face, uint8_t first_channel,
                                    const CODY2D::MonoFont &font, DebugChannelList &list);

// list: Immutable successfully prepared live channel text.
// strip: Borrowed RGB565 tile, using the same logical rotation as character rendering.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns real native raster errors; no display/transport operation occurs here.
esp_err_t draw_debug_channel_list_rows(const DebugChannelList &list, CODY2D::Surface strip,
                                      uint16_t first_row, uint16_t first_column);

// strip: Borrowed native RGB565 tile for the reversible debug view.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Returns real raster errors; full Back hit target is retained separately.
esp_err_t draw_debug_view_back_rows(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

// point: Logical acquired touch coordinate; the full 60x60 lateral circle is active.
bool debug_view_back_contains(CODY2D::Point point);

}  // namespace CODY
