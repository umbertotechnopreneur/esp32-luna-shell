// Generated from assets/cody/navigation.json; do not edit.
#pragma once
#include <array>
#include "CODY_rig.hpp"
#include "CODY_theme_generated.hpp"
namespace CODY::NavigationData {
inline constexpr uint32_t auto_hide_ms = 5000U;
inline constexpr uint32_t fade_ms = 330U;
inline constexpr uint16_t top = Theme::navigation_top;
inline constexpr uint16_t bottom = Theme::navigation_bottom;
inline constexpr uint16_t ink = Theme::navigation_ink;
inline constexpr uint16_t selected = Theme::navigation_selected;
// No parameters: derive exactly 32 RGB565 gradient steps from the shared theme.
constexpr std::array<uint16_t, 32> make_shades() {
    std::array<uint16_t, 32> shades{};
    for (unsigned step = 0; step < shades.size(); ++step) {
        uint16_t value = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned shift = channel == 0 ? 11U : channel == 1 ? 5U : 0U;
            const unsigned mask = channel == 1 ? 63U : 31U;
            const unsigned start = (top >> shift) & mask, end = (bottom >> shift) & mask;
            value |= static_cast<uint16_t>(((start * (31 - step) + end * step + 15) / 31) << shift);
        }
        shades[step] = value;
    }
    return shades;
}
inline constexpr auto shades = make_shades();
inline constexpr RigControlGeometry controls[] = {
    {RigControl::Back, false, {40.000000000f, 233.000000000f}, {40.000000000f, 233.000000000f}, 0.000000000f, 30.000000000f, 0.000000000f, 0.000000000f, 0, {10, 203, 70, 263}},
    {RigControl::Pause, false, {158.000000000f, 47.000000000f}, {158.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {136, 25, 180, 69}},
    {RigControl::Screenshot, false, {233.000000000f, 47.000000000f}, {233.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {211, 25, 255, 69}},
    {RigControl::Overlay, false, {308.000000000f, 47.000000000f}, {308.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {286, 25, 330, 69}},
};
} // namespace CODY::NavigationData
