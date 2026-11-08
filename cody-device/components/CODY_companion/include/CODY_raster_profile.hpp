/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <cstdint>

namespace CODY {

enum class JellyRasterKernel : uint8_t { Scalar, Simd, SimdVerify };

// Metrics have no texture dependency, including in the native vector build.
// Detailed block timing is an explicit experiment; ordinary Debug uses the fused loop.
struct JellyRasterProfile {
    int64_t (*now_us)() = nullptr;
    bool blocks_enabled = false;
    JellyRasterKernel kernel = JellyRasterKernel::Scalar;
    uint32_t verified_samples = 0;
    uint32_t vectorized_pixels = 0;
    uint32_t scalar_tail_pixels = 0;
    uint32_t zero_footprint_pixels = 0;
    uint32_t coverage_skipped_pixels = 0;
    int64_t total_us = 0;
    int64_t preparation_us = 0;
    int64_t interpolation_us = 0;
    int64_t blend_us = 0;
    uint32_t sampled_pixels = 0;
    uint32_t transparent_pixels = 0;
    uint32_t blended_pixels = 0;
    uint32_t timed_blocks = 0;
};

} // namespace CODY
