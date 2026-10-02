#pragma once

#include <cstdint>

#include "esp_err.h"

namespace CODY2D {

// Explicit limits keep source walks and raster loops bounded on the device.
constexpr uint16_t kMaxDimension = 1024;
constexpr uint16_t kMaxPaletteColors = 256;
constexpr uint32_t kVerticalGradientLevels = 32;

struct Surface {
    uint16_t *pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t stride = 0;
    uint32_t pixel_capacity = 0;
};

struct Rect {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

struct Point {
    int16_t x = 0;
    int16_t y = 0;
};

// All pointed-to storage belongs to the caller and remains readable for the call.
// row_offsets has exactly height + 1 uint32_t entries. Every run is three bytes:
// a nonzero little-endian uint16_t pixel count, followed by a uint8_t palette index.
struct IndexedRleImage {
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t palette_count = 0;
    const uint16_t *palette = nullptr;
    const uint32_t *row_offsets = nullptr;
    const uint8_t *runs = nullptr;
    uint32_t runs_size = 0;
};

// surface: Externally owned native RGB565 storage, with stride and capacity in pixels.
esp_err_t validate_surface(Surface surface);

// image: Complete immutable indexed image and its readable palette, offsets, and run storage.
esp_err_t validate_indexed_rle_image(const IndexedRleImage &image);

// surface: Destination RGB565 surface; row padding is left untouched.
// color: Native RGB565 color written to every visible pixel.
esp_err_t clear(Surface surface, uint16_t color);

// surface: Destination RGB565 surface with explicit writable capacity.
// rect: Nonempty rectangle wholly inside the surface; coordinates are not clipped.
// color: Native RGB565 fill color.
esp_err_t fill_rect(Surface surface, Rect rect, uint16_t color);

// surface: Destination RGB565 surface with explicit writable capacity.
// center: Circle center in surface pixel coordinates.
// radius: Whole-pixel radius; the complete circle must fit inside the surface.
// color: Native RGB565 fill color.
esp_err_t fill_circle(Surface surface, Point center, uint16_t radius, uint16_t color);

// surface: Destination RGB565 surface with explicit writable capacity.
// rect: Nonempty rectangle wholly inside the surface, defining the gradient's vertical span.
// top_color: Native RGB565 color at the first row of the rectangle.
// bottom_color: Native RGB565 color at the final row; a one-row gradient uses top_color.
esp_err_t fill_vertical_gradient32(
    Surface surface, Rect rect, uint16_t top_color, uint16_t bottom_color
);

// image: Immutable source; its metadata and requested rows are checked before any write.
// first_row: First image row to decode; destination.height determines the number of rows.
// destination: RGB565 strip with width equal to image.width; stride and capacity are in pixels.
esp_err_t decode_rows(
    const IndexedRleImage &image, uint32_t first_row, Surface destination
);

}  // namespace CODY2D
