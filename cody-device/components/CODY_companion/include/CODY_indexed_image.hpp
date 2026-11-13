#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "CODY2D.hpp"

namespace CODY {

// Display-owned metadata for immutable 8-bit indices preceded by 256 big-endian
// RGB565 colors. Only the 512-byte palette is copied, in the destination order.
// One display owner prepares and draws this cache between retired frames.
struct IndexedImage {
    std::array<uint16_t, 256> palette{};
    const uint8_t *resource = nullptr;
    const uint8_t *indices = nullptr;
    size_t resource_bytes = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    CODY2D::Rgb565ByteOrder byte_order = CODY2D::Rgb565ByteOrder::Native;
    bool valid = false;
};
static_assert(sizeof(IndexedImage) <= 576, "Indexed artwork must retain only a bounded palette and metadata");

// image: Display-owned cache; invalid input retires its previous binding.
// start: First readable byte of an immutable palette-plus-indices resource.
// end: One-past-end linker symbol for that same resource.
// width: Exact authored image width, without scaling.
// height: Exact authored image height, without scaling.
// byte_order: Destination RGB565 representation used by subsequent draws.
// Exception: Rejects missing storage, invalid geometry or a mismatched resource extent.
inline esp_err_t prepare_indexed_image(IndexedImage &image, const uint8_t *start, const uint8_t *end,
    uint16_t width, uint16_t height, CODY2D::Rgb565ByteOrder byte_order)
{
    if (start == nullptr || end == nullptr || !CODY2D::valid_rgb565_byte_order(byte_order)) {
        image.valid = false;
        return ESP_ERR_INVALID_ARG;
    }
    constexpr size_t palette_bytes = 256U * 2U;
    const auto first = reinterpret_cast<uintptr_t>(start);
    const auto last = reinterpret_cast<uintptr_t>(end);
    const size_t expected_bytes = palette_bytes + size_t{width} * height;
    if (width == 0 || height == 0 || width > CODY2D::kMaxDimension || height > CODY2D::kMaxDimension ||
        last < first || last - first != expected_bytes) {
        image.valid = false;
        return ESP_ERR_INVALID_SIZE;
    }
    if (image.valid && image.resource == start && image.resource_bytes == expected_bytes &&
        image.width == width && image.height == height && image.byte_order == byte_order) return ESP_OK;

    image.valid = false;
    for (size_t index = 0; index < image.palette.size(); ++index) {
        const auto native = static_cast<uint16_t>((uint16_t{start[index * 2U]} << 8U) | start[index * 2U + 1U]);
        image.palette[index] = CODY2D::rgb565_native_to_stored(native, byte_order);
    }
    image.resource = start;
    image.indices = start + palette_bytes;
    image.resource_bytes = expected_bytes;
    image.width = width;
    image.height = height;
    image.byte_order = byte_order;
    image.valid = true;
    return ESP_OK;
}

// image: Successfully prepared cache whose immutable source remains readable.
// bounds: Logical image placement with the exact authored width and height.
// strip: Borrowed destination tile; stride, capacity and byte order are preserved.
// first_row: Logical y coordinate of the tile's first row.
// first_column: Logical x coordinate of the tile's first column.
// Exception: Preserves surface errors and rejects unprepared or incompatible geometry before drawing.
inline esp_err_t draw_indexed_image_rows(const IndexedImage &image, CODY2D::Rect bounds,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    if (!image.valid || image.indices == nullptr || image.resource == nullptr) return ESP_ERR_INVALID_STATE;
    const uint32_t image_right = uint32_t{bounds.x} + bounds.width;
    const uint32_t image_bottom = uint32_t{bounds.y} + bounds.height;
    const uint32_t tile_right = uint32_t{first_column} + strip.width;
    const uint32_t tile_bottom = uint32_t{first_row} + strip.height;
    if (bounds.width != image.width || bounds.height != image.height || image.width == 0 || image.height == 0 ||
        image_right > CODY2D::kMaxDimension || image_bottom > CODY2D::kMaxDimension ||
        tile_right > CODY2D::kMaxDimension || tile_bottom > CODY2D::kMaxDimension ||
        image.byte_order != strip.byte_order) return ESP_ERR_INVALID_SIZE;

    const uint32_t left = std::max<uint32_t>(first_column, bounds.x);
    const uint32_t right = std::min(tile_right, image_right);
    const uint32_t top = std::max<uint32_t>(first_row, bounds.y);
    const uint32_t bottom = std::min(tile_bottom, image_bottom);
    if (left >= right || top >= bottom) return ESP_OK;
    for (uint32_t y = top; y < bottom; ++y) {
        const auto *source = image.indices + size_t{y - bounds.y} * image.width;
        auto *destination = strip.pixels + size_t{y - first_row} * strip.stride;
        for (uint32_t x = left; x < right; ++x) {
            destination[x - first_column] = image.palette[source[x - bounds.x]];
        }
    }
    return ESP_OK;
}

} // namespace CODY
