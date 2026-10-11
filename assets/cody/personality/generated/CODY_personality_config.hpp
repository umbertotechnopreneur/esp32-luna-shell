// Generated from assets/cody/personality.yaml; edit that source, then regenerate.
#pragma once
#include "personality_catalog.hpp"
#include <cstdint>
namespace CODY::PersonalityConfig {
struct AnchorProfile {
    float offset_x;
    float offset_y;
    float extent_per_scale;
    float pixel_offset_x;
    float pixel_offset_y;
    float size_multiplier;
    bool allow_body_overlap;
};
inline constexpr AnchorProfile main{0.09f, -1.14f, 0.2f, 30.0f, 40.0f, 3.6f, true};
inline constexpr AnchorProfile sleep{0.09f, -1.14f, 0.2f, 30.0f, 40.0f, 3.6f, true};
inline constexpr int64_t stability_us = 300000;
inline constexpr int64_t fade_in_us = 400000;
inline constexpr int64_t minimum_visible_us = 2500000;
inline constexpr int64_t fade_out_us = 400000;
inline constexpr int64_t position_window_us = 360000;
inline constexpr int64_t scale_window_us = 400000;
inline constexpr int64_t history_bucket_us = 25000;
inline constexpr int64_t history_bucket_count = 17;
inline constexpr float maximum_position_lag_pixels = 4.0f;
inline constexpr float minimum_extent = 12.0f;
inline constexpr float maximum_base_extent = 58.0f;
inline constexpr float maximum_deformation_pixels = 2.0f;
inline constexpr float body_clearance_pixels = 4.0f;
struct CueRule {
    PersonalityCue cue;
    uint16_t priority;
    bool interrupts;
    int64_t repeat_cooldown_us;
    bool sfx_enabled;
};
inline constexpr CueRule cue_rules[] = {
    {PersonalityCue::Sleep, 1000, true, 0, true},
    {PersonalityCue::Fear, 300, true, 0, true},
    {PersonalityCue::Joy, 100, false, 15000000, true},
    {PersonalityCue::Hunger, 100, false, 15000000, true},
    {PersonalityCue::Thirst, 100, false, 15000000, true},
    {PersonalityCue::Heat, 100, false, 15000000, true},
    {PersonalityCue::Cold, 100, false, 15000000, true},
    {PersonalityCue::LowBattery, 100, false, 0, true},
    {PersonalityCue::Charging, 100, false, 20000000, true},
    {PersonalityCue::NoWifi, 100, false, 20000000, true},
    {PersonalityCue::Curiosity, 100, false, 15000000, true},
    {PersonalityCue::Confusion, 100, false, 15000000, true},
    {PersonalityCue::Affection, 200, true, 0, true},
    {PersonalityCue::Frustration, 300, true, 0, true},
};
inline constexpr uint8_t cue_rule_count = 14;
inline constexpr uint8_t low_battery_enter_percent = 20;
inline constexpr uint8_t low_battery_exit_percent = 25;
} // namespace CODY::PersonalityConfig
namespace CODY::PersonalityAudioConfig {
inline constexpr bool enabled = true;
inline constexpr int64_t cooldown_us = 0;
inline constexpr int64_t request_expiry_us = 400000;
} // namespace CODY::PersonalityAudioConfig
