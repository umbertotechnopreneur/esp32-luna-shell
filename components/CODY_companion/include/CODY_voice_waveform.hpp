#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include "CODY2D.hpp"

namespace CODY {

inline constexpr int kVoiceRibbonLeft = 41;
inline constexpr int kVoiceRibbonWidth = 384;
inline constexpr int kVoiceRibbonTop = 184;
inline constexpr int kVoiceRibbonHeight = 48;
inline constexpr int64_t kVoiceRibbonFrameUs = 33333;
inline constexpr unsigned kVoiceRibbonNodes = 49;
inline constexpr unsigned kVoiceRibbonLayers = 4;

struct VoiceRibbonNode {
    int16_t center_q8 = 0;
    uint16_t radius_q8 = 0;
};
struct VoiceRibbonFrame {
    VoiceRibbonNode nodes[kVoiceRibbonLayers][kVoiceRibbonNodes]{};
};
static_assert(sizeof(VoiceRibbonFrame) <= 800, "Ribbon knots belong to the display owner, not a DMA callback stack");

// phase: One complete periodic turn in the low 16 bits; interpolation is Q15.
inline int voice_ribbon_sine(uint32_t phase)
{
    constexpr int16_t quarter[] = {0,3212,6393,9512,12539,15446,18204,20787,
        23170,25329,27245,28898,30273,31356,32137,32609,32767};
    const unsigned turn = phase & 65535U;
    const unsigned quadrant = turn >> 14;
    const unsigned folded = (quadrant & 1U) ? 16384U - (turn & 16383U) : turn & 16383U;
    const unsigned node = std::min(15U, folded >> 10);
    const unsigned fraction = folded - node * 1024U;
    const int value = quarter[node] + (quarter[node + 1] - quarter[node]) * int(fraction) / 1024;
    return quadrant >= 2 ? -value : value;
}

// now_us: Monotonic presentation time; periodic shape is artwork, not sampled PCM.
// energy: Visual gain derived from a fresh canonical microphone level, zero when unavailable.
// frame: Display-owned knots, prepared once and reused by every DMA strip of this frame.
inline void prepare_voice_ribbon(int64_t now_us, uint8_t energy, VoiceRibbonFrame &frame)
{
    const uint32_t time = static_cast<uint32_t>((std::max<int64_t>(0, now_us) % 12000000) * 65536 / 3000000);
    for (unsigned layer = 0; layer < kVoiceRibbonLayers; ++layer) {
        const uint32_t phase = (layer & 1U) ? 0U - time : time;
        for (unsigned node = 0; node < kVoiceRibbonNodes; ++node) {
            const int position = int(node) * 256 / 48 - 128;
            const int envelope = (16384 - position * position) / 64;
            const uint32_t spatial = node * 65536U / 48;
            const int carrier = voice_ribbon_sine(spatial + phase + layer * 14500U);
            const int lobe = (voice_ribbon_sine(spatial * 2U - phase + layer * 19000U) + 32768) / 256;
            // Closed, translucent lobes taper to the centerline at both ends.
            // Their bounded envelope needs no frame-sized scratch, blur or runtime trig.
            auto &knot = frame.nodes[layer][node];
            knot.center_q8 = static_cast<int16_t>(carrier * envelope / 32768 * (3 + energy / 48));
            knot.radius_q8 = static_cast<uint16_t>(envelope * (180 + lobe * (2 + energy / 64)) / 128);
        }
    }
}

// strip: Borrowed writable native RGB565 tile, with its declared byte order.
// first_row: Logical display row represented by the first tile row.
// frame: Immutable prepared knots shared across the complete retired frame.
// Exception: Returns surface/bounds failures; never writes outside the 466-pixel canvas.
inline esp_err_t draw_voice_ribbon_rows(CODY2D::Surface strip, uint16_t first_row,
                                      const VoiceRibbonFrame &frame)
{
    const esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    if (strip.width != 466 || uint32_t(first_row) + strip.height > 466) return ESP_ERR_INVALID_ARG;
    const int top = std::max<int>(first_row, kVoiceRibbonTop);
    const int bottom = std::min<int>(first_row + strip.height, kVoiceRibbonTop + kVoiceRibbonHeight);
    if (top >= bottom) return ESP_OK;
    constexpr uint16_t colors[] = {0x16FF, 0x4A9F, 0xF2B8, 0xAB1F};
    constexpr uint8_t opacities[] = {170, 155, 180, 145};
    for (int x = 0; x < kVoiceRibbonWidth; ++x) {
        const unsigned node = unsigned(x) / 8;
        const int fraction = x % 8;
        for (unsigned layer = 0; layer < kVoiceRibbonLayers; ++layer) {
            const auto &a = frame.nodes[layer][node];
            const auto &b = frame.nodes[layer][node + 1];
            const int center = (kVoiceRibbonTop + kVoiceRibbonHeight / 2) * 256 +
                a.center_q8 + (b.center_q8 - a.center_q8) * fraction / 8;
            const int radius = a.radius_q8 + (int(b.radius_q8) - a.radius_q8) * fraction / 8;
            if (radius == 0) continue;
            for (int y = top; y < bottom; ++y) {
                const int distance = std::abs(y * 256 + 128 - center);
                const int coverage = std::clamp(radius + 256 - distance, 0, 512);
                if (coverage == 0) continue;
                // A soft outer edge and a brighter center create volume with alpha alone.
                const uint8_t alpha = static_cast<uint8_t>(coverage * opacities[layer] / 512);
                const uint8_t light = static_cast<uint8_t>(std::clamp((radius - distance) * 70 / std::max(1, radius), 0, 70));
                const uint16_t color = CODY2D::blend_rgb565(0xFFFF, colors[layer], light);
                auto &pixel = strip.pixels[(y - first_row) * strip.stride + kVoiceRibbonLeft + x];
                const uint16_t background = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
                pixel = CODY2D::rgb565_native_to_stored(CODY2D::blend_rgb565(color, background, alpha), strip.byte_order);
            }
        }
    }
    return ESP_OK;
}
}
