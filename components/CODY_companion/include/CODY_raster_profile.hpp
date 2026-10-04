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
