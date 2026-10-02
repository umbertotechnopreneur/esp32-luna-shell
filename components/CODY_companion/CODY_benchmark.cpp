#include "CODY_benchmark.hpp"
#include <algorithm>
#include <cmath>

namespace CODY {
// mode: Explicit overlay identity; invalid identities never enter a window.
// costs: Completed-frame measurements, retaining actual elapsed microseconds.
esp_err_t FrameBenchmark::record(OverlayMode mode, const FrameCosts &costs)
{
    if (mode != OverlayMode::Artwork && mode != OverlayMode::Skeleton) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint64_t stages = static_cast<uint64_t>(costs.geometry_us) +
                            costs.raster_us + costs.transfer_us;
    if (costs.total_us == 0 || stages > costs.total_us) {
        return ESP_ERR_INVALID_ARG;
    }
    Window &window = mode == OverlayMode::Skeleton ? skeleton_ : artwork_;
    window.frames[window.next] = costs;
    window.next = (window.next + 1U) % kCapacity;
    if (window.count < kCapacity) {
        ++window.count;
    }
    return ESP_OK;
}

// mode: One independently retained overlay mode.
// budget_us: Requested deadline, distinct from a measured refresh rate.
// output: Exact rolling statistics; no observations are invented for an empty window.
esp_err_t FrameBenchmark::summarize(OverlayMode mode, uint32_t budget_us,
                                   FrameDistribution &output) const
{
    if ((mode != OverlayMode::Artwork && mode != OverlayMode::Skeleton) || budget_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const Window &window = mode == OverlayMode::Skeleton ? skeleton_ : artwork_;
    if (window.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    std::array<uint32_t, kCapacity> sorted{};
    uint64_t geometry = 0;
    uint64_t raster = 0;
    uint64_t transfer = 0;
    uint64_t total = 0;
    double mean = 0;
    double squared_deviation = 0;
    FrameDistribution result{};
    result.count = window.count;
    for (uint32_t index = 0; index < window.count; ++index) {
        const FrameCosts &frame = window.frames[index];
        sorted[index] = frame.total_us;
        geometry += frame.geometry_us;
        raster += frame.raster_us;
        transfer += frame.transfer_us;
        total += frame.total_us;
        // Welford's recurrence avoids subtracting two large squared sums.
        const double delta = frame.total_us - mean;
        mean += delta / (index + 1U);
        squared_deviation += delta * (frame.total_us - mean);
        if (frame.total_us > budget_us) {
            ++result.missed_deadlines;
        }
    }
    std::sort(sorted.begin(), sorted.begin() + window.count);
    // Nearest-rank percentiles describe the real retained samples exactly.
    result.median_us = sorted[(window.count - 1U) / 2U];
    result.p95_us = sorted[(95U * window.count + 99U) / 100U - 1U];
    result.maximum_us = sorted[window.count - 1U];
    result.standard_deviation_us = std::sqrt(squared_deviation / window.count);
    result.mean = {static_cast<uint32_t>(geometry / window.count),
                   static_cast<uint32_t>(raster / window.count),
                   static_cast<uint32_t>(transfer / window.count),
                   static_cast<uint32_t>(total / window.count)};
    output = result;
    return ESP_OK;
}

// Reset explicitly after entry/resume, so standby time never contaminates this window.
void FrameBenchmark::reset()
{
    artwork_.count = 0;
    artwork_.next = 0;
    skeleton_.count = 0;
    skeleton_.next = 0;
}
} // namespace CODY
