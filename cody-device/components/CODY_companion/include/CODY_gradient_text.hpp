#pragma once

#include <algorithm>
#include <array>
#include "CODY2D.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

// value: Printable ASCII character in a context-specific action caption.
// Returns its seven-row Home pixel glyph, or null for unsupported characters.
inline const std::array<uint8_t, 7> *gradient_pixel_glyph(char value)
{
    static constexpr std::array<std::array<uint8_t, 7>, 26> upper{{
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {15,16,16,19,17,17,15}, {17,17,17,31,17,17,17}, {31,4,4,4,4,4,31},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    }};
    static constexpr std::array<std::array<uint8_t, 7>, 26> lower{{
        {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30}, {0,0,15,16,16,16,15},
        {1,1,15,17,17,17,15}, {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8},
        {0,15,17,17,15,1,14}, {16,16,30,17,17,17,17}, {4,0,12,4,4,4,14},
        {2,0,6,2,2,18,12}, {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
        {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17}, {0,0,14,17,17,17,14},
        {0,30,17,17,30,16,16}, {0,15,17,17,15,1,1}, {0,0,22,25,16,16,16},
        {0,0,15,16,14,1,30}, {8,8,28,8,8,9,6}, {0,0,17,17,17,19,13},
        {0,0,17,17,17,10,4}, {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
        {0,17,17,17,15,1,14}, {0,0,31,2,4,8,31}
    }};
    static constexpr std::array<uint8_t, 7> dot{0,0,0,0,0,6,6};
    static constexpr std::array<uint8_t, 7> dash{0,0,0,31,0,0,0};
    static constexpr std::array<uint8_t, 7> exclamation{4,4,4,4,4,0,4};
    if (value >= 'A' && value <= 'Z') return &upper[value - 'A'];
    if (value >= 'a' && value <= 'z') return &lower[value - 'a'];
    if (value == '.') return &dot;
    if (value == '-') return &dash;
    if (value == '!') return &exclamation;
    return nullptr;
}

// row: Zero-based visible glyph row.
// height: Complete glyph height in pixels.
// opacity: Enabled or faded action opacity.
inline uint16_t gradient_action_color(uint16_t row, uint16_t height, uint8_t opacity)
{
    const uint32_t level = row * 765U / std::max<uint32_t>(1U, height - 1U);
    const uint16_t color = level <= 255U ? CODY2D::blend_rgb565(
        Theme::gradients_home_action_text_segments_0_foreground,
        Theme::gradients_home_action_text_segments_0_background, static_cast<uint8_t>(level)) :
        level <= 510U ? CODY2D::blend_rgb565(
            Theme::gradients_home_action_text_segments_1_foreground,
            Theme::gradients_home_action_text_segments_1_background, static_cast<uint8_t>(level - 255U)) :
        CODY2D::blend_rgb565(Theme::gradients_home_action_text_segments_2_foreground,
            Theme::gradients_home_action_text_segments_2_background, static_cast<uint8_t>(level - 510U));
    return CODY2D::fade_rgb565_to_black(color, opacity);
}

} // namespace CODY
