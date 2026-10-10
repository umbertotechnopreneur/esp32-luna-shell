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

#include "CODY_raster_benchmark.hpp"

#include <algorithm>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_raster_bench";
}

// now_us: Monotonic entry time after the power owner admits the finite override.
// Exception: Rejects duplicate entry and invalid time without changing the run.
esp_err_t RasterBenchmarkRun::begin(int64_t now_us)
{
    if (phase_ != RasterRunPhase::Inactive || now_us <= 0 ||
        now_us > INT64_MAX - kWarmupUs - kMeasurementUs) {
        ESP_LOGE(kTag, "Begin rejected: phase=%u now=%lld (%s)",
                 static_cast<unsigned>(phase_), static_cast<long long>(now_us),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    started_us_ = now_us;
    phase_ = RasterRunPhase::Warmup;
    return ESP_OK;
}

// now_us: Monotonic owner time; an interrupted run cannot become complete.
bool RasterBenchmarkRun::due(int64_t now_us) const
{
    return (phase_ == RasterRunPhase::Warmup || phase_ == RasterRunPhase::Measuring) &&
        now_us >= started_us_ + kWarmupUs + kMeasurementUs;
}

// started_us: Measured start of one successfully retired frame.
// completed_us: Measured end before statistics and log output.
// costs: Nonoverlapping elapsed stages and complete pipeline duration.
// pixels: Fixed payload count for the forced static-artwork redraw.
// jelly: Backend identity, coarse counters and optional detailed block timings.
// vector_us: Time spent on vector artwork in the same frame.
// Exception: Rejects invalid intervals, changing payload/backend, or bounded storage exhaustion.
esp_err_t RasterBenchmarkRun::record(int64_t started_us, int64_t completed_us,
                                     const FrameCosts &costs, uint32_t pixels,
                                     const JellyRasterProfile &jelly, int64_t vector_us)
{
    const uint64_t stages = static_cast<uint64_t>(costs.geometry_us) + costs.raster_us + costs.transfer_us;
    if ((phase_ != RasterRunPhase::Warmup && phase_ != RasterRunPhase::Measuring) ||
        started_us < started_us_ || started_us < last_completed_us_ || completed_us <= started_us ||
        costs.total_us != static_cast<uint64_t>(completed_us - started_us) || stages > costs.total_us ||
        vector_us < 0 || jelly.total_us < 0 || jelly.interpolation_us < 0 || jelly.blend_us < 0) {
        ESP_LOGE(kTag, "Frame measurement rejected: phase=%u start=%lld end=%lld total=%lu (%s)",
                 static_cast<unsigned>(phase_), static_cast<long long>(started_us),
                 static_cast<long long>(completed_us), static_cast<unsigned long>(costs.total_us),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    last_completed_us_ = completed_us;
    // A frame straddling either boundary belongs to neither sample population.
    if (started_us < started_us_ + kWarmupUs ||
        completed_us > started_us_ + kWarmupUs + kMeasurementUs) {
        return ESP_OK;
    }
    if (pixels == 0 || (summary_.frames != 0 &&
        (pixels != summary_.pixels_per_frame || jelly.kernel != summary_.kernel))) {
        ESP_LOGE(kTag, "Workload changed: pixels=%lu expected=%lu kernel=%u expected=%u (%s)",
                 static_cast<unsigned long>(pixels), static_cast<unsigned long>(summary_.pixels_per_frame),
                 static_cast<unsigned>(jelly.kernel), static_cast<unsigned>(summary_.kernel),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (summary_.frames == kCapacity) {
        ESP_LOGE(kTag, "Frame capacity exceeded: %lu (%s)", static_cast<unsigned long>(kCapacity),
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    phase_ = RasterRunPhase::Measuring;
    summary_.kernel = jelly.kernel;
    frame_times_[summary_.frames++] = costs.total_us;
    summary_.pixels_per_frame = pixels;
    summary_.missed += costs.total_us > kBudgetUs ? 1U : 0U;
    summary_.maximum_us = std::max(summary_.maximum_us, costs.total_us);
    summary_.total_us += costs.total_us;
    summary_.geometry_us += costs.geometry_us;
    summary_.raster_us += costs.raster_us;
    summary_.transfer_us += costs.transfer_us;
    summary_.jelly_us += jelly.total_us;
    summary_.vector_us += vector_us;
    summary_.preparation_us += jelly.preparation_us;
    summary_.interpolation_us += jelly.interpolation_us;
    summary_.composition_us += jelly.blend_us;
    summary_.sampled_pixels += jelly.sampled_pixels;
    summary_.composited_pixels += jelly.blended_pixels;
    summary_.verified_samples += jelly.verified_samples;
    summary_.vectorized_pixels += jelly.vectorized_pixels;
    summary_.scalar_tail_pixels += jelly.scalar_tail_pixels;
    summary_.zero_footprint_pixels += jelly.zero_footprint_pixels;
    summary_.coverage_skipped_pixels += jelly.coverage_skipped_pixels;
    return ESP_OK;
}

// now_us: Current owner timestamp after the finite measurement window.
// Exception: Rejects early, interrupted or empty completion; keeps partial evidence.
esp_err_t RasterBenchmarkRun::finish(int64_t now_us)
{
    if (!due(now_us) || summary_.frames == 0) {
        ESP_LOGE(kTag, "Finish rejected: phase=%u frames=%lu (%s)",
                 static_cast<unsigned>(phase_), static_cast<unsigned long>(summary_.frames),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // Sort bounded PSRAM storage after the measured interval; no frame-time scratch array.
    std::sort(frame_times_.begin(), frame_times_.begin() + summary_.frames);
    summary_.p95_us = frame_times_[(95U * summary_.frames + 99U) / 100U - 1U];
    phase_ = RasterRunPhase::Complete;
    return ESP_OK;
}

// No parameters: an aborted capture keeps partial counts but has no completion percentile.
void RasterBenchmarkRun::interrupt()
{
    if (phase_ == RasterRunPhase::Warmup || phase_ == RasterRunPhase::Measuring) {
        phase_ = RasterRunPhase::Interrupted;
    }
}

// No parameters: report the explicit run lifecycle.
RasterRunPhase RasterBenchmarkRun::phase() const
{
    return phase_;
}

// No parameters: caller copies results only after finish or explicit interruption.
const RasterRunSummary &RasterBenchmarkRun::summary() const
{
    return summary_;
}

} // namespace CODY
