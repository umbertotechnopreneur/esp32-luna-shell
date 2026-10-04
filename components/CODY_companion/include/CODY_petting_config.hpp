#pragma once

#include <cstdint>

namespace CODY::PettingConfig {
inline constexpr int64_t minimum_contact_us = 500000;
inline constexpr int64_t maximum_contact_us = 3000000;
inline constexpr int64_t maximum_sample_gap_us = 250000;
inline constexpr int64_t cooldown_us = 6000000;
inline constexpr int64_t affection_us = 4000000;
inline constexpr float upper_margin_per_scale = .10f;
inline constexpr float lower_margin_per_scale = .08f;
inline constexpr float minimum_path_per_scale = .16f;
inline constexpr float minimum_displacement_per_scale = .08f;
inline constexpr float maximum_speed_per_scale = 2.f;
inline constexpr float jitter_pixels = 1.5f;
} // namespace CODY::PettingConfig
