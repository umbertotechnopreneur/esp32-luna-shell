#pragma once

#include <array>
#include <cstdint>

namespace CODY {

// Stable identity and swatches belong to character appearance, not the UI palette.
// Theme zero exactly preserves the current Muse raster and state colors.
struct MuseCharacterTheme {
    const char *name;
    uint32_t fur_rgb;
    uint32_t face_rgb;
    uint32_t outline_rgb;
    uint32_t accent_rgb;
};

inline constexpr std::array<MuseCharacterTheme, 8> kMuseCharacterThemes{{
    {"Classic",  0xe6d7bd, 0xf6dfbd, 0x3a2b22, 0xf4aaa0},
    {"Lavender", 0xd9cafa, 0xf7e3e6, 0x352450, 0xc982d6},
    {"Ocean",    0x9edbe2, 0xeaf7f2, 0x173943, 0x55a0d7},
    {"Forest",   0xb6d9a8, 0xf4e7cf, 0x293d2e, 0xe69870},
    {"Sunset",   0xf0c7a5, 0xfff0dd, 0x51302e, 0xe78971},
    {"Rose",     0xeec2d3, 0xfff0e5, 0x4a2a42, 0xd475a2},
    {"Slate",    0xc9d5eb, 0xf5e8d9, 0x27364e, 0x86a8dc},
    {"Honey",    0xefcf86, 0xfff0d4, 0x493620, 0xdc9360},
}};

// id: Stable stored theme identity, independent of model or semantic pose.
// Exception: Unknown or missing identities select Classic without changing state.
inline constexpr const MuseCharacterTheme &muse_character_theme(uint8_t id)
{
    return kMuseCharacterThemes[id < kMuseCharacterThemes.size() ? id : 0];
}

} // namespace CODY
