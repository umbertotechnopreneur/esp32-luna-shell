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

#include <array>
#include <cstdint>
#include "CODY_benchmark.hpp"
#include "CODY_raster_profile.hpp"

namespace CODY {

enum class RasterRunPhase : uint8_t { Inactive, Warmup, Measuring, Complete, Interrupted };

struct RasterRunSummary {
    JellyRasterKernel kernel = JellyRasterKernel::Scalar;
    uint32_t frames = 0;
    uint32_t pixels_per_frame = 0;
    uint32_t missed = 0;
    uint32_t p95_us = 0;
    uint32_t maximum_us = 0;
    uint64_t total_us = 0;
    uint64_t geometry_us = 0;
    uint64_t raster_us = 0;
    uint64_t transfer_us = 0;
    uint64_t jelly_us = 0;
    uint64_t vector_us = 0;
    uint64_t preparation_us = 0;
    uint64_t interpolation_us = 0;
    uint64_t composition_us = 0;
    uint64_t sampled_pixels = 0;
    uint64_t composited_pixels = 0;
    uint64_t verified_samples = 0;
    uint64_t vectorized_pixels = 0;
    uint64_t scalar_tail_pixels = 0;
    uint64_t zero_footprint_pixels = 0;
    uint64_t coverage_skipped_pixels = 0;
};

// Firmware allocates this bounded run in PSRAM. No per-frame allocation or logging.
class RasterBenchmarkRun final {
public:
    static constexpr int64_t kWarmupUs = 5000000;
    static constexpr int64_t kMeasurementUs = 40000000;
    static constexpr uint32_t kBudgetUs = 40000;
    static constexpr uint32_t kCapacity = 2048;
    static constexpr uint32_t kStandbyOverrideSeconds = 60;

    // now_us: Monotonic time after boot/lifecycle completion and policy admission.
    // Exception: Rejects a duplicate run or nonpositive timestamp.
    esp_err_t begin(int64_t now_us);
    // now_us: Current monotonic timestamp, never a synthetic sensor sample.
    bool due(int64_t now_us) const;
    // started_us: Start of the successfully retired frame's measured pipeline.
    // completed_us: End of that pipeline before observer/log output work.
    // costs: Actual stage durations, with transfer blocked time separate from CPU work.
    // pixels: Submitted payload; an identical static workload must preserve this count.
    // jelly: Optional detailed timings, zero when detailed profiling is disabled.
    // vector_us: Vector path elapsed time in this same frame.
    // Exception: Rejects invalid measurements, changed payload, or sample-capacity overflow.
    esp_err_t record(int64_t started_us, int64_t completed_us, const FrameCosts &costs,
                     uint32_t pixels, const JellyRasterProfile &jelly, int64_t vector_us);
    // now_us: Actual completion time; early summaries are never reported as complete.
    // Exception: Rejects early completion or an empty measurement window.
    esp_err_t finish(int64_t now_us);
    // No parameters: preserve partial evidence but never label it a completed benchmark.
    void interrupt();
    // No parameters: status belongs to this explicit run, not the hardware snapshot.
    RasterRunPhase phase() const;
    // No parameters: aggregate lifetime is owned by this PSRAM-backed run.
    const RasterRunSummary &summary() const;

private:
    RasterRunPhase phase_ = RasterRunPhase::Inactive;
    int64_t started_us_ = 0;
    int64_t last_completed_us_ = 0;
    RasterRunSummary summary_{};
    std::array<uint32_t, kCapacity> frame_times_{};
};

} // namespace CODY
