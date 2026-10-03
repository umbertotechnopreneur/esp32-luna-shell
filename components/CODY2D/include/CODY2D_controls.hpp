#pragma once

#include "CODY2D_text.hpp"

namespace CODY2D {

struct CapsuleControl {
    Rect bounds{};
    TextLine caption{};
    uint16_t top_color = 0;
    uint16_t bottom_color = 0;
    bool enabled = true;
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
