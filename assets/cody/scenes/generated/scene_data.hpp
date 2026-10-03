// Generated via scripts/device.ps1 generate -Generator cody-scenes.
#pragma once
#include "CODY_scene.hpp"
namespace CODY::SceneData {
inline constexpr uint32_t boot_delay_ms = 2000U;
extern const CODY2D::MonoFont title_font;
extern const CODY2D::MonoFont body_font;
inline constexpr SceneDefinition definitions[] = {
    {
        SceneId::Greeting, "greeting", {233.000000000f, 225.000000000f, 130.000000000f},
        {0, 0},
        {{0.120000000f, 0.120000000f, 0.150000000f, 0.150000000f, 0.650000000f, 0.000000000f, 1.100000000f, 1.000000000f, 1.250000000f, 1.000000000f}}, &title_font,
        {"Hi, I'm Cody!", 13},
        348, 65535, 70, false, true, true
    },
    {
        SceneId::PairInvitation, "pair-invitation", {233.000000000f, 105.000000000f, 65.000000000f},
        {0, 0},
        {{0.120000000f, 0.120000000f, 0.150000000f, 0.150000000f, 0.650000000f, 0.000000000f, 1.100000000f, 1.000000000f, 1.250000000f, 1.000000000f}}, &title_font,
        {"Let's pair", 10},
        185, 65535, 0, false, false, true
    },
    {
        SceneId::InstallApp, "install-app", {233.000000000f, 70.000000000f, 36.000000000f},
        {0, 0},
        {{1.000000000f, 1.000000000f, 0.000000000f, 0.000000000f, 0.040000000f, 0.000000000f, 1.000000000f, 1.000000000f, 1.200000000f, 1.000000000f}}, &title_font,
        {"Install app", 11},
        105, 65535, 0, false, false, false
    },
    {
        SceneId::ScanToPair, "scan-to-pair", {233.000000000f, 70.000000000f, 36.000000000f},
        {0, 0},
        {{1.000000000f, 1.000000000f, 0.000000000f, 0.000000000f, 0.040000000f, 0.000000000f, 1.000000000f, 1.000000000f, 1.200000000f, 1.000000000f}}, &title_font,
        {"Scan to pair", 12},
        105, 65535, 0, false, false, true
    },
    {
        SceneId::Pairing, "pairing", {233.000000000f, 160.000000000f, 105.000000000f},
        {0, 0},
        {{1.000000000f, 1.000000000f, 0.000000000f, 0.000000000f, 0.040000000f, 0.000000000f, 1.000000000f, 1.000000000f, 1.200000000f, 1.000000000f}}, &title_font,
        {"Pairing...", 10},
        305, 65535, 0, false, false, true
    },
    {
        SceneId::PairingFailed, "pairing-failed", {233.000000000f, 160.000000000f, 105.000000000f},
        {0, 0},
        {{0.650000000f, 0.650000000f, 0.300000000f, 0.300000000f, 0.120000000f, 0.000000000f, 0.800000000f, 1.000000000f, 1.500000000f, 0.850000000f}}, &title_font,
        {"Try again", 9},
        305, 65535, 0, false, false, true
    },
    {
        SceneId::Online, "online", {233.000000000f, 160.000000000f, 105.000000000f},
        {0, 0},
        {{0.120000000f, 0.120000000f, 0.150000000f, 0.150000000f, 0.650000000f, 0.000000000f, 1.100000000f, 1.000000000f, 1.250000000f, 1.000000000f}}, &title_font,
        {"You're online.", 14},
        305, 65535, 0, false, false, true
    },
};
} // namespace CODY::SceneData
