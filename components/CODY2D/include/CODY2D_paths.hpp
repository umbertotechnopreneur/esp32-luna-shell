#pragma once

#include "CODY2D.hpp"

namespace CODY2D {

struct Vec2 { float x = 0; float y = 0; };

// Paths use canvas coordinates. Only this explicit strip API clips to a tile.
constexpr uint16_t kMaxPathVertices = 256;
constexpr float kMaxStrokeWidth = 21;

// strip: Validated caller-owned RGB565 destination with its own width and stride.
// first_row: Canvas row represented by the strip's first pixel row.
// vertices: Readable closed polygon vertices in canvas coordinates.
// count: Number of vertices, bounded by kMaxPathVertices.
// palette: Readable 256-entry native RGB565 palette.
// first_color: Top gradient slot, or the solid color slot.
// last_color: Bottom gradient slot, equal to first_color for a solid fill.
// opacity: Source coverage from transparent zero to opaque 255, composited in native RGB565.
// first_column: Canvas column represented by the strip's first pixel column.
esp_err_t fill_path_rows(Surface strip, uint16_t first_row,
                        const Vec2 *vertices, uint16_t count,
                        const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                        uint8_t opacity = 255, uint16_t first_column = 0);

// strip: Caller-owned RGB565 destination.
// first_row: Global canvas row represented by this strip.
// from: Segment start in canvas coordinates.
// to: Segment end in canvas coordinates; an equal endpoint draws a dot.
// width: Positive finite stroke diameter in pixels, at most kMaxStrokeWidth.
// color: Native RGB565 stroke color, with round caps.
// first_column: Canvas column represented by the strip's first pixel column.
esp_err_t stroke_rows(Surface strip, uint16_t first_row,
                      Vec2 from, Vec2 to, float width, uint16_t color,
                      uint16_t first_column = 0);

}  // namespace CODY2D
