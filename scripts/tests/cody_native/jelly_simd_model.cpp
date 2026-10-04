#include "CODY_jelly_simd.hpp"
#include <cstdio>
#include <cstdlib>

// This is a host-only ABI/arithmetic model, not an emulation or proof of S3 assembly.
namespace CODY {
bool corrupt_next_simd_model_output = false;

// pairs: Actual firmware packer's aligned texel/weight records.
// output: Aligned firmware scratch receiving exactly two pixels per pair.
// pair_count: Full pairs; zero must not access any pointer.
// constants: Actual firmware row constants; all channels have independently checked weights.
// Exception: Aborts this explicit host check if an internal kernel precondition is violated.
extern "C" void cody_jelly_bilinear_s3(const JellySimdPair *pairs, uint32_t *output,
                                      uint32_t pair_count, const JellySimdConstants *constants)
{
    if (pair_count == 0) {
        return;
    }
    if (pairs == nullptr || output == nullptr || constants == nullptr ||
        (reinterpret_cast<uintptr_t>(pairs) & 15U) != 0 ||
        (reinterpret_cast<uintptr_t>(output) & 15U) != 0 ||
        (reinterpret_cast<uintptr_t>(constants) & 15U) != 0) {
        std::fprintf(stderr, "SIMD model: invalid pointer/alignment\n");
        std::abort();
    }
    for (uint32_t pair_index = 0; pair_index < pair_count; ++pair_index) {
        const auto &pair = pairs[pair_index];
        for (uint32_t pixel = 0; pixel < 2; ++pixel) {
            uint32_t rgba = 0;
            for (uint32_t channel = 0; channel < 4; ++channel) {
                const uint32_t lane = pixel * 4 + channel;
                const uint32_t x = pair.horizontal[lane];
                const uint32_t y = constants->vertical[lane];
                if (x > 255 || y > 255 || constants->rounding[lane] != 128) {
                    std::fprintf(stderr, "SIMD model: invalid weight/rounding lane\n");
                    std::abort();
                }
                const uint32_t shift = channel * 8;
                const uint32_t above = (((pair.first[pixel] >> shift) & 255U) * (256U - x) +
                    ((pair.second[pixel] >> shift) & 255U) * x + 128U) / 256U;
                const uint32_t below = (((pair.first[pixel + 2] >> shift) & 255U) * (256U - x) +
                    ((pair.second[pixel + 2] >> shift) & 255U) * x + 128U) / 256U;
                rgba |= ((above * (256U - y) + below * y + 128U) / 256U) << shift;
            }
            output[pair_index * 2 + pixel] = rgba;
        }
    }
    if (corrupt_next_simd_model_output) {
        output[0] ^= 1U;
        corrupt_next_simd_model_output = false;
    }
}
} // namespace CODY
