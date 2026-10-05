// Generated from assets/cody/personality.yaml; edit that source, then regenerate.
#pragma once
#include "personality_catalog.hpp"
#include <cstdint>
namespace CODY::PettingConfig {
inline constexpr PersonalityEvent event = PersonalityEvent::Caress;
inline constexpr int64_t minimum_contact_us = 500000;
inline constexpr int64_t maximum_contact_us = 3000000;
inline constexpr int64_t maximum_sample_gap_us = 250000;
inline constexpr int64_t cooldown_us = 6000000;
inline constexpr int64_t affection_us = 4000000;
inline constexpr float upper_margin_per_scale = 0.1f;
inline constexpr float lower_margin_per_scale = 0.08f;
inline constexpr float minimum_path_per_scale = 0.16f;
inline constexpr float minimum_displacement_per_scale = 0.08f;
inline constexpr float maximum_speed_per_scale = 2.0f;
inline constexpr float jitter_pixels = 1.5f;
} // namespace CODY::PettingConfig
