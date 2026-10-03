#pragma once

#include <cstdint>

#include "esp_err.h"

namespace CODY2D {

// Explicit limits keep source walks and raster loops bounded on the device.
constexpr uint16_t kMaxDimension = 1024;
constexpr uint16_t kMaxPaletteColors = 256;
constexpr uint32_t kVerticalGradientLevels = 32;

enum class Rgb565ByteOrder : uint8_t {
    Native,
    ByteSwapped,
};

// byte_order: Typed RGB565 storage representation to validate.
constexpr bool valid_rgb565_byte_order(Rgb565ByteOrder byte_order)
{
    return byte_order == Rgb565ByteOrder::Native ||
           byte_order == Rgb565ByteOrder::ByteSwapped;
}

// color: RGB565 word whose two bytes exchange positions.
constexpr uint16_t swap_rgb565_bytes(uint16_t color)
{
    return static_cast<uint16_t>((color << 8U) | (color >> 8U));
}

// native_color: Native RGB565 word supplied by every public drawing API.
// byte_order: Validated destination storage representation.
constexpr uint16_t rgb565_native_to_stored(
    uint16_t native_color, Rgb565ByteOrder byte_order
)
{
    return byte_order == Rgb565ByteOrder::ByteSwapped
        ? swap_rgb565_bytes(native_color)
        : native_color;
}

// stored_color: RGB565 word read from storage in the declared representation.
// byte_order: Validated source storage representation.
constexpr uint16_t rgb565_stored_to_native(
    uint16_t stored_color, Rgb565ByteOrder byte_order
)
{
    // Byte swapping is its own inverse; native storage is the identity case.
    return rgb565_native_to_stored(stored_color, byte_order);
}

struct Surface {
    uint16_t *pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t stride = 0;
    uint32_t pixel_capacity = 0;
    Rgb565ByteOrder byte_order = Rgb565ByteOrder::Native;
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

// surface: Externally owned typed RGB565 storage, with stride and capacity in pixels.
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
