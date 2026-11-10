#pragma once

#include "CODY2D_controls.hpp"
#include "CODY_pixel_glyphs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace CODY::wifi_menu_art {

using Face = std::array<uint16_t, pixel_glyphs::kGlyphRows>;
inline constexpr Face kHeadingFace{0xE63F, 0xBD5F, 0xA4FF, 0x9C7F, 0x945F, 0x83BF, 0x737C};
inline constexpr Face kIdentityFace{0xFFDF, 0xEF7F, 0xE75F, 0xE71F, 0xDEDF, 0xDEBF, 0xCE5F};
inline constexpr Face kChoiceFace{0xEFFF, 0xBFFF, 0x9F5F, 0xAEDF, 0xC65F, 0xD5DF, 0xC51F};
inline constexpr char kHeadingText[] = "SET UP WI-FI";
inline constexpr char kPairText[] = "PAIR WITH BLUETOOTH";
inline constexpr char kWifiText[] = "SETUP WI-FI";
inline constexpr char kNamePrefix[] = "AiGadget-";
inline constexpr char kHexDigits[] = "0123456789ABCDEF";
inline constexpr size_t kSuffixLength = 6;
inline constexpr size_t kNameLength = sizeof(kNamePrefix) - 1U + kSuffixLength;
inline constexpr uint16_t kCellAdvance = pixel_glyphs::kGlyphColumns + 1U;
inline constexpr uint16_t kCenter = 233;
inline constexpr uint16_t kIdentityScale = 3;
inline constexpr uint16_t kChoiceIconSize = 24;
inline constexpr uint16_t kChoiceGap = 10;

template <size_t Width, size_t Height>
struct Bitmap {
    static constexpr size_t width = Width;
    static constexpr size_t height = Height;
    std::array<uint16_t, Width * Height> pixels{};
};

// text: Compile-time caption, including its terminator.
// face: Precomputed RGB565 colors for the seven rows of each letter.
template <size_t ScaleX, size_t ScaleY, size_t N>
constexpr auto bake(const char (&text)[N], const Face &face)
{
    static_assert(N > 1 && ScaleX > 0 && ScaleY > 0);
    constexpr auto width = ((N - 1U) * kCellAdvance - 1U) * ScaleX + 4U;
    constexpr auto height = pixel_glyphs::kGlyphRows * ScaleY + 4U;
    Bitmap<width, height> bitmap{};
    // Compose depth, keyline and face once in flash. Zero pixels are transparent.
    for (size_t layer = 0; layer < 3; ++layer) {
        for (size_t index = 0; index + 1U < N; ++index) {
            const auto *glyph = pixel_glyphs::find_glyph(text[index]);
            if (glyph == nullptr) continue;
            for (size_t row = 0; row < pixel_glyphs::kGlyphRows; ++row) {
                for (size_t col = 0; col < pixel_glyphs::kGlyphColumns; ++col) {
                    if ((glyph->columns[col] & (1U << row)) == 0) continue;
                    const size_t x = (index * kCellAdvance + col) * ScaleX;
                    const size_t y = row * ScaleY;
                    const size_t x_offset = layer == 0 ? 3U : layer == 1 ? 0U : 1U;
                    const size_t y_offset = layer == 0 ? 3U : layer == 1 ? 0U : 1U;
                    const size_t thickness = layer == 1 ? 2U : 0U;
                    const uint16_t ink = layer == 0 ? 0x49B7 : layer == 1 ? 0x190B : face[row];
                    for (size_t dy = 0; dy < ScaleY + thickness; ++dy) {
                        for (size_t dx = 0; dx < ScaleX + 1U + thickness; ++dx) {
                            bitmap.pixels[(y + y_offset + dy) * width + x + x_offset + dx] = ink;
                        }
                    }
                }
            }
        }
    }
    return bitmap;
}

inline constexpr auto kHeading = bake<3, 3>(kHeadingText, kHeadingFace);
inline constexpr auto kPair = bake<2, 3>(kPairText, kChoiceFace);
inline constexpr auto kWifi = bake<2, 3>(kWifiText, kChoiceFace);
inline constexpr auto kIdentityPrefix = bake<kIdentityScale, kIdentityScale>(kNamePrefix, kIdentityFace);
inline constexpr auto kIdentityUnavailable = bake<kIdentityScale, kIdentityScale>("NAME UNAVAILABLE", kIdentityFace);
inline constexpr auto kIdentityDigits = [] {
    using Digit = decltype(bake<kIdentityScale, kIdentityScale>("0", kIdentityFace));
    std::array<Digit, sizeof(kHexDigits) - 1U> digits{};
    for (size_t index = 0; index < digits.size(); ++index) {
        const char text[]{kHexDigits[index], '\0'};
        digits[index] = bake<kIdentityScale, kIdentityScale>(text, kIdentityFace);
    }
    return digits;
}();

// bitmap: Immutable precolored flash artwork.
// x: Logical left edge of the artwork.
// y: Logical top edge of the artwork.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
template <size_t Width, size_t Height>
void blit(const Bitmap<Width, Height> &bitmap, uint16_t x, uint16_t y,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto left = std::max<uint16_t>(x, first_column);
    const auto right = std::min<uint16_t>(x + Width, first_column + strip.width);
    const auto top = std::max<uint16_t>(y, first_row);
    const auto bottom = std::min<uint16_t>(y + Height, first_row + strip.height);
    if (left >= right || top >= bottom) return;
    for (uint16_t row = top; row < bottom; ++row) {
        const auto *source = bitmap.pixels.data() + (row - y) * Width + left - x;
        auto *destination = strip.pixels + (row - first_row) * strip.stride + left - first_column;
        for (uint16_t col = left; col < right; ++col, ++source, ++destination) {
            if (*source != 0) *destination = CODY2D::rgb565_native_to_stored(*source, strip.byte_order);
        }
    }
}

// name: Validated AiGadget identity or an empty string when unpublished.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
inline void draw_identity(const char *name, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    constexpr uint16_t y = 118;
    if (name[0] == '\0') {
        blit(kIdentityUnavailable, kCenter - kIdentityUnavailable.width / 2U, y, strip, first_row, first_column);
        return;
    }
    constexpr uint16_t advance = kIdentityScale * kCellAdvance;
    constexpr uint16_t width = (kNameLength - 1U) * advance + kIdentityDigits[0].width;
    constexpr uint16_t left = kCenter - width / 2U;
    blit(kIdentityPrefix, left, y, strip, first_row, first_column);
    for (size_t index = sizeof(kNamePrefix) - 1U; index < kNameLength; ++index) {
        const char value = name[index];
        const size_t digit = value <= '9' ? value - '0' : value - 'A' + 10U;
        blit(kIdentityDigits[digit], left + index * advance, y, strip, first_row, first_column);
    }
}

// bitmap: Immutable caption for the selected action identity.
// icon: Native Bluetooth or Wi-Fi outline to the left of the lettering.
// bounds: Complete prepared touch target, with centered artwork inside it.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Returns the native icon raster error.
template <size_t Width, size_t Height>
esp_err_t draw_choice(const Bitmap<Width, Height> &bitmap, CODY2D::ActionIcon icon,
    CODY2D::Rect bounds, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const uint16_t x = bounds.x + (bounds.width - Width - kChoiceIconSize - kChoiceGap) / 2U;
    const uint16_t y = bounds.y + (bounds.height - Height) / 2U;
    blit(bitmap, x + kChoiceIconSize + kChoiceGap, y, strip, first_row, first_column);
    const CODY2D::Rect icon_bounds{x, static_cast<uint16_t>(bounds.y + (bounds.height - kChoiceIconSize) / 2U),
        kChoiceIconSize, kChoiceIconSize};
    return CODY2D::draw_action_icon_rows(icon, icon_bounds, 0xBFFF, strip, first_row, first_column);
}

} // namespace CODY::wifi_menu_art
