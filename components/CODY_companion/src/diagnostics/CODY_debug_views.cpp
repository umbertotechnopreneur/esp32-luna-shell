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

#include "CODY2D_controls.hpp"

namespace CODY {
namespace {
constexpr uint16_t kTextColor = 0xF7BE;
}  // namespace

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
