// Generated from assets/cody/navigation.json; do not edit.
#pragma once
#include "CODY_rig.hpp"
namespace CODY::NavigationData {
inline constexpr uint32_t auto_hide_ms = 5000U;
inline constexpr uint32_t fade_ms = 330U;
inline constexpr uint16_t top = 0x18C3;
inline constexpr uint16_t bottom = 0x0841;
inline constexpr uint16_t ink = 0xAD55;
inline constexpr uint16_t selected = 0xFFFF;
inline constexpr uint16_t shades[] = {0x18C3, 0x18C3, 0x18C3, 0x18C3, 0x18A3, 0x18A3, 0x18A3, 0x18A3, 0x10A2, 0x10A2, 0x10A2, 0x10A2, 0x1082, 0x1082, 0x1082, 0x1082, 0x1082, 0x1082, 0x1082, 0x1082, 0x1062, 0x1062, 0x1062, 0x1062, 0x0861, 0x0861, 0x0861, 0x0861, 0x0841, 0x0841, 0x0841, 0x0841};
inline constexpr RigControlGeometry controls[] = {
    {RigControl::Back, false, {40.000000000f, 233.000000000f}, {40.000000000f, 233.000000000f}, 0.000000000f, 30.000000000f, 0.000000000f, 0.000000000f, 0, {10, 203, 70, 263}},
    {RigControl::Pause, true, {158.000000000f, 47.000000000f}, {158.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {136, 25, 180, 69}},
    {RigControl::Screenshot, true, {233.000000000f, 47.000000000f}, {233.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {211, 25, 255, 69}},
    {RigControl::Overlay, true, {308.000000000f, 47.000000000f}, {308.000000000f, 47.000000000f}, 0.000000000f, 22.000000000f, 0.000000000f, 0.000000000f, 0, {286, 25, 330, 69}},
};
} // namespace CODY::NavigationData
