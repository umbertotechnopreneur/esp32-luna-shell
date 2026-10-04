#pragma once

#include <cstdint>

namespace CODY::PersonalityConfig {

struct AnchorProfile {
    // Icon center offsets from the evaluated Nose anchor in rig-local units.
    float offset_x;
    float offset_y;
    // Base dimensions use nominal size, so breathing cannot pump the sprite.
    float extent_per_scale;
};

// Main and Sleep deliberately share a center: expression transitions stay continuous.
inline constexpr AnchorProfile main{.09f, -1.14f, .20f};
inline constexpr AnchorProfile sleep{.09f, -1.14f, .20f};
inline constexpr int64_t stability_us = 300000;
inline constexpr int64_t fade_in_us = 400000;
inline constexpr int64_t minimum_visible_us = 2500000;
inline constexpr int64_t fade_out_us = 600000;
inline constexpr int64_t position_window_us = 360000;
inline constexpr int64_t scale_window_us = 400000;
inline constexpr int64_t history_bucket_us = 25000;
inline constexpr int64_t history_bucket_count = 17;
inline constexpr float maximum_position_lag_pixels = 4.f;
inline constexpr float minimum_extent = 12.f;
inline constexpr float maximum_base_extent = 58.f;
inline constexpr float maximum_deformation_pixels = 2.f;
inline constexpr float body_clearance_pixels = 4.f;

} // namespace CODY::PersonalityConfig
