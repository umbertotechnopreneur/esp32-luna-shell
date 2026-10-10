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

#include "CODY_jelly_simd.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_simd_verify";
constexpr uint32_t kGuard = 0x5aa59669U;

// first: RGBA8 upper/lower texels for one output pixel in the packed pair ABI.
// second: Right neighbours corresponding to first.
// pixel: Pixel offset 0 or 1 within the pair.
// horizontal: Q8 horizontal weight.
// vertical: Q8 vertical weight.
uint32_t reference_sample(const uint32_t *first, const uint32_t *second,
                          uint32_t pixel, uint32_t horizontal, uint32_t vertical)
{
    uint32_t result = 0;
    // Independent per-channel arithmetic; do not share the packed C lerp or SIMD helpers.
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const uint32_t above = ((((first[pixel] >> shift) & 255U) * (256U - horizontal) +
            ((second[pixel] >> shift) & 255U) * horizontal + 128U) >> 8U);
        const uint32_t below = ((((first[pixel + 2] >> shift) & 255U) * (256U - horizontal) +
            ((second[pixel + 2] >> shift) & 255U) * horizontal + 128U) >> 8U);
        result |= ((above * (256U - vertical) + below * vertical + 128U) >> 8U) << shift;
    }
    return result;
}
}

// No parameters: run only when simd_verify was explicitly selected, before timing begins.
// Exception: Returns the original parity failure; never certifies partial coverage.
esp_err_t verify_jelly_simd_kernel()
{
    alignas(16) JellySimdPair pairs[2] = {
        {{0xffffffffU, 0x01010101U, 0x80402010U, 0xff0080ffU},
         {0x00000000U, 0xfe7ffd01U, 0xff01027fU, 0x7f102030U}, {}},
        {{0x00000000U, 0xff030201U, 0xff807f01U, 0x7f7f7f7fU},
         {0xffffffffU, 0x80402010U, 0x01000001U, 0xff0102feU}, {}}
    };
    struct alignas(16) GuardedOutput {
        std::array<uint32_t, 4> before;
        std::array<uint32_t, 4> pixels;
        std::array<uint32_t, 4> after;
    } output;
    static_assert(offsetof(GuardedOutput, pixels) == 16);
    output.before.fill(kGuard);
    output.pixels.fill(kGuard);
    output.after.fill(kGuard);
    JellySimdConstants constants;
    std::fill_n(constants.vertical, 8, 0);
    const auto untouched = output.pixels;
    cody_jelly_bilinear_s3(nullptr, nullptr, 0, nullptr);
    cody_jelly_bilinear_s3(pairs, output.pixels.data(), 0, &constants);
    if (output.pixels != untouched) {
        ESP_LOGE(kTag, "Zero-length kernel changed output (%s)", esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (uint32_t vertical = 0; vertical < 256; ++vertical) {
        std::fill_n(constants.vertical, 8, static_cast<uint16_t>(vertical));
        for (uint32_t horizontal = 0; horizontal < 256; ++horizontal) {
            const uint16_t weights[4] = {static_cast<uint16_t>(horizontal),
                static_cast<uint16_t>(255U - horizontal),
                static_cast<uint16_t>((horizontal + 73U) & 255U),
                static_cast<uint16_t>((horizontal + 191U) & 255U)};
            for (uint32_t pixel = 0; pixel < 4; ++pixel) {
                std::fill_n(pairs[pixel / 2].horizontal + (pixel % 2) * 4, 4, weights[pixel]);
            }
            const std::array<JellySimdPair, 2> input_before{pairs[0], pairs[1]};
            const JellySimdConstants constants_before = constants;
            cody_jelly_bilinear_s3(pairs, output.pixels.data(), 2, &constants);
            for (uint32_t pixel = 0; pixel < 4; ++pixel) {
                const auto &pair = input_before[pixel / 2];
                const uint32_t expected = reference_sample(pair.first, pair.second, pixel % 2,
                                                            weights[pixel], vertical);
                if (output.pixels[pixel] != expected || output.before[pixel] != kGuard ||
                    output.after[pixel] != kGuard) {
                    ESP_LOGE(kTag, "Kernel/guard mismatch pixel=%lu h=%u v=%lu expected=%08lx actual=%08lx (%s)",
                        static_cast<unsigned long>(pixel), static_cast<unsigned>(weights[pixel]),
                        static_cast<unsigned long>(vertical), static_cast<unsigned long>(expected),
                        static_cast<unsigned long>(output.pixels[pixel]), esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                    return ESP_ERR_INVALID_RESPONSE;
                }
            }
            if (std::memcmp(pairs, input_before.data(), sizeof(pairs)) != 0 ||
                std::memcmp(&constants, &constants_before, sizeof(constants)) != 0) {
                ESP_LOGE(kTag, "Kernel changed immutable inputs (%s)", esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    ESP_LOGI(kTag, "Kernel contract passed: 65536 weight pairs, 262144 RGBA samples, bounds and input guards");
    return ESP_OK;
}
} // namespace CODY
