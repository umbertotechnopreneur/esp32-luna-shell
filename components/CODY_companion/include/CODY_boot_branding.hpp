#pragma once

#include "CODY2D_text.hpp"
#include "VIBEWARE_boot_animation.h"
#include "vibeware_boot_data.h"
#include "esp_log.h"
#include <cstddef>

namespace CODY::BootBranding {
inline constexpr uint16_t kCanvas = 466;
inline constexpr CODY2D::Rect kArtwork{137, 164, 192, 219};
inline constexpr char kTitle[] = "Cody AI Companion";
inline constexpr char kSubtitle[] = "A distraction-free sidekick";
inline constexpr uint16_t kTitleY = 60;
inline constexpr uint16_t kSubtitleY = 110;

struct Presentation {
    CODY2D::TextLine title{};
    CODY2D::TextLine subtitle{};
    bool valid = false;
};

// Keep the 512-byte decode row in the serialized component owner, never in a render callback's stack.
struct Scratch {
    uint16_t source_row[VIBEWARE_BOOT_WIDTH]{};
};

// operation: Rejected preparation or rendering operation.
// error: Original specific cause returned to the startup/frame owner.
inline esp_err_t reject(const char *operation, esp_err_t error)
{
    ESP_LOGE("CodyBootBranding", "%s failed: %s", operation, esp_err_to_name(error));
    return error;
}

// bounds: Full logical rectangle, checked against the narrowest occupied circular chord.
inline bool fits_circle(CODY2D::Rect bounds)
{
    const int32_t center = kCanvas / 2;
    const int32_t left = bounds.x - center;
    const int32_t right = bounds.x + bounds.width - center;
    const int32_t top = bounds.y - center;
    const int32_t bottom = bounds.y + bounds.height - center;
    const int32_t radius_squared = center * center;
    return left * left + top * top <= radius_squared &&
        right * right + top * top <= radius_squared &&
        left * left + bottom * bottom <= radius_squared &&
        right * right + bottom * bottom <= radius_squared;
}

// font: Existing immutable native font; its actual metrics determine centering.
// text: Counted bounded printable ASCII identity, independent of typewriter/audio timing.
// top: Logical top row above the complete animated-artwork region.
// color: Native RGB565 foreground color.
// line: Receives the accepted centered line, never a truncated caption.
// Exception: Returns original font/preparation errors or a specific geometry rejection.
inline esp_err_t prepare_line(const CODY2D::MonoFont &font, CODY2D::TextSpan text,
    uint16_t top, uint16_t color, CODY2D::TextLine &line)
{
    const esp_err_t font_error = CODY2D::validate_mono_font(font);
    if (font_error != ESP_OK) {
        return reject("Font validation", font_error);
    }
    if (text.length == 0) {
        return reject("Empty caption", ESP_ERR_INVALID_ARG);
    }
    const uint32_t width = static_cast<uint32_t>(text.length - 1) * font.advance + font.width;
    if (width > kCanvas || static_cast<uint32_t>(top) + font.height > kArtwork.y) {
        ESP_LOGE("CodyBootBranding", "Caption rejected: width=%lu top=%u height=%u artwork_top=%u",
            static_cast<unsigned long>(width), top, font.height, kArtwork.y);
        return reject("Caption/artwork separation", ESP_ERR_INVALID_SIZE);
    }
    const uint16_t left = static_cast<uint16_t>((kCanvas - width) / 2);
    const esp_err_t error = CODY2D::prepare_text_line(font, text,
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, color, line);
    if (error != ESP_OK) {
        return reject("Caption preparation", error);
    }
    if (!fits_circle(line.bounds)) {
        ESP_LOGE("CodyBootBranding", "Caption chord rejected: x=%u y=%u width=%u height=%u",
            line.bounds.x, line.bounds.y, line.bounds.width, line.bounds.height);
        return reject("Caption circular chord", ESP_ERR_INVALID_SIZE);
    }
    return ESP_OK;
}

// title_font: Canonical large title font shared with the firmware's scenes.
// body_font: Canonical normal-size body font shared with the firmware's scenes.
// presentation: Component-owned prepared layout, published only after both lines are accepted.
// Exception: Returns the actual failed font/layout operation; startup must not hide partial branding.
inline esp_err_t prepare(const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    Presentation &presentation)
{
    Presentation candidate;
    esp_err_t error = prepare_line(title_font, {kTitle, sizeof(kTitle) - 1}, kTitleY, 0xffff,
        candidate.title);
    if (error != ESP_OK) {
        return error;
    }
    error = prepare_line(body_font, {kSubtitle, sizeof(kSubtitle) - 1}, kSubtitleY, 0xbdf7,
        candidate.subtitle);
    if (error != ESP_OK) {
        return error;
    }
    if (candidate.title.bounds.y + candidate.title.bounds.height > candidate.subtitle.bounds.y ||
        !fits_circle(kArtwork)) {
        return reject("Branding layout separation", ESP_ERR_INVALID_SIZE);
    }
    candidate.valid = true;
    presentation = candidate;
    return ESP_OK;
}

// presentation: Immutable owner-prepared title/subtitle, displayed together from the first frame.
// scratch: Exclusively borrowed component-owned single decoded source row.
// frame_index: Original approved GIF resource ordinal; frame duration and sequence remain unchanged.
// strip: Borrowed logical full-width RGB565 tile, retaining native/panel byte order and capacity.
// first_row: Logical canvas y origin shared with the display's existing rotation mapping.
// Exception: Returns every actual validation, decoder and text-raster failure to the frame owner.
inline esp_err_t draw_rows(const Presentation &presentation, Scratch &scratch,
    uint16_t frame_index, CODY2D::Surface strip, uint16_t first_row)
{
    if (!presentation.valid || frame_index >= VIBEWARE_BOOT_FRAME_COUNT || strip.width != kCanvas ||
        first_row >= kCanvas || strip.height > kCanvas - first_row) {
        ESP_LOGE("CodyBootBranding", "Frame rejected: valid=%u frame=%u width=%u height=%u first_row=%u",
            presentation.valid, frame_index, strip.width, strip.height, first_row);
        return reject("Branding frame bounds", ESP_ERR_INVALID_ARG);
    }
    esp_err_t error = CODY2D::clear(strip, 0);
    if (error != ESP_OK) {
        return reject("Branding strip", error);
    }
    for (uint16_t row = 0; row < strip.height; ++row) {
        const uint16_t canvas_y = first_row + row;
        if (canvas_y < kArtwork.y || canvas_y >= kArtwork.y + kArtwork.height) {
            continue;
        }
        // Center-sampled nearest-neighbor scaling preserves the complete artwork
        // and deterministically selects the same source pixels on device and PC.
        const uint16_t source_y = static_cast<uint16_t>(
            ((2U * (canvas_y - kArtwork.y) + 1U) * VIBEWARE_BOOT_HEIGHT) / (2U * kArtwork.height));
        error = VIBEWARE_boot_decode_rows(frame_index, source_y, 1, scratch.source_row,
            VIBEWARE_BOOT_WIDTH, VIBEWARE_BOOT_WIDTH, false);
        if (error != ESP_OK) {
            return error;
        }
        uint16_t *destination = strip.pixels + static_cast<size_t>(row) * strip.stride + kArtwork.x;
        for (uint16_t column = 0; column < kArtwork.width; ++column) {
            const uint16_t source_x = static_cast<uint16_t>(
                ((2U * column + 1U) * VIBEWARE_BOOT_WIDTH) / (2U * kArtwork.width));
            destination[column] = CODY2D::rgb565_native_to_stored(scratch.source_row[source_x], strip.byte_order);
        }
    }
    error = CODY2D::draw_text_rows(presentation.title, presentation.title.text.length, strip, first_row, 0);
    if (error != ESP_OK) {
        return reject("Title raster", error);
    }
    error = CODY2D::draw_text_rows(presentation.subtitle, presentation.subtitle.text.length, strip, first_row, 0);
    return error == ESP_OK ? ESP_OK : reject("Subtitle raster", error);
}

} // namespace CODY::BootBranding
