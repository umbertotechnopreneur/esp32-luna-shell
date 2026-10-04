#include "CODY_raster_benchmark.hpp"
// This target deliberately excludes Pearl: shared profiling types must still compile.
#include "CODY_rig.hpp"
#include <cstdio>
#include <limits>

namespace {
int failures = 0;
constexpr int64_t kEntry = 1000;
constexpr int64_t kMeasureStart = kEntry + CODY::RasterBenchmarkRun::kWarmupUs;
constexpr int64_t kEnd = kMeasureStart + CODY::RasterBenchmarkRun::kMeasurementUs;

// condition: Externally observable result, independent of compiler assertion settings.
// message: Contract description emitted on failure.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

// run: The real portable firmware session under test.
// start: Synthetic host-only clock; never published into a firmware snapshot.
// duration: Exact retired frame duration.
// pixels: Actual workload identity used by the session contract.
esp_err_t record(CODY::RasterBenchmarkRun &run, int64_t start, uint32_t duration,
                 uint32_t pixels = 500)
{
    return run.record(start, start + duration, {100, 500, 100, duration}, pixels, {}, 0);
}

// No parameters: boundaries must exclude warmup, straddling frames and incomplete runs.
void check_window()
{
    CODY::RasterBenchmarkRun run;
    require(run.begin(kEntry) == ESP_OK, "begin finite run");
    require(run.begin(kEntry) == ESP_ERR_INVALID_STATE, "reject duplicate begin");
    require(record(run, kEntry, 1000) == ESP_OK, "accept warmup frame without measuring it");
    require(record(run, kMeasureStart - 500, 1000) == ESP_OK, "exclude warmup boundary crossing");
    require(run.summary().frames == 0, "warmup contributes no samples");
    require(record(run, kMeasureStart + 1000, 40000) == ESP_OK, "measure frame after warmup");
    require(run.phase() == CODY::RasterRunPhase::Measuring, "explicit measuring state");
    require(run.summary().missed == 0, "40 ms exactly meets 25 FPS budget");
    require(record(run, kEnd - 1000, 1000) == ESP_OK, "end boundary is inclusive");
    require(record(run, kEnd, 1000) == ESP_OK, "exclude frame beyond the finite window");
    require(run.summary().frames == 2, "only fully contained frames count");
    require(!run.due(kEnd - 1) && run.due(kEnd), "precise end deadline");
    require(run.finish(kEnd - 1) == ESP_ERR_INVALID_STATE, "reject early completion");
    require(run.finish(kEnd) == ESP_OK, "complete nonempty finite run");
    require(run.summary().p95_us == 40000, "nearest-rank p95 for two samples");
    require(run.finish(kEnd) == ESP_ERR_INVALID_STATE, "completion cannot be replayed");
}

// No parameters: early slow frames must remain in a full-run p95 beyond 128 samples.
void check_full_distribution()
{
    CODY::RasterBenchmarkRun run;
    require(run.begin(kEntry) == ESP_OK, "begin full-distribution run");
    for (uint32_t index = 0; index < 200; ++index) {
        const uint32_t duration = index < 20 ? 50001 : 1000;
        require(record(run, kMeasureStart + index * 100000LL, duration) == ESP_OK,
                "record all 200 frames without dropping old samples");
    }
    require(run.finish(kEnd) == ESP_OK, "finish full distribution");
    const auto &summary = run.summary();
    require(summary.frames == 200 && summary.missed == 20, "all-run counts and misses");
    require(summary.p95_us == 50001 && summary.maximum_us == 50001,
            "full p95 retains early stalls beyond the rolling window");
    require(summary.total_us == 1180020 && summary.raster_us == 100000,
            "stage and total aggregation use all measured frames");
}

// No parameters: reject changed work, invalid time and empty/interrupted completion.
void check_rejection()
{
    CODY::RasterBenchmarkRun invalid;
    require(invalid.begin(0) == ESP_ERR_INVALID_STATE, "reject unavailable entry timestamp");
    require(invalid.begin(std::numeric_limits<int64_t>::max()) == ESP_ERR_INVALID_STATE,
            "reject deadline overflow");
    require(invalid.begin(kEntry) == ESP_OK, "rejected entry preserves inactive state");
    require(record(invalid, kMeasureStart, 1000, 0) == ESP_ERR_INVALID_STATE,
            "empty raster cannot establish a workload");
    require(record(invalid, kMeasureStart + 2000, 1000) == ESP_OK, "establish fixed payload");
    require(record(invalid, kMeasureStart + 4000, 1000, 499) == ESP_ERR_INVALID_STATE,
            "reject changed payload");
    require(record(invalid, kMeasureStart + 3000, 1000) == ESP_ERR_INVALID_ARG,
            "reject backwards/overlapping frame clock");
    require(invalid.record(kMeasureStart + 6000, kMeasureStart + 7000,
                          {700, 500, 100, 1000}, 500, {}, 0) == ESP_ERR_INVALID_ARG,
            "reject stages exceeding measured total");
    invalid.interrupt();
    require(invalid.phase() == CODY::RasterRunPhase::Interrupted &&
            invalid.summary().frames == 1 && invalid.summary().p95_us == 0,
            "interruption retains partial count without a completion percentile");
    require(!invalid.due(kEnd) && invalid.finish(kEnd) == ESP_ERR_INVALID_STATE,
            "interrupted run cannot become a completed baseline");
    CODY::RasterBenchmarkRun empty;
    require(empty.begin(kEntry) == ESP_OK && empty.finish(kEnd) == ESP_ERR_INVALID_STATE,
            "empty window cannot complete");
}

// No parameters: one timed population must never mix scalar, SIMD and verification frames.
void check_kernel_identity()
{
    CODY::RasterBenchmarkRun run;
    require(run.begin(kEntry) == ESP_OK, "begin SIMD identity run");
    CODY::JellyRasterProfile profile;
    profile.kernel = CODY::JellyRasterKernel::Simd;
    profile.vectorized_pixels = 480;
    profile.scalar_tail_pixels = 1;
    profile.zero_footprint_pixels = 19;
    require(run.record(kMeasureStart, kMeasureStart + 1000, {100, 500, 100, 1000},
                       500, profile, 0) == ESP_OK, "establish SIMD population");
    profile.kernel = CODY::JellyRasterKernel::Scalar;
    require(run.record(kMeasureStart + 2000, kMeasureStart + 3000, {100, 500, 100, 1000},
                       500, profile, 0) == ESP_ERR_INVALID_STATE, "reject scalar frame in SIMD run");
    profile.kernel = CODY::JellyRasterKernel::SimdVerify;
    require(run.record(kMeasureStart + 4000, kMeasureStart + 5000, {100, 500, 100, 1000},
                       500, profile, 0) == ESP_ERR_INVALID_STATE, "reject verification frame in timing run");
    const auto &summary = run.summary();
    require(summary.frames == 1 && summary.kernel == CODY::JellyRasterKernel::Simd &&
            summary.vectorized_pixels == 480 && summary.scalar_tail_pixels == 1 &&
            summary.zero_footprint_pixels == 19 && summary.verified_samples == 0,
            "rejected backend changes cannot contaminate counters or identity");
}

// No parameters: bounded storage must fail visibly instead of silently truncating p95.
void check_capacity()
{
    CODY::RasterBenchmarkRun run;
    require(run.begin(kEntry) == ESP_OK, "begin bounded run");
    for (uint32_t index = 0; index < CODY::RasterBenchmarkRun::kCapacity; ++index) {
        require(record(run, kMeasureStart + index * 2000LL, 1000) == ESP_OK,
                "accept each bounded slot");
    }
    require(record(run, kMeasureStart + CODY::RasterBenchmarkRun::kCapacity * 2000LL, 1000) ==
            ESP_ERR_NO_MEM, "capacity exhaustion is explicit");
    require(run.summary().frames == CODY::RasterBenchmarkRun::kCapacity,
            "overflow does not modify sample count");
}
}

// No parameters: host-only assertions; exit status preserves any failed contract.
int main()
{
    CODY::RigRasterProfile non_pearl_profile{};
    require(!non_pearl_profile.jelly.blocks_enabled, "non-Pearl shared profile defaults to no detail");
    check_window();
    check_full_distribution();
    check_rejection();
    check_kernel_identity();
    check_capacity();
    return failures == 0 ? 0 : 1;
}
