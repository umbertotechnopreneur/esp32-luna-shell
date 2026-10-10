#pragma once

#include "CODY2D_text.hpp"

namespace CODY2D {

enum class ActionIcon : uint8_t {
    None, Link, Wifi, Gear, Check, Close, Arrow, Qr, Retry,
    Motion, Rotate, Sound, Orientation
};

struct CapsuleControl {
    Rect bounds{};
    TextLine caption{};
    uint16_t top_color = 0;
    uint16_t bottom_color = 0;
    bool enabled = true;
    uint16_t corner_radius = 0; // Zero retains the full capsule radius.
    uint16_t border_color = 0;
    ActionIcon icon = ActionIcon::None;
    Point icon_center{};
    uint16_t icon_color = 0xFFFF;
};

// control: Immutable logical bounds, palette role and optional prepared caption.
// point: Logical touch position already mapped by the display rotation owner.
bool capsule_contains(const CapsuleControl &control, Point point);

// control: Immutable capsule; the same bounds also define its full touch target.
// strip: Borrowed RGB565 tile, retaining stride and storage byte order.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
esp_err_t draw_capsule_rows(const CapsuleControl &control, Surface strip,
                           uint16_t first_row, uint16_t first_column);

} // namespace CODY2D
