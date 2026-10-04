#pragma once
#include "CODY_demo.hpp"
namespace CODY::RuntimeData {
inline constexpr bool autostart = true;
inline constexpr bool raster_benchmark_enabled = true;
inline constexpr bool raster_benchmark_profile_blocks = true;
inline constexpr uint16_t raster_benchmark_cloud_scale_percent = 100U;
inline constexpr auto execution_timings = true;
inline constexpr auto mirror_to_sd_when_available = true;
inline constexpr auto report_interval_ms = 2000;
inline constexpr auto frame_budget_us = 40000;
inline constexpr int ready_volume_percent = 100;
inline constexpr int eyes_open_volume_percent = 100;
inline constexpr int sample_rate_hz = 22050;
inline constexpr uint32_t cycle_ms = 29000U;
inline constexpr DemoStep steps[] = {
    {DemoPhase::Breathe, 5000U, {{1.0f, 1.0f, 0.0f, 0.0f, 0.04f, 0.0f, 0.72f, 1.0f, 1.0f, 0.58f}}, 0.12f, 0.25f},
    {DemoPhase::ContourSlow, 7000U, {{1.0f, 1.0f, 0.0f, 0.0f, 0.04f, 0.0f, 0.72f, 1.0f, 1.0f, 0.58f}}, 0.65f, 0.15f},
    {DemoPhase::ContourFast, 7000U, {{1.0f, 1.0f, 0.0f, 0.0f, 0.04f, 0.0f, 0.72f, 1.0f, 1.0f, 0.58f}}, 0.65f, 1.4f},
    {DemoPhase::Sleepy, 5000U, {{0.35f, 0.35f, 0.0f, 0.0f, 0.34f, 0.0f, 0.5f, 1.0f, 1.0f, 0.58f}}, 0.15f, 0.2f},
    {DemoPhase::Awake, 5000U, {{1.0f, 1.0f, 0.0f, 0.0f, 0.04f, 0.0f, 0.72f, 1.0f, 1.0f, 0.58f}}, 0.3f, 0.5f},
};
} // namespace CODY::RuntimeData
