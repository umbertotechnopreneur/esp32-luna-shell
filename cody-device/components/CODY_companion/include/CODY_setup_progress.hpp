#pragma once

#include <algorithm>
#include <cmath>

#include "CODY2D_controls.hpp"
#include "CODY_damage.hpp"
#include "CODY_theme_generated.hpp"

namespace CODY {

inline constexpr uint8_t kSetupProgressSteps = 64;
inline constexpr uint8_t kSetupProgressArcSteps = 8;
inline constexpr int64_t kSetupProgressFrameUs = 125000;
inline constexpr float kSetupProgressRadius = 230.5f;
inline constexpr float kSetupProgressTau = 6.28318530718f;

// phase: Current zero-based animation step.
// Returns a conservative display rectangle surrounding the moving segment.
inline DamageRect setup_progress_damage(uint8_t phase)
{
    int left = 466, top = 466, right = 0, bottom = 0;
    for (int offset = -1; offset <= kSetupProgressArcSteps; ++offset) {
        const float angle = kSetupProgressTau * ((phase + kSetupProgressSteps - offset) % kSetupProgressSteps) /
            kSetupProgressSteps;
        const int x = static_cast<int>(233.f + std::sin(angle) * kSetupProgressRadius);
        const int y = static_cast<int>(233.f - std::cos(angle) * kSetupProgressRadius);
        left = std::min(left, x - 6);
        top = std::min(top, y - 6);
        right = std::max(right, x + 7);
        bottom = std::max(bottom, y + 7);
    }
    return {static_cast<uint16_t>(std::clamp(left, 0, 466)), static_cast<uint16_t>(std::clamp(top, 0, 466)),
        static_cast<uint16_t>(std::clamp(right, 0, 466)), static_cast<uint16_t>(std::clamp(bottom, 0, 466))};
}

// phase: Current zero-based animation step.
// opacity: Whole-band visibility, including optional QR chrome fade.
// strip: Borrowed tile with native logical origin and byte order.
// first_row: Logical top of the tile.
// first_column: Logical left edge of the tile.
// progress: True draws the animated setup band; false draws the temporary QR border.
// Exception: Returns the original invalid-surface error or rejects tiles outside the display.
inline esp_err_t draw_setup_progress_rows(uint8_t phase, uint8_t opacity, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column, bool progress = true)
{
    if (phase >= kSetupProgressSteps || first_row + strip.height > 466U ||
        first_column + strip.width > 466U) return ESP_ERR_INVALID_ARG;
    const auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK || opacity == 0) return error;
    for (int y = first_row; y < first_row + strip.height; ++y) {
        const float dy = y + .5f - 233.f;
        const float outer_squared = 232.f * 232.f - dy * dy;
        if (outer_squared <= 0.f) continue;
        const float inner_squared = 229.f * 229.f - dy * dy;
        const float outer = std::sqrt(outer_squared);
        const float inner = inner_squared > 0.f ? std::sqrt(inner_squared) : 0.f;
        const int left = std::max<int>(first_column, static_cast<int>(std::ceil(232.5f - outer)));
        const int right = std::min<int>(first_column + strip.width, static_cast<int>(std::floor(232.5f + outer)) + 1);
        const int skip_left = static_cast<int>(std::floor(232.5f - inner)) + 1;
        const int skip_right = static_cast<int>(std::ceil(232.5f + inner));
        for (int x = left; x < right; ++x) {
            if (x >= skip_left && x < skip_right) { x = skip_right - 1; continue; }
            const float dx = x + .5f - 233.f;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float coverage = std::min(radius - 229.f, 232.f - radius);
            if (coverage <= 0.f) continue;
            float angle = std::atan2(dx, -dy);
            if (angle < 0.f) angle += kSetupProgressTau;
            const auto cell = static_cast<uint8_t>(angle * kSetupProgressSteps / kSetupProgressTau);
            const auto offset = static_cast<uint8_t>((phase + kSetupProgressSteps - cell) % kSetupProgressSteps);
            const auto foreground = !progress ? Theme::chrome_qr_border :
                offset < kSetupProgressArcSteps ? Theme::chrome_setup_progress_highlight : Theme::chrome_setup_progress_track;
            auto &pixel = strip.pixels[(y - first_row) * strip.stride + x - first_column];
            const auto background = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            const auto alpha = static_cast<uint8_t>(std::min(coverage, 1.f) * opacity);
            pixel = CODY2D::rgb565_native_to_stored(CODY2D::blend_rgb565(foreground, background, alpha), strip.byte_order);
        }
    }
    return ESP_OK;
}

} // namespace CODY
