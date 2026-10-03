#include "CODY2D.hpp"

#include <cstdint>

#include "esp_log.h"

namespace CODY2D {
namespace {

constexpr char kTag[] = "CODY2D";

// surface: Already validated writable RGB565 surface.
// row: Visible destination row within the surface.
// first_column: First included column of an already validated span.
// count: Number of pixels in the span, without writing stride padding.
// stored_color: RGB565 color already encoded for the surface byte order.
void fill_row(
    Surface surface, uint32_t row, uint32_t first_column,
    uint32_t count, uint16_t stored_color
)
{
    uint16_t *destination = surface.pixels + row * surface.stride + first_column;
    for (uint32_t column = 0; column < count; ++column) {
        destination[column] = stored_color;
    }
}

// surface: Already validated destination surface.
// rect: Requested nonempty rectangle, which must remain wholly inside the surface.
// operation: Raster operation included in a rejection log.
esp_err_t validate_rect(Surface surface, Rect rect, const char *operation)
{
    const uint32_t right = static_cast<uint32_t>(rect.x) + rect.width;
    const uint32_t bottom = static_cast<uint32_t>(rect.y) + rect.height;
    if (rect.width == 0 || rect.height == 0) {
        ESP_LOGE(kTag, "%s rejected empty rectangle: x=%u y=%u width=%u height=%u (%s)",
                 operation, rect.x, rect.y, rect.width, rect.height,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (right > surface.width || bottom > surface.height) {
        ESP_LOGE(kTag, "%s rejected rectangle outside %ux%u surface: x=%u y=%u width=%u height=%u (%s)",
                 operation, surface.width, surface.height,
                 rect.x, rect.y, rect.width, rect.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// top_color: Native RGB565 color at level zero.
// bottom_color: Native RGB565 color at level 31.
// level: Validated gradient level in the inclusive range zero through 31.
uint16_t gradient_color(uint16_t top_color, uint16_t bottom_color, uint32_t level)
{
    constexpr uint32_t last_level = kVerticalGradientLevels - 1;
    const uint32_t inverse = last_level - level;
    const uint32_t red = (((top_color >> 11) & 31U) * inverse +
                          ((bottom_color >> 11) & 31U) * level + last_level / 2) / last_level;
    const uint32_t green = (((top_color >> 5) & 63U) * inverse +
                            ((bottom_color >> 5) & 63U) * level + last_level / 2) / last_level;
    const uint32_t blue = ((top_color & 31U) * inverse +
                           (bottom_color & 31U) * level + last_level / 2) / last_level;
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

// runs: Readable run storage after complete source validation.
// offset: Byte offset of a complete three-byte run inside that storage.
uint16_t run_count(const uint8_t *runs, uint32_t offset)
{
    return static_cast<uint16_t>(runs[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(runs[offset + 1]) << 8);
}

}  // namespace

// surface: Externally owned typed RGB565 storage, with stride and capacity in pixels.
esp_err_t validate_surface(Surface surface)
{
    if (!valid_rgb565_byte_order(surface.byte_order)) {
        ESP_LOGE(kTag, "Surface byte order rejected: order=%u (%s)",
                 static_cast<unsigned>(surface.byte_order),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (surface.pixels == nullptr ||
        reinterpret_cast<uintptr_t>(surface.pixels) % alignof(uint16_t) != 0 ||
        surface.width == 0 || surface.height == 0 ||
        surface.width > kMaxDimension || surface.height > kMaxDimension ||
        surface.stride < surface.width) {
        ESP_LOGE(kTag, "Surface rejected: pixels=%p width=%u height=%u stride=%u capacity=%lu maximum_dimension=%u (%s)",
                 surface.pixels, surface.width, surface.height, surface.stride,
                 static_cast<unsigned long>(surface.pixel_capacity), kMaxDimension,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // The dimension and stride types bound this product below UINT32_MAX.
    const uint32_t required_pixels =
        static_cast<uint32_t>(surface.height - 1) * surface.stride + surface.width;
    if (surface.pixel_capacity < required_pixels) {
        ESP_LOGE(kTag, "Surface capacity rejected: supplied=%lu required=%lu width=%u height=%u stride=%u (%s)",
                 static_cast<unsigned long>(surface.pixel_capacity),
                 static_cast<unsigned long>(required_pixels),
                 surface.width, surface.height, surface.stride,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

namespace {

// image: Immutable indexed image with readable palette, offset, and run storage extents.
esp_err_t validate_indexed_rle_metadata(const IndexedRleImage &image)
{
    if (image.width == 0 || image.height == 0 ||
        image.width > kMaxDimension || image.height > kMaxDimension ||
        image.palette_count == 0 || image.palette_count > kMaxPaletteColors ||
        image.palette == nullptr || image.row_offsets == nullptr || image.runs == nullptr ||
        reinterpret_cast<uintptr_t>(image.palette) % alignof(uint16_t) != 0 ||
        reinterpret_cast<uintptr_t>(image.row_offsets) % alignof(uint32_t) != 0) {
        ESP_LOGE(kTag, "RLE image metadata rejected: width=%u height=%u colors=%u palette=%p offsets=%p runs=%p (%s)",
                 image.width, image.height, image.palette_count,
                 image.palette, image.row_offsets, image.runs,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t maximum_run_bytes =
        static_cast<uint32_t>(image.width) * image.height * 3U;
    if (image.runs_size == 0 || image.runs_size > maximum_run_bytes ||
        image.runs_size % 3U != 0 || image.row_offsets[0] != 0 ||
        image.row_offsets[image.height] != image.runs_size) {
        ESP_LOGE(kTag, "RLE image extent rejected: bytes=%lu maximum=%lu first_offset=%lu final_offset=%lu (%s)",
                 static_cast<unsigned long>(image.runs_size),
                 static_cast<unsigned long>(maximum_run_bytes),
                 static_cast<unsigned long>(image.row_offsets[0]),
                 static_cast<unsigned long>(image.row_offsets[image.height]),
                 esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

// image: Immutable indexed image whose metadata and declared storage extents are validated.
// first_row: First source row of an already validated nonempty range.
// row_count: Number of source rows within image.height to inspect before any write.
esp_err_t validate_indexed_rle_rows(
    const IndexedRleImage &image, uint32_t first_row, uint32_t row_count
)
{
    const uint32_t end_row = first_row + row_count;
    for (uint32_t row = first_row; row < end_row; ++row) {
        const uint32_t start = image.row_offsets[row];
        const uint32_t end = image.row_offsets[row + 1];
        if (start >= end || end > image.runs_size || start % 3U != 0 || end % 3U != 0) {
            ESP_LOGE(kTag, "RLE row offsets rejected: row=%lu start=%lu end=%lu bytes=%lu (%s)",
                     static_cast<unsigned long>(row), static_cast<unsigned long>(start),
                     static_cast<unsigned long>(end), static_cast<unsigned long>(image.runs_size),
                     esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
        uint32_t pixels = 0;
        for (uint32_t offset = start; offset < end; offset += 3U) {
            const uint16_t count = run_count(image.runs, offset);
            const uint8_t palette_index = image.runs[offset + 2];
            if (count == 0 || palette_index >= image.palette_count ||
                count > static_cast<uint32_t>(image.width) - pixels) {
                ESP_LOGE(kTag, "RLE run rejected: row=%lu offset=%lu count=%u index=%u colors=%u written=%lu width=%u (%s)",
                         static_cast<unsigned long>(row), static_cast<unsigned long>(offset),
                         count, palette_index, image.palette_count,
                         static_cast<unsigned long>(pixels), image.width,
                         esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                return ESP_ERR_INVALID_RESPONSE;
            }
            pixels += count;
        }
        if (pixels != image.width) {
            ESP_LOGE(kTag, "RLE row width rejected: row=%lu decoded=%lu expected=%u (%s)",
                     static_cast<unsigned long>(row), static_cast<unsigned long>(pixels),
                     image.width, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

}  // namespace

// image: Complete immutable indexed image and its readable palette, offsets, and run storage.
esp_err_t validate_indexed_rle_image(const IndexedRleImage &image)
{
    const esp_err_t result = validate_indexed_rle_metadata(image);
    if (result != ESP_OK) {
        return result;
    }
    return validate_indexed_rle_rows(image, 0, image.height);
}

// surface: Destination RGB565 surface; row padding is left untouched.
// color: Native RGB565 color written to every visible pixel.
esp_err_t clear(Surface surface, uint16_t color)
{
    const esp_err_t result = validate_surface(surface);
    if (result != ESP_OK) {
        return result;
    }
    const uint16_t stored_color = rgb565_native_to_stored(color, surface.byte_order);
    for (uint32_t row = 0; row < surface.height; ++row) {
        fill_row(surface, row, 0, surface.width, stored_color);
    }
    return ESP_OK;
}

// surface: Destination RGB565 surface with explicit writable capacity.
// rect: Nonempty rectangle wholly inside the surface; coordinates are not clipped.
// color: Native RGB565 fill color.
esp_err_t fill_rect(Surface surface, Rect rect, uint16_t color)
{
    esp_err_t result = validate_surface(surface);
    if (result != ESP_OK) {
        return result;
    }
    result = validate_rect(surface, rect, "Rectangle fill");
    if (result != ESP_OK) {
        return result;
    }
    const uint16_t stored_color = rgb565_native_to_stored(color, surface.byte_order);
    for (uint32_t row = rect.y; row < static_cast<uint32_t>(rect.y) + rect.height; ++row) {
        fill_row(surface, row, rect.x, rect.width, stored_color);
    }
    return ESP_OK;
}

// surface: Destination RGB565 surface with explicit writable capacity.
// center: Circle center in surface pixel coordinates.
// radius: Whole-pixel radius; the complete circle must fit inside the surface.
// color: Native RGB565 fill color.
esp_err_t fill_circle(Surface surface, Point center, uint16_t radius, uint16_t color)
{
    const esp_err_t result = validate_surface(surface);
    if (result != ESP_OK) {
        return result;
    }
    const int32_t left = static_cast<int32_t>(center.x) - radius;
    const int32_t top = static_cast<int32_t>(center.y) - radius;
    const int32_t right = static_cast<int32_t>(center.x) + radius;
    const int32_t bottom = static_cast<int32_t>(center.y) + radius;
    if (radius > kMaxDimension || left < 0 || top < 0 ||
        right >= surface.width || bottom >= surface.height) {
        ESP_LOGE(kTag, "Circle fill rejected: center=%d,%d radius=%u surface=%ux%u (%s)",
                 center.x, center.y, radius, surface.width, surface.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const int32_t radius_squared = static_cast<int32_t>(radius) * radius;
    const uint16_t stored_color = rgb565_native_to_stored(color, surface.byte_order);
    for (int32_t row = top; row <= bottom; ++row) {
        const int32_t distance_y = row - center.y;
        uint16_t *destination = surface.pixels + static_cast<uint32_t>(row) * surface.stride;
        for (int32_t column = left; column <= right; ++column) {
            const int32_t distance_x = column - center.x;
            if (distance_x * distance_x + distance_y * distance_y <= radius_squared) {
                destination[column] = stored_color;
            }
        }
    }
    return ESP_OK;
}

// surface: Destination RGB565 surface with explicit writable capacity.
// rect: Nonempty rectangle wholly inside the surface, defining the gradient's vertical span.
// top_color: Native RGB565 color at the first row of the rectangle.
// bottom_color: Native RGB565 color at the final row; a one-row gradient uses top_color.
esp_err_t fill_vertical_gradient32(
    Surface surface, Rect rect, uint16_t top_color, uint16_t bottom_color
)
{
    esp_err_t result = validate_surface(surface);
    if (result != ESP_OK) {
        return result;
    }
    result = validate_rect(surface, rect, "Vertical gradient fill");
    if (result != ESP_OK) {
        return result;
    }
    uint16_t colors[kVerticalGradientLevels];
    for (uint32_t level = 0; level < kVerticalGradientLevels; ++level) {
        const uint16_t native_color = gradient_color(top_color, bottom_color, level);
        colors[level] = rgb565_native_to_stored(native_color, surface.byte_order);
    }
    const uint32_t span = rect.height - 1U;
    for (uint32_t row = 0; row < rect.height; ++row) {
        const uint32_t level = span == 0 ? 0 :
            (row * (kVerticalGradientLevels - 1U) + span / 2U) / span;
        fill_row(surface, static_cast<uint32_t>(rect.y) + row, rect.x, rect.width, colors[level]);
    }
    return ESP_OK;
}

// color: Native RGB565 source color with its existing edge coverage already applied.
// opacity: Visibility against black; endpoints remain exact.
uint16_t fade_rgb565_to_black(uint16_t color, uint8_t opacity)
{
    const uint32_t red = (((color >> 11U) & 31U) * opacity + 127U) / 255U;
    const uint32_t green = (((color >> 5U) & 63U) * opacity + 127U) / 255U;
    const uint32_t blue = ((color & 31U) * opacity + 127U) / 255U;
    return static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
}

// surface: Caller-owned tile, validated before any mutation; row padding is untouched.
// opacity: Global layer visibility, never a display-backlight preference.
esp_err_t fade_to_black(Surface surface, uint8_t opacity)
{
    const esp_err_t error = validate_surface(surface);
    if (error != ESP_OK || opacity == 255) {
        return error;
    }
    if (opacity == 0) {
        return clear(surface, 0);
    }
    for (uint32_t row = 0; row < surface.height; ++row) {
        uint16_t *pixels = surface.pixels + row * surface.stride;
        for (uint32_t column = 0; column < surface.width; ++column) {
            const uint16_t native = rgb565_stored_to_native(pixels[column], surface.byte_order);
            pixels[column] = rgb565_native_to_stored(fade_rgb565_to_black(native, opacity), surface.byte_order);
        }
    }
    return ESP_OK;
}

// image: Immutable source; its metadata and requested rows are checked before any write.
// first_row: First image row to decode; destination.height determines the number of rows.
// destination: RGB565 strip with width equal to image.width; stride and capacity are in pixels.
esp_err_t decode_rows(
    const IndexedRleImage &image, uint32_t first_row, Surface destination
)
{
    esp_err_t result = validate_surface(destination);
    if (result != ESP_OK) {
        return result;
    }
    result = validate_indexed_rle_metadata(image);
    if (result != ESP_OK) {
        return result;
    }
    // Subtraction avoids overflowing a caller-provided uint32_t first_row.
    if (destination.width != image.width || first_row >= image.height ||
        destination.height > static_cast<uint32_t>(image.height) - first_row) {
        ESP_LOGE(kTag, "RLE row request rejected: first=%lu rows=%u strip_width=%u image=%ux%u (%s)",
                 static_cast<unsigned long>(first_row), destination.height,
                 destination.width, image.width, image.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    result = validate_indexed_rle_rows(image, first_row, destination.height);
    if (result != ESP_OK) {
        return result;
    }
    for (uint32_t row = 0; row < destination.height; ++row) {
        const uint32_t source_row = first_row + row;
        const uint32_t start = image.row_offsets[source_row];
        const uint32_t end = image.row_offsets[source_row + 1];
        uint32_t column = 0;
        for (uint32_t offset = start; offset < end; offset += 3U) {
            const uint16_t count = run_count(image.runs, offset);
            // Boot decoding is off the animation hot path. Converting each run
            // avoids reserving a 256-entry palette on the 6 KB worker stack.
            const uint16_t stored_color = rgb565_native_to_stored(
                image.palette[image.runs[offset + 2]], destination.byte_order
            );
            fill_row(destination, row, column, count, stored_color);
            column += count;
        }
    }
    return ESP_OK;
}

}  // namespace CODY2D
