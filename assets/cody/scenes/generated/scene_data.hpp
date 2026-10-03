// Generated via scripts/device.ps1 generate -Generator cody-scenes.
#pragma once
#include "CODY_scene.hpp"
namespace CODY::SceneData {
inline constexpr uint32_t boot_delay_ms = 2000U;
extern const CODY2D::MonoFont title_font;
inline constexpr SceneDefinition definitions[] = {
    {
        SceneId::Greeting, "greeting", {233.000000000f, 225.000000000f, 130.000000000f},
        {{0.120000000f, 0.120000000f, 0.150000000f, 0.150000000f, 0.650000000f, 0.000000000f, 1.100000000f, 1.000000000f, 1.250000000f, 1.000000000f}}, &title_font,
        {"Hi, I'm Cody!", 13},
        348, 65535, 70, false, false
    },
};
} // namespace CODY::SceneData
