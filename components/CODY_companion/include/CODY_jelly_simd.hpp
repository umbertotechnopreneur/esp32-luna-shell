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

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

#ifndef CODY_JELLY_SIMD_AVAILABLE
#define CODY_JELLY_SIMD_AVAILABLE 0
#endif

namespace CODY {

// Each pair carries two destination pixels, retaining packed flash RGBA8 texels.
// first/second order: upper pixel 0, upper pixel 1, lower pixel 0, lower pixel 1.
// Horizontal weights repeat four times per pixel, one for each RGBA channel.
struct alignas(16) JellySimdPair {
    uint32_t first[4];
    uint32_t second[4];
    uint16_t horizontal[8];
};
static_assert(sizeof(JellySimdPair) == 48);
static_assert(alignof(JellySimdPair) == 16);
static_assert(offsetof(JellySimdPair, second) == 16);
static_assert(offsetof(JellySimdPair, horizontal) == 32);

// Row constants are reused for every block; immutable rounding lanes load once per call.
struct alignas(16) JellySimdConstants {
    uint16_t vertical[8];
    uint16_t rounding[8] = {128, 128, 128, 128, 128, 128, 128, 128};
};
static_assert(sizeof(JellySimdConstants) == 32);
static_assert(offsetof(JellySimdConstants, rounding) == 16);

// pairs: pair_count immutable 16-byte-aligned records with channel weights in [0,255].
// output: 16-byte-aligned scratch with capacity for exactly 2*pair_count RGBA8 words.
// pair_count: Nonnegative number of records; zero performs no load or store.
// constants: 16-byte-aligned row weights in [0,255] and eight rounding lanes of 128.
// Internal kernel: caller owns bounds/alignment; no panel, settings or sensor access.
extern "C" void cody_jelly_bilinear_s3(const JellySimdPair *pairs, uint32_t *output,
                                      uint32_t pair_count, const JellySimdConstants *constants);

// No parameters: explicit verification experiment, never part of a timed performance run.
// Exception: Returns ESP_ERR_INVALID_RESPONSE on an output/guard/input mutation mismatch.
esp_err_t verify_jelly_simd_kernel();

} // namespace CODY
