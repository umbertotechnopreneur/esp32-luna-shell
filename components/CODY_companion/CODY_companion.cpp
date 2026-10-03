#include "CODY_companion.h"

#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "CODY2D.hpp"
#include "CODY_rig.hpp"
#include "CODY_debug_hud.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_response.hpp"
#include "CODY_demo.hpp"
#include "CODY_benchmark.hpp"
#include "poc_settings.hpp"
#include "cloud_rig_data.hpp"
#include "cody_sleep_blue.hpp"
#include "cody_waking_blue.hpp"
#include "cody_ready_blue.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "bsp/display.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "CODY_companion";
constexpr uint16_t kPanelWidth = 466;
constexpr uint16_t kPanelHeight = 466;
// Raster borrows the frame owner's internal DMA slot. Its actual row count
// comes from the lease; touch polling stays bounded even on an emergency row.
constexpr uint16_t kTouchPollRows = 32;
constexpr uint16_t kPaletteLimit = 64;
constexpr uint32_t kSamplePeriodMs = 50;
constexpr uint32_t kStandbySamplePeriodMs = 200;
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
constexpr uint32_t kWorkerStackBytes = 6144;
constexpr uint32_t kRigFramePeriodMs = 33;
constexpr uint32_t kPausedReadoutPeriodMs = 1000;
// This is a failure deadline, not an FPS claim; real frame cost still needs measurement.
constexpr uint32_t kFrameBudgetMs = 500;

enum class StartState : uint32_t {
    Idle,
    Starting,
    Running,
    Failed,
};

enum class BootPhase : uint32_t {
    Sleeping,
    Waking,
    Ready,
};

struct DrawResult {
    esp_err_t error = ESP_OK;
    bool parked = false;
    uint32_t completed_revision = 0;
};

enum class TouchPollStatus : uint8_t {
    Reported,
    NoReport,
    Failed,
};

struct TouchPollResult {
    TouchPollStatus status;
    esp_err_t error;
};

// One report per polling band fits here. Preserve every press/release until the frame
// ends, so a navigation event cannot change half of the displayed image.
struct PendingTouchReports {
    LUNASHELL_touch_point_t samples[(kPanelHeight + kTouchPollRows - 1U) / kTouchPollRows]{};
    uint16_t count = 0;
};

// point: Receives a real report, retaining its original timestamp and error.
TouchPollResult read_touch_report(LUNASHELL_touch_point_t &point);
// reports: Frame-local reports to dispatch after complete frame retirement.
esp_err_t capture_pending_touch(PendingTouchReports &reports);
// reports: Acquired reports in their original order, under the display lock.
esp_err_t dispatch_pending_touch(const PendingTouchReports &reports);

std::atomic<StartState> s_start_state{StartState::Idle};
std::atomic<esp_err_t> s_terminal_error{ESP_OK};
struct CompanionResources {
    CODY::RigScene *previous_rig = nullptr;
    bool previous_frame_valid = false;
    CODY::RigScene *rig = nullptr;
    CODY::RigDebugStrokeCache *rig_debug_strokes = nullptr;
};
CompanionResources s_resources;
std::atomic<uint32_t> s_rendered_revision{0};
std::atomic<bool> s_demo_requested{false};
// This bounded collector shares the display/state lock with completed frame publication.
CODY::FrameBenchmark s_benchmark;
// Completed-frame geometry and controls share the display lock.
// They are never advanced by a failed frame or a deferred touch event.
bool s_cache_was_rig = false;
bool s_cached_rig_debug = false;
bool s_cached_rig_paused = false;
CODY::PointId s_cached_selected_point = CODY::kNoPoint;
CODY::DebugReadout s_cached_readout;
struct CompanionState {
    // After startup, non-atomic changes use the display lock. Only the worker reads its schedule outside it.
    BootPhase boot_phase = BootPhase::Sleeping;
    bool rig_visible = false;
    bool rig_debug = true;
    bool animation_paused = false;
    bool touch_pressed = false;
    CODY::PointId selected_point = CODY::kNoPoint;
    std::atomic<bool> frame_dirty{false};
    int64_t animation_elapsed_us = 0;
    int64_t animation_sampled_us = 0;
    CODY::ContourState contour{};
    CODY::Channels face{};
    CODY::ResponseState response{};
    CODY::ResponseFrame response_frame{};
    bool response_sources_logged = false;
    int64_t next_frame_us = 0;
    int64_t last_timing_log_us = 0;
    uint32_t rig_frame_count = 0;
    CODY::FrameRateMeasurement frame_rate{};
};
CompanionState s_state;

// Adopt one successfully acquired BSP lock. Failed acquisition never creates this guard.
class DisplayUnlockOnExit final {
public:
    // The caller already holds one level of the recursive display lock.
    DisplayUnlockOnExit() = default;

    // Return this call's lock level; an outer recursive lock stays owned.
    ~DisplayUnlockOnExit()
    {
        release();
    }

    // Release this level early when subsequent logging no longer needs panel ownership.
    void release()
    {
        if (active_) {
            bsp_display_unlock();
            active_ = false;
        }
    }

    // other: A lock owner cannot be copied into a second owner.
    DisplayUnlockOnExit(const DisplayUnlockOnExit &other) = delete;
    // other: A live lock owner cannot be replaced by another owner.
    DisplayUnlockOnExit &operator=(const DisplayUnlockOnExit &other) = delete;

private:
    bool active_ = true;
};

// Own only the frame acquired by this call; every return passes its original result.
class StandaloneFrameOnExit final {
public:
    // Adopt the platform frame already acquired by this call.
    StandaloneFrameOnExit() = default;

    // A missed finish is a visible failure; never leave a DMA lease or watchdog behind.
    ~StandaloneFrameOnExit()
    {
        if (active_) {
            const esp_err_t error = LUNASHELL_display_frame_end(ESP_ERR_INVALID_STATE);
            ESP_LOGE(kTag, "Unfinished render frame retired (%s)", esp_err_to_name(error));
        }
    }

    // draw: Geometry/raster/transfer result; successful completion still checks the deadline.
    DrawResult finish(DrawResult draw)
    {
        draw.error = LUNASHELL_display_frame_end(draw.error);
        active_ = false;
        if (draw.error != ESP_OK) {
            draw.completed_revision = 0;
        }
        return draw;
    }

    // other: Ownership cannot be duplicated.
    StandaloneFrameOnExit(const StandaloneFrameOnExit &other) = delete;
    // other: An acquired transaction cannot be replaced.
    StandaloneFrameOnExit &operator=(const StandaloneFrameOnExit &other) = delete;

private:
    bool active_ = true;
};

struct FrameTimings {
    struct WorkerLoopTrace {
        bool valid = false;
        uint32_t requested_wait_ms = 0;
        int64_t scheduled_us = 0;
        int64_t loop_started_us = 0;
        int64_t wait_started_us = 0;
        int64_t wait_completed_us = 0;
        int64_t draw_requested_us = 0;
    } worker;
    int64_t started_us = 0;
    int64_t frame_begin_started_us = 0;
    int64_t frame_begin_completed_us = 0;
    int64_t buffer_ready_us = 0;
    int64_t geometry_completed_us = 0;
    int64_t tiles_completed_us = 0;
    int64_t frame_end_started_us = 0;
    int64_t frame_end_completed_us = 0;
    int64_t statistics_completed_us = 0;
    int64_t state_unlocked_us = 0;
    int64_t geometry_us = 0;
    int64_t raster_us = 0;
    int64_t damage_us = 0;
    int64_t transfer_us = 0;
    uint32_t submitted_pixels = 0;
    uint32_t previous_log_output_us = 0;
    uint16_t dirty_rectangles = 0;
    uint16_t tile_rows = 0;
    uint16_t visited_tiles = 0;
    uint16_t skipped_tiles = 0;
    uint16_t rasterized_tiles = 0;
    uint16_t touch_polls = 0;
    CODY::RigRasterProfile raster_profile{esp_timer_get_time};
};

// The next sampled frame reports observer cost without logging recursively.
uint32_t s_previous_log_output_us = 0;

// operation: Consumer stage that has failed and must remain visible in the snapshot.
// error: Original initialization, decode, display, touch, or task creation failure.
// touch_failure: Whether the failed stage belongs to the mandatory touch device.
esp_err_t fail_consumer(const char *operation, esp_err_t error, bool touch_failure)
{
    esp_err_t no_previous_failure = ESP_OK;
    s_terminal_error.compare_exchange_strong(
        no_previous_failure, error, std::memory_order_acq_rel, std::memory_order_acquire
    );
    s_start_state.store(StartState::Failed, std::memory_order_release);
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    if (touch_failure) {
        snapshots.reportTouch(false, error);
    } else {
        snapshots.reportDisplay(false, error);
    }
    ESP_LOGE(kTag, "%s failed; consumer stopped: %s", operation, esp_err_to_name(error));
    return error;
}


// image: Immutable source that must cover the mandatory panel with at most 64 colors.
// operation: Asset phase named in a metadata rejection log.
esp_err_t validate_boot_asset(const CODY2D::IndexedRleImage &image, const char *operation)
{
    if (image.width != kPanelWidth || image.height != kPanelHeight ||
        image.palette_count == 0 || image.palette_count > kPaletteLimit) {
        ESP_LOGE(kTag, "%s rejected: width=%u height=%u palette=%u (%s)",
                 operation, image.width, image.height, image.palette_count,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::validate_indexed_rle_image(image);
}

// phase: Typed startup checkpoint; an unknown checkpoint is a visible failure.
const CODY2D::IndexedRleImage *boot_asset_for(BootPhase phase)
{
    switch (phase) {
    case BootPhase::Sleeping:
        return &CODY_asset::sleeping_blue;
    case BootPhase::Waking:
        return &CODY_asset::waking_blue;
    case BootPhase::Ready:
        return &CODY_asset::ready_blue;
    default:
        ESP_LOGE(kTag, "Unknown boot phase rejected: %lu (%s)",
                 static_cast<unsigned long>(phase), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return nullptr;
    }
}

// Before task creation, only startup owns these PSRAM rig resources.
void release_companion_resources()
{
    s_resources.previous_frame_valid = false;
    if (s_resources.previous_rig != nullptr) {
        s_resources.previous_rig->~RigScene();
        heap_caps_free(s_resources.previous_rig);
        s_resources.previous_rig = nullptr;
    }
    if (s_resources.rig != nullptr) {
        s_resources.rig->~RigScene();
        heap_caps_free(s_resources.rig);
        s_resources.rig = nullptr;
    }
    if (s_resources.rig_debug_strokes != nullptr) {
        s_resources.rig_debug_strokes->~RigDebugStrokeCache();
        heap_caps_free(s_resources.rig_debug_strokes);
        s_resources.rig_debug_strokes = nullptr;
    }
}

// Drain every possible boot-completion writer before releasing the rig resources.
// A failed lock leaves the buffer retained until reset because its ownership is ambiguous.
esp_err_t release_worker_resources()
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        const uint32_t retained_bytes =
            (s_resources.previous_rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig_debug_strokes != nullptr ? sizeof(CODY::RigDebugStrokeCache) : 0);
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayConsumerRetention(
            retained_bytes, lock_error
        );
        ESP_LOGE(kTag, "Companion cleanup retained PSRAM until reset: %s",
                 esp_err_to_name(lock_error));
        return lock_error;
    }
    release_companion_resources();
    bsp_display_unlock();
    return ESP_OK;
}

// A closed write gate with a healthy BSP is a power transition, not a completed frame.
bool renderer_is_parked()
{
    return !LUNASHELL_display_runtime_writes_allowed() && bsp_display_is_ready();
}

// health: Explicit state of a published source; numeric telemetry is never implied here.
const char *source_health_name(CODY::SampleHealth health)
{
    switch (health) {
    case CODY::SampleHealth::Current:
        return "CURRENT";
    case CODY::SampleHealth::Stale:
        return "STALE";
    case CODY::SampleHealth::Error:
        return "ERROR";
    default:
        return "UNAVAILABLE";
    }
}

// source: Stable stimulus identity in its typed namespace.
// previous: Last committed source status used to avoid repeated identical logs.
// current: New source status, retaining the original error and sample timestamp.
// force: Logs the first observed state even when the source starts unavailable.
void log_response_source(CODY::StimulusId source, const CODY::SampleStamp &previous,
                         const CODY::SampleStamp &current, bool force)
{
    if (!force && previous.health == current.health && previous.error == current.error) {
        return;
    }
    const char *name = nullptr;
    switch (source) {
    case CODY::StimulusId::MotionAcceleration:
        name = "motion.acceleration";
        break;
    case CODY::StimulusId::PowerCharge:
        name = "power.charge";
        break;
    case CODY::StimulusId::MicrophoneLevel:
        name = "audio.microphone";
        break;
    case CODY::StimulusId::SpeakerPlayback:
        name = "audio.playback";
        break;
    default:
        name = "invalid-stimulus";
        break;
    }
    ESP_LOGI(kTag, "Stimulus %s=%s error=%s sample_at=%lldus", name,
             source_health_name(current.health), esp_err_to_name(current.error),
             static_cast<long long>(current.sampled_at_us));
}

// frame_started_us: Real timestamp sampled while the display lock is held.
// resume_animation: Exclude standby time from both independent animation clocks.
esp_err_t evaluate_companion_geometry(int64_t frame_started_us, bool resume_animation)
{
    if (s_resources.rig == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t animation_delta_us = resume_animation || s_state.animation_sampled_us == 0
        ? 0 : frame_started_us - s_state.animation_sampled_us;
    const float animation_delta_ms = static_cast<float>(animation_delta_us) / 1000.f;
    CODY::Channels target_face{};
    CODY::ContourControls target_contour{};
    CODY::DemoPhase phase{};
    const esp_err_t demo_error = CODY::demo_targets(s_state.animation_elapsed_us + animation_delta_us,
        target_face, target_contour, phase);
    if (demo_error != ESP_OK) {
        ESP_LOGE(kTag, "Authored demo targets rejected: %s", esp_err_to_name(demo_error));
        return demo_error;
    }
    CODY::Channels next_face{};
    const esp_err_t face_error = CODY::advance_face(
        s_state.face, target_face, animation_delta_ms, next_face
    );
    if (face_error != ESP_OK) {
        return face_error;
    }
    CODY::ResponseInputs inputs{};
    const esp_err_t observation_error = CODY::observe_response_inputs(inputs);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    CODY::ResponseState previous_response = s_state.response;
    if (resume_animation) {
        CODY::rebase_response_motion(previous_response);
    }
    CODY::ResponseState next_response{};
    // A publication can occur after frame_started_us while we wait for the snapshot.
    // Freshness uses the later observation time, not a potentially earlier frame clock.
    const esp_err_t response_error = CODY::advance_response(
        CODY::Data::response, previous_response, inputs, animation_delta_ms,
        esp_timer_get_time(), true, next_response
    );
    if (response_error != ESP_OK) {
        return response_error;
    }
    CODY::ResponseFrame response_frame{};
    const esp_err_t composition_error = CODY::compose_response(
        CODY::Data::response, next_response, next_face, target_contour,
        CODY::Data::placement, inputs.rendering_rotation_degrees, true, response_frame
    );
    if (composition_error != ESP_OK) {
        return composition_error;
    }
    CODY::ContourState next_contour{};
    const esp_err_t contour_error = CODY::advance_contour(
        s_state.contour, response_frame.contour, animation_delta_ms, next_contour
    );
    if (contour_error != ESP_OK) {
        return contour_error;
    }
    const esp_err_t geometry_error = CODY::evaluate_rig(
        response_frame.face, response_frame.transform, *s_resources.rig, &next_contour
    );
    if (geometry_error != ESP_OK) {
        return geometry_error;
    }
    s_state.contour = next_contour;
    s_state.face = next_face;
    log_response_source(CODY::StimulusId::MotionAcceleration, s_state.response.motion_status,
                        next_response.motion_status, !s_state.response_sources_logged);
    log_response_source(CODY::StimulusId::PowerCharge, s_state.response.power_status,
                        next_response.power_status, !s_state.response_sources_logged);
    log_response_source(CODY::StimulusId::MicrophoneLevel, s_state.response.microphone_status,
                        next_response.microphone_status, !s_state.response_sources_logged);
    log_response_source(CODY::StimulusId::SpeakerPlayback, s_state.response.playback_status,
                        next_response.playback_status, !s_state.response_sources_logged);
    s_state.response_sources_logged = true;
    s_state.response = next_response;
    s_state.response_frame = response_frame;
    // Parked time never advances the body, breath, blink or contour clocks.
    s_state.animation_elapsed_us += animation_delta_us;
    s_state.animation_sampled_us = frame_started_us;
    return ESP_OK;
}

struct CompletedFrameLog {
    CODY::DebugReadout readout{};
    LUNASHELL::DisplayFrameSnapshot transfer{};
    bool rig = false;
    bool paused = false;
    CODY::FrameDistribution distribution{};
    uint32_t frame_count = 0;
    bool debug = false;
    int64_t completed_us = 0;
};

struct CompletedFrameReport {
    FrameTimings timings{};
    CompletedFrameLog log{};
    bool ready = false;
};

// frame_started_us: Application frame timestamp that the driver observation must follow.
// transfer: Receives the latest completed frame metrics from the canonical snapshot.
esp_err_t read_completed_transfer_profile(int64_t frame_started_us,
                                          LUNASHELL::DisplayFrameSnapshot &transfer)
{
    const esp_err_t error =
        LUNASHELL::DeviceSnapshotService::instance().readDisplayFrameObservation(transfer);
    if (error != ESP_OK) {
        return error;
    }
    if (!transfer.measured || transfer.last_error != ESP_OK ||
        transfer.completed_at_us < frame_started_us) {
        ESP_LOGE(kTag, "Completed transfer profile unavailable for this frame (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// timings: Completed frame's measured stages, never an estimated CPU metric.
// log: Receives bounded copied state only when a timing log is due.
// should_log: True only after the configured interval and a successful statistics snapshot.
esp_err_t record_completed_frame(const FrameTimings &timings, CompletedFrameLog &log, bool &should_log)
{
    should_log = false;
    s_state.frame_dirty.store(false, std::memory_order_release);
    log.completed_us = esp_timer_get_time();
    log.rig = s_state.rig_visible;
    log.debug = s_state.rig_debug;
    log.paused = s_state.animation_paused;
    if (!s_state.rig_visible) {
        should_log = CODY::PocData::execution_timings;
        return ESP_OK;
    }

    // Let late frames go. Schedule from this frame's real timestamp.
    const uint32_t period_ms = s_state.animation_paused ? kPausedReadoutPeriodMs : kRigFramePeriodMs;
    s_state.next_frame_us = timings.started_us + period_ms * 1000LL;
    ++s_state.rig_frame_count;
    log.frame_count = s_state.rig_frame_count;
    const int64_t completed_us = log.completed_us;
    CODY::record_frame_rate(s_state.frame_rate, timings.started_us, completed_us);
    if (!CODY::PocData::execution_timings) {
        return ESP_OK;
    }
    if (s_state.animation_paused) {
        // Paused HUD refreshes are not animation samples or frame-budget misses.
        should_log = s_state.last_timing_log_us == 0 || completed_us - s_state.last_timing_log_us >=
            CODY::PocData::report_interval_ms * 1000LL;
        if (should_log) {
            const esp_err_t transfer_error =
                read_completed_transfer_profile(timings.started_us, log.transfer);
            if (transfer_error != ESP_OK) {
                return transfer_error;
            }
            s_state.last_timing_log_us = completed_us;
        }
        return ESP_OK;
    }
    const CODY::OverlayMode mode = s_state.rig_debug ? CODY::OverlayMode::Skeleton : CODY::OverlayMode::Artwork;
    const CODY::FrameCosts costs{static_cast<uint32_t>(timings.geometry_us),
        static_cast<uint32_t>(timings.raster_us), static_cast<uint32_t>(timings.transfer_us),
        static_cast<uint32_t>(completed_us - timings.started_us)};
    esp_err_t error = s_benchmark.record(mode, costs);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Completed frame timing rejected: %s", esp_err_to_name(error));
        return error;
    }
    if (s_state.last_timing_log_us != 0 && completed_us - s_state.last_timing_log_us <
        CODY::PocData::report_interval_ms * 1000LL) {
        return ESP_OK;
    }
    error = s_benchmark.summarize(mode, CODY::PocData::frame_budget_us, log.distribution);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Completed frame statistics failed: %s", esp_err_to_name(error));
        return error;
    }
    error = read_completed_transfer_profile(timings.started_us, log.transfer);
    if (error != ESP_OK) {
        return error;
    }
    s_state.last_timing_log_us = completed_us;
    log.debug = s_state.rig_debug;
    log.completed_us = completed_us;
    should_log = true;
    return ESP_OK;
}

// elapsed_us: Nonnegative measured duration; round only the displayed value.
long rounded_ms(int64_t elapsed_us)
{
    return static_cast<long>((elapsed_us + 500) / 1000);
}

// timings: Actual frame-stage durations; no timing is inferred from CPU usage.
// log: Bounded copied state; these three compact records run outside the panel lock.
void log_completed_frame(const FrameTimings &timings, const CompletedFrameLog &log)
{
    if (!log.rig) {
        ESP_LOGI(kTag, "BOOT tick=%lld total=%ldms",
            static_cast<long long>(timings.started_us),
            rounded_ms(log.completed_us - timings.started_us));
        return;
    }
    const auto &statistics = log.distribution;
    // Keep each line below the sink's 255-byte limit. Source-health changes and
    // real failures still have their own event logs; no recurring model dump.
    if (log.paused) {
        ESP_LOGI(kTag, "PERF tick=%lld f=%lu mode=paused %s",
            static_cast<long long>(timings.started_us),
            static_cast<unsigned long>(log.frame_count), log.readout.processors);
    } else {
        ESP_LOGI(kTag, "PERF tick=%lld f=%lu mode=%s n=%lu mean=%ldms p95=%ldms missed=%lu %s %s",
            static_cast<long long>(timings.started_us),
            static_cast<unsigned long>(log.frame_count), log.debug ? "skeleton" : "artwork",
            static_cast<unsigned long>(statistics.count),
            rounded_ms(statistics.mean.total_us), rounded_ms(statistics.p95_us),
            static_cast<unsigned long>(statistics.missed_deadlines),
            log.readout.frame_rate, log.readout.processors);
    }
    ESP_LOGI(kTag, "STAGE ms geo=%ld dirty=%ld draw=%ld convert=%ld submit=%ld dma=%ld total=%ld prev_log=%ld",
        rounded_ms(timings.geometry_us), rounded_ms(timings.damage_us),
        rounded_ms(timings.raster_us), rounded_ms(log.transfer.last_conversion_us),
        rounded_ms(log.transfer.last_submission_us), rounded_ms(log.transfer.last_completion_wait_us),
        rounded_ms(log.completed_us - timings.started_us), rounded_ms(timings.previous_log_output_us));
    ESP_LOGI(kTag, "DRAW ms clear=%ld art=%ld skel=%ld nav=%ld links=%ld tangents=%ld markers=%ld px=%lu bytes=%lu rects=%u tiles=%u/%u",
        rounded_ms(timings.raster_profile.clear_us), rounded_ms(timings.raster_profile.artwork_us),
        rounded_ms(timings.raster_profile.skeleton_us), rounded_ms(timings.raster_profile.navigation_us),
        rounded_ms(timings.raster_profile.skeleton_links_us),
        rounded_ms(timings.raster_profile.skeleton_tangents_us),
        rounded_ms(timings.raster_profile.skeleton_markers_us),
        static_cast<unsigned long>(timings.submitted_pixels),
        static_cast<unsigned long>(timings.submitted_pixels * sizeof(uint16_t)),
        timings.dirty_rectangles, timings.rasterized_tiles, timings.visited_tiles);
}

// plan: Frame-local damage coverage, before any tile is rasterized.
// control: Typed navigation identity whose visible state changed.
esp_err_t include_control_damage(CODY::DirtyPlan &plan, CODY::RigControl control)
{
    CODY::DamageRect bounds;
    const esp_err_t error = CODY::rig_control_damage_bounds(control, bounds);
    if (error != ESP_OK) {
        return error;
    }
    return CODY::include_damage(plan, bounds);
}

// Hold the shared recursive panel lock across every strip so rotation and standby
// cannot alter the panel/touch mapping in the middle of one complete redraw.
// resume_animation: Skip parked time when restarting the animation clocks after standby.
// deferred_log: Startup's outer recursive lock defers output until that owner releases it.
// worker_trace: Optional timings captured by the worker before it enters the frame owner.
DrawResult draw_companion_frame(bool resume_animation = false,
                                CompletedFrameReport *deferred_log = nullptr,
                                const FrameTimings::WorkerLoopTrace *worker_trace = nullptr)
{
    FrameTimings timings{};
    if (worker_trace != nullptr) {
        timings.worker = *worker_trace;
    }
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        return {ESP_ERR_INVALID_STATE, renderer_is_parked(), 0};
    }
    timings.frame_begin_started_us = esp_timer_get_time();
    const esp_err_t lock_error = LUNASHELL_display_frame_begin(kFrameBudgetMs);
    timings.frame_begin_completed_us = esp_timer_get_time();
    if (lock_error != ESP_OK) {
        return {lock_error, renderer_is_parked(), 0};
    }
    StandaloneFrameOnExit frame;
    // Retain the companion's state lock while the completed-frame result is recorded.
    const esp_err_t state_lock_error = bsp_display_lock(0);
    if (state_lock_error != ESP_OK) {
        return frame.finish({state_lock_error, false, 0});
    }
    DisplayUnlockOnExit state_lock;
    if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
        const esp_err_t error = s_terminal_error.load(std::memory_order_acquire);
        return frame.finish({error, false, 0});
    }
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        const bool parked = renderer_is_parked();
        return frame.finish({ESP_ERR_INVALID_STATE, parked, 0});
    }
    const uint32_t revision = LUNASHELL_display_runtime_content_revision();
    if (revision == 0) {
        return frame.finish({ESP_ERR_INVALID_RESPONSE, false, 0});
    }
    const CODY2D::IndexedRleImage *image = boot_asset_for(s_state.boot_phase);
    if (image == nullptr) {
        return frame.finish({ESP_ERR_INVALID_STATE, false, 0});
    }

    void *raster_storage = nullptr;
    size_t raster_capacity_bytes = 0;
    const esp_err_t buffer_error = LUNASHELL_display_frame_buffer(&raster_storage, &raster_capacity_bytes);
    if (buffer_error != ESP_OK) {
        return frame.finish({buffer_error, false, 0});
    }
    const size_t row_bytes = kPanelWidth * sizeof(uint16_t);
    if (raster_storage == nullptr || raster_capacity_bytes < row_bytes * 2U ||
        raster_capacity_bytes / row_bytes > kPanelHeight ||
        reinterpret_cast<uintptr_t>(raster_storage) % alignof(uint16_t) != 0) {
        ESP_LOGE(kTag, "Frame raster capacity rejected: pixels=%p bytes=%u (%s)",
            raster_storage, static_cast<unsigned>(raster_capacity_bytes), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return frame.finish({ESP_ERR_INVALID_SIZE, false, 0});
    }
    // Luna's panel rounder aligns both axes to pairs of pixels. Keep tile
    // boundaries even too, so expanding vertical damage never reads outside
    // the rasterized tile. An odd-sized lease can leave its last row unused.
    timings.tile_rows = static_cast<uint16_t>((raster_capacity_bytes / row_bytes) & ~size_t{1});
    const bool reuse_previous = s_resources.previous_frame_valid && !resume_animation &&
        s_rendered_revision.load(std::memory_order_acquire) == revision &&
        s_state.rig_visible && s_cache_was_rig;
    // A failed partial update never becomes the reference for the next frame.
    s_resources.previous_frame_valid = false;
    PendingTouchReports pending_touch;
    timings.buffer_ready_us = esp_timer_get_time();
    timings.started_us = timings.buffer_ready_us;
    CODY::DebugReadout debug_readout;
    CODY::DirtyPlan dirty_plan;
    const bool frame_was_rig = s_state.rig_visible;
    const bool frame_debug = s_state.rig_debug;
    if (s_state.rig_visible) {
        if (resume_animation) {
            s_state.frame_rate = {};
            s_benchmark.reset();
        }
        const esp_err_t readout_error = CODY::prepare_debug_readout(
            s_state.frame_rate, timings.started_us, debug_readout
        );
        if (readout_error != ESP_OK) {
            return frame.finish({readout_error, false, 0});
        }
        if (s_state.animation_paused) {
            std::snprintf(debug_readout.frame_rate, sizeof(debug_readout.frame_rate), "PAUSED");
        }
        // Freeze the evaluated scene as well as the clocks. Fresh sensors keep
        // publishing independently, but cannot deform a paused character.
        const esp_err_t geometry_error = s_state.animation_paused
            ? (s_resources.rig->valid ? ESP_OK : ESP_ERR_INVALID_STATE)
            : evaluate_companion_geometry(timings.started_us, resume_animation);
        if (geometry_error != ESP_OK) {
            return frame.finish({geometry_error, false, 0});
        }
        if (frame_debug) {
            if (s_resources.rig_debug_strokes == nullptr) {
                ESP_LOGE(kTag, "Debug stroke cache unavailable: %s",
                         esp_err_to_name(ESP_ERR_INVALID_STATE));
                return frame.finish({ESP_ERR_INVALID_STATE, false, 0});
            }
            const esp_err_t prepare_error = CODY::prepare_rig_debug_strokes(
                *s_resources.rig, s_state.selected_point, *s_resources.rig_debug_strokes
            );
            if (prepare_error != ESP_OK) {
                ESP_LOGE(kTag, "Frame debug stroke preparation failed: %s",
                         esp_err_to_name(prepare_error));
                return frame.finish({prepare_error, false, 0});
            }
        }
        timings.geometry_us = esp_timer_get_time() - timings.started_us;
    }
    timings.geometry_completed_us = esp_timer_get_time();
    const int64_t damage_started_us = timings.geometry_completed_us;
    esp_err_t plan_error = ESP_OK;
    if (!reuse_previous) {
        plan_error = CODY::include_damage(dirty_plan, {0, 0, kPanelWidth, kPanelHeight});
    } else {
        plan_error = CODY::plan_rig_damage(*s_resources.previous_rig, *s_resources.rig,
            s_cached_rig_debug, frame_debug, s_cached_selected_point, s_state.selected_point,
            dirty_plan);
        if (plan_error == ESP_OK &&
            (std::strcmp(s_cached_readout.frame_rate, debug_readout.frame_rate) != 0 ||
             std::strcmp(s_cached_readout.processors, debug_readout.processors) != 0)) {
            plan_error = CODY::include_damage(dirty_plan, CODY::debug_readout_damage_bounds());
        }
        if (plan_error == ESP_OK && s_cached_rig_debug != frame_debug) {
            plan_error = include_control_damage(dirty_plan, CODY::RigControl::Overlay);
        }
        if (plan_error == ESP_OK && s_cached_rig_paused != s_state.animation_paused) {
            plan_error = include_control_damage(dirty_plan, CODY::RigControl::Pause);
        }
    }
    timings.damage_us = esp_timer_get_time() - damage_started_us;
    if (plan_error != ESP_OK) {
        return frame.finish({plan_error, false, 0});
    }
    esp_err_t result = ESP_OK;
    uint32_t next_touch_poll_row = kTouchPollRows;
    for (uint32_t row = 0; row < kPanelHeight; row += timings.tile_rows) {
        ++timings.visited_tiles;
        result = LUNASHELL_display_frame_check_deadline();
        if (result != ESP_OK) {
            break;
        }
        if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
            result = s_terminal_error.load(std::memory_order_acquire);
            break;
        }
        // This guard remains explicit even though the shared lock prevents a
        // standby request from closing the gate until the complete frame ends.
        if (!LUNASHELL_display_runtime_writes_allowed()) {
            const bool parked = renderer_is_parked();
            return frame.finish({ESP_ERR_INVALID_STATE, parked, 0});
        }
        const uint32_t remaining_rows = kPanelHeight - row;
        const uint16_t strip_rows = static_cast<uint16_t>(
            remaining_rows < timings.tile_rows ? remaining_rows : timings.tile_rows
        );
        CODY::DamageRect damage;
        result = CODY::dirty_tile_bounds(dirty_plan, static_cast<uint16_t>(row), strip_rows, damage);
        if (result != ESP_OK) {
            break;
        }
        if (damage.empty()) {
            ++timings.skipped_tiles;
        } else {
            ++timings.rasterized_tiles;
            const uint16_t width = damage.right - damage.left;
            const uint16_t height = damage.bottom - damage.top;
            // Raster directly into the leased DMA base with a compact stride.
            // All current layers are recomposed, including unchanged overlaps.
            const CODY2D::Surface strip{
                static_cast<uint16_t *>(raster_storage), width, height, width,
                static_cast<uint32_t>(raster_capacity_bytes / sizeof(uint16_t))
            };
            const int64_t raster_started_us = esp_timer_get_time();
            if (s_state.rig_visible) {
                result = CODY::draw_rig_rows(
                    *s_resources.rig, strip, damage.top, frame_debug, s_state.selected_point,
                    CODY::PocData::execution_timings ? &timings.raster_profile : nullptr,
                    s_state.animation_paused, damage.left,
                    frame_debug ? s_resources.rig_debug_strokes : nullptr
                );
                if (result == ESP_OK) {
                    result = CODY::draw_debug_readout_rows(
                        debug_readout, strip, damage.top, damage.left
                    );
                }
            } else {
                // Boot assets invalidate the entire canvas, so their decoder
                // still receives full-width rows with no horizontal crop.
                result = CODY2D::decode_rows(*image, damage.top, strip);
            }
            timings.raster_us += esp_timer_get_time() - raster_started_us;
            if (result == ESP_OK) {
                const int64_t transfer_started_us = esp_timer_get_time();
                result = LUNASHELL_display_write_rgb565(
                    damage.left, damage.top, damage.right, damage.bottom, strip.pixels
                );
                timings.transfer_us += esp_timer_get_time() - transfer_started_us;
                if (result == ESP_OK) {
                    timings.submitted_pixels += static_cast<uint32_t>(width) * height;
                    ++timings.dirty_rectangles;
                }
            }
        }
        if (result != ESP_OK) {
            // The lock still excludes power transitions here: a returned write
            // error is a real driver/watchdog failure, never a successful park.
            break;
        }
        // Service IRQ reports between strips; the evaluated geometry stays immutable.
        if (s_start_state.load(std::memory_order_acquire) == StartState::Running &&
            (row + strip_rows >= next_touch_poll_row || row + strip_rows == kPanelHeight)) {
            ++timings.touch_polls;
            next_touch_poll_row = ((row + strip_rows) / kTouchPollRows + 1U) * kTouchPollRows;
            const esp_err_t touch_error = capture_pending_touch(pending_touch);
            if (touch_error != ESP_OK) {
                result = fail_consumer("Touch report during frame", touch_error, true);
                break;
            }
        }
    }
    timings.tiles_completed_us = esp_timer_get_time();
    if (result == ESP_OK &&
        s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
        result = s_terminal_error.load(std::memory_order_acquire);
    }
    const uint32_t completed_revision = result == ESP_OK ? revision : 0;
    timings.frame_end_started_us = esp_timer_get_time();
    const DrawResult completed = frame.finish({result, false, completed_revision});
    timings.frame_end_completed_us = esp_timer_get_time();
    s_resources.previous_frame_valid = completed.error == ESP_OK;
    if (completed.error == ESP_OK) {
        // Commit geometry only after every panel write and frame retirement.
        // The panel byte swap touched only the DMA tile, never this reference.
        if (frame_was_rig) {
            *s_resources.previous_rig = *s_resources.rig;
        }
        s_cache_was_rig = frame_was_rig;
        s_cached_rig_debug = frame_debug;
        s_cached_rig_paused = s_state.animation_paused;
        s_cached_selected_point = s_state.selected_point;
        s_cached_readout = debug_readout;
    }
    CompletedFrameLog log{};
    log.readout = debug_readout;
    bool should_log = false;
    if (completed.error == ESP_OK) {
        const esp_err_t statistics_error = record_completed_frame(timings, log, should_log);
        timings.statistics_completed_us = esp_timer_get_time();
        if (statistics_error != ESP_OK) {
            return {fail_consumer("Completed frame statistics", statistics_error, false), false, 0};
        }
    }
    if (completed.error == ESP_OK) {
        const esp_err_t input_error = dispatch_pending_touch(pending_touch);
        if (input_error != ESP_OK) {
            return {fail_consumer("Deferred touch dispatch", input_error, true), false, 0};
        }
    }
    // A slow log sink must not keep rotation or standby waiting behind the renderer.
    state_lock.release();
    timings.state_unlocked_us = esp_timer_get_time();
    timings.previous_log_output_us = s_previous_log_output_us;
    if (should_log) {
        if (deferred_log != nullptr) {
            deferred_log->timings = timings;
            deferred_log->log = log;
            deferred_log->ready = true;
        } else {
            const int64_t log_started_us = esp_timer_get_time();
            log_completed_frame(timings, log);
            s_previous_log_output_us = static_cast<uint32_t>(esp_timer_get_time() - log_started_us);
        }
    }
    return completed;
}

// requested_phase: Next real startup checkpoint; backward or duplicate changes are rejected.
// operation: Startup checkpoint named in logs and structured display failure publication.
esp_err_t advance_boot_phase(BootPhase requested_phase, const char *operation)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return fail_consumer(operation, lock_error, false);
    }
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        bsp_display_unlock();
        ESP_LOGE(kTag, "%s rejected for consumer state %lu: %s", operation,
                 static_cast<unsigned long>(state), esp_err_to_name(error));
        return error;
    }
    const bool is_next_checkpoint =
        (s_state.boot_phase == BootPhase::Sleeping && requested_phase == BootPhase::Waking) ||
        (s_state.boot_phase == BootPhase::Waking && requested_phase == BootPhase::Ready);
    if (!is_next_checkpoint) {
        const uint32_t current_phase = static_cast<uint32_t>(s_state.boot_phase);
        bsp_display_unlock();
        ESP_LOGE(kTag, "%s rejected: current phase=%lu requested phase=%lu (%s)",
                 operation, static_cast<unsigned long>(current_phase),
                 static_cast<unsigned long>(requested_phase),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // Every immutable flash asset completed full validation before worker creation.
    s_state.boot_phase = requested_phase;
    CompletedFrameReport report;
    const DrawResult draw = draw_companion_frame(false, &report);
    if (draw.error != ESP_OK) {
        const esp_err_t result = fail_consumer(operation, draw.error, false);
        bsp_display_unlock();
        return result;
    }
    s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
    bsp_display_unlock();
    if (report.ready) {
        log_completed_frame(report.timings, report.log);
    }
    ESP_LOGI(kTag, "%s drawn", operation);
    return ESP_OK;
}

// Semantic touch identities are independent from renderer storage.
enum class TouchAction : uint8_t {
    None,
    ReturnToBoot,
    ToggleOverlay,
    TogglePause,
    SelectPoint,
};

// point: Real logical touch coordinate already mapped by the display owner.
TouchAction classify_rig_touch(const LUNASHELL_touch_point_t &point)
{
    switch (CODY::rig_control_at({static_cast<float>(point.x), static_cast<float>(point.y)})) {
    case CODY::RigControl::Back:
        return TouchAction::ReturnToBoot;
    case CODY::RigControl::Overlay:
        return TouchAction::ToggleOverlay;
    case CODY::RigControl::Pause:
        return TouchAction::TogglePause;
    case CODY::RigControl::None:
        break;
    }
    return s_state.rig_debug ? TouchAction::SelectPoint : TouchAction::None;
}

// Shared route entry: touch and automatic demo use the same clocks, buffers and model.
// Caller owns the display/state lock; entering does not draw while another frame is active.
void enter_animation_demo()
{
    s_state.rig_visible = true;
    s_state.rig_debug = true;
    s_state.animation_paused = false;
    s_state.animation_elapsed_us = 0;
    s_state.contour = CODY::make_contour_state();
    s_state.face = CODY::Data::ready;
    s_state.response = CODY::make_response_state(CODY::Data::response);
    s_state.response_sources_logged = false;
    s_state.animation_sampled_us = 0;
    s_state.last_timing_log_us = 0;
    s_state.rig_frame_count = 0;
    s_state.frame_rate = {};
    s_benchmark.reset();
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Animation demo entered: %s; authored targets and real snapshot stimuli", CODY::Data::character_name);
}

// The startup task submits an atomic intent; only the worker changes animation state.
esp_err_t apply_demo_request()
{
    if (!s_demo_requested.exchange(false, std::memory_order_acq_rel)) {
        return ESP_OK;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    if (s_state.boot_phase != BootPhase::Ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_state.rig_visible) {
        enter_animation_demo();
    }
    return ESP_OK;
}

// point: Successfully acquired valid hardware sample, handled under the display lock.
esp_err_t handle_touch_sample(const LUNASHELL_touch_point_t &point)
{
    const bool pressed_edge = point.pressed && !s_state.touch_pressed;
    s_state.touch_pressed = point.pressed;
    if (pressed_edge && s_state.boot_phase == BootPhase::Ready && LUNASHELL_display_runtime_writes_allowed()) {
        if (!s_state.rig_visible) {
            if (point.x >= 73 && point.x < 393 && point.y >= 73 && point.y < 393) {
                enter_animation_demo();
            }
        } else {
            const TouchAction action = classify_rig_touch(point);
            if (action == TouchAction::ReturnToBoot) {
                s_state.rig_visible = false;
                s_state.selected_point = CODY::kNoPoint;
                ESP_LOGI(kTag, "Rig Back -> completed boot checkpoint");
            } else if (action == TouchAction::TogglePause) {
                s_state.animation_paused = !s_state.animation_paused;
                // Resume from this pose without integrating time or motion
                // accumulated during the pause. Keep both frame windows clean.
                s_state.animation_sampled_us = 0;
                CODY::rebase_response_motion(s_state.response);
                s_state.frame_rate = {};
                s_benchmark.reset();
                s_state.last_timing_log_us = 0;
                ESP_LOGI(kTag, "Animation %s by touch=(%u,%u); HUD period=%lu ms",
                    s_state.animation_paused ? "PAUSED" : "RESUMED", point.x, point.y,
                    static_cast<unsigned long>(s_state.animation_paused ? kPausedReadoutPeriodMs : kRigFramePeriodMs));
            } else if (action == TouchAction::ToggleOverlay) {
                s_state.rig_debug = !s_state.rig_debug;
                s_state.selected_point = CODY::kNoPoint;
                s_state.last_timing_log_us = 0;
                ESP_LOGI(kTag, "Rig geometry overlay=%u", s_state.rig_debug);
            } else if (action == TouchAction::SelectPoint) {
                const esp_err_t selection_error = CODY::nearest_rig_point(*s_resources.rig,
                    {static_cast<float>(point.x), static_cast<float>(point.y)}, 12, s_state.selected_point);
                if (selection_error != ESP_OK) {
                    return selection_error;
                }
                if (s_state.selected_point != CODY::kNoPoint) {
                    const auto local = s_resources.rig->local[CODY::to_index(s_state.selected_point)];
                    ESP_LOGI(kTag, "Rig point=%s local=(%.4f,%.4f) touch=(%u,%u)",
                             CODY::rig_point_name(s_state.selected_point), local.x, local.y, point.x, point.y);
                }
            }
            if (!s_state.animation_paused && s_state.rig_frame_count != 0 &&
                action != TouchAction::ReturnToBoot && action != TouchAction::ToggleOverlay &&
                action != TouchAction::TogglePause) {
                CODY::EyeId eye = CODY::EyeId::None;
                const esp_err_t hit_error = CODY::hit_test_eye(*s_resources.rig,
                    {static_cast<float>(point.x), static_cast<float>(point.y)},
                    CODY::Data::response.touch.eye_hit_radius, eye);
                if (hit_error != ESP_OK) {
                    return hit_error;
                }
                if (eye != CODY::EyeId::None) {
                    using Kind = LUNASHELL::CharacterInteractionSnapshot::Kind;
                    const auto kind = eye == CODY::EyeId::Left ? Kind::EyeLeftTap : Kind::EyeRightTap;
                    const esp_err_t event_error = LUNASHELL::DeviceSnapshotService::instance()
                        .reportCharacterInteraction(kind, point.sample_timestamp_us);
                    if (event_error != ESP_OK) {
                        return event_error;
                    }
                    ESP_LOGI(kTag, "Eye tap=%s: protective closure and annoyance",
                        eye == CODY::EyeId::Left ? "left" : "right");
                }
            }
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
    }
    return ESP_OK;
}

// point: Receives a real report, retaining its original timestamp and error.
TouchPollResult read_touch_report(LUNASHELL_touch_point_t &point)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return {TouchPollStatus::Failed, lock_error};
    }
    const DisplayUnlockOnExit display_lock;
    const esp_err_t sample_error = LUNASHELL_touch_read_point(&point);
    if (sample_error == ESP_ERR_NOT_FOUND && point.error == sample_error &&
        !point.valid && point.sample_timestamp_us == 0) {
        return {TouchPollStatus::NoReport, sample_error};
    }
    if (sample_error != ESP_OK || !point.valid) {
        return {TouchPollStatus::Failed,
                sample_error != ESP_OK ? sample_error : ESP_ERR_INVALID_RESPONSE};
    }
    return {TouchPollStatus::Reported, ESP_OK};
}

// reports: Frame-local reports to dispatch after complete frame retirement.
esp_err_t capture_pending_touch(PendingTouchReports &reports)
{
    LUNASHELL_touch_point_t point{};
    const TouchPollResult sample = read_touch_report(point);
    if (sample.status == TouchPollStatus::NoReport) {
        return ESP_OK; // No read took place; no measurement was published.
    }
    if (sample.status == TouchPollStatus::Failed) {
        return sample.error;
    }
    if (reports.count >= sizeof(reports.samples) / sizeof(reports.samples[0])) {
        ESP_LOGE(kTag, "Frame touch report capacity exhausted (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    reports.samples[reports.count++] = point;
    return ESP_OK;
}

// reports: Acquired reports in their original order, under the display lock.
esp_err_t dispatch_pending_touch(const PendingTouchReports &reports)
{
    for (uint16_t report_index = 0; report_index < reports.count; ++report_index) {
        const esp_err_t result = handle_touch_sample(reports.samples[report_index]);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

// Poll the hardware owner once; no report is distinct from a failed hardware read.
TouchPollResult sample_touch()
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return {TouchPollStatus::Failed, lock_error};
    }
    const DisplayUnlockOnExit display_lock;
    LUNASHELL_touch_point_t point{};
    const TouchPollResult sample = read_touch_report(point);
    if (sample.status != TouchPollStatus::Reported) {
        return sample;
    }
    const esp_err_t route_error = handle_touch_sample(point);
    return {route_error == ESP_OK ? TouchPollStatus::Reported : TouchPollStatus::Failed,
            route_error};
}

// context: Unused; this single worker owns the boot/debug route and animation clock.
void companion_worker(void *context)
{
    (void)context;
    bool parked = false;
    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        FrameTimings::WorkerLoopTrace worker_trace{};
        worker_trace.valid = true;
        worker_trace.loop_started_us = now_us;
        // Only this worker changes rig visibility after startup; boot remains static.
        const int64_t remaining_us = s_state.next_frame_us - now_us;
        uint32_t wait_ms = kSamplePeriodMs;
        if (parked) {
            wait_ms = kStandbySamplePeriodMs;
        } else if (s_state.rig_visible) {
            wait_ms = remaining_us > 0 ?
                static_cast<uint32_t>((remaining_us + 999) / 1000) : 1;
        }
        worker_trace.requested_wait_ms = wait_ms;
        worker_trace.wait_started_us = esp_timer_get_time();
        const esp_err_t wait_error = LUNASHELL_touch_wait_for_report(wait_ms);
        worker_trace.wait_completed_us = esp_timer_get_time();
        if (wait_error != ESP_OK && wait_error != ESP_ERR_NOT_FOUND) {
            fail_consumer("Touch IRQ wait", wait_error, true);
            break;
        }
        if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
            break;
        }
        const TouchPollResult touch = sample_touch();
        if (touch.status == TouchPollStatus::Failed) {
            fail_consumer("Touch sample", touch.error, true);
            break;
        }
        const esp_err_t demo_error = apply_demo_request();
        if (demo_error != ESP_OK) {
            fail_consumer("Automatic demo request", demo_error, false);
            break;
        }
        if (!LUNASHELL_display_runtime_writes_allowed()) {
            if (!renderer_is_parked()) {
                fail_consumer("Display readiness", ESP_ERR_INVALID_STATE, false);
                break;
            }
            parked = true;
            continue;
        }
        const uint32_t revision = LUNASHELL_display_runtime_content_revision();
        if (!parked && s_state.rig_visible && esp_timer_get_time() < s_state.next_frame_us &&
            !s_state.frame_dirty.load(std::memory_order_acquire) &&
            revision == s_rendered_revision.load(std::memory_order_acquire)) {
            continue;
        }
        if (!parked && !s_state.rig_visible && !s_state.frame_dirty.load(std::memory_order_acquire) &&
            revision == s_rendered_revision.load(std::memory_order_acquire)) {
            continue;
        }
        worker_trace.draw_requested_us = esp_timer_get_time();
        worker_trace.scheduled_us = s_state.rig_visible && s_state.next_frame_us > 0
            ? s_state.next_frame_us : worker_trace.draw_requested_us;
        const DrawResult draw = draw_companion_frame(parked, nullptr, &worker_trace);
        if (draw.parked) {
            ESP_LOGW(kTag, "Redraw parked during power transition: %s",
                     esp_err_to_name(draw.error));
            parked = true;
            continue;
        }
        if (draw.error != ESP_OK) {
            fail_consumer("Companion frame redraw", draw.error, false);
            break;
        }
        s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
        parked = false;
    }
    const esp_err_t cleanup_error = release_worker_resources();
    if (cleanup_error != ESP_OK) {
        fail_consumer("Strip cleanup", cleanup_error, false);
    }
    vTaskDeleteWithCaps(nullptr);
}

}  // namespace

// Start the single companion owner; a failed stage keeps its original error.
extern "C" esp_err_t CODY_companion_start(void)
{
    StartState expected = StartState::Idle;
    if (!s_start_state.compare_exchange_strong(
            expected, StartState::Starting,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        const esp_err_t result = expected == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire)
            : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Start rejected for existing consumer state %lu: %s",
                 static_cast<unsigned long>(expected), esp_err_to_name(result));
        return result;
    }
    const esp_err_t asset_error = validate_boot_asset(
        CODY_asset::sleeping_blue, "Sleeping boot asset"
    );
    if (asset_error != ESP_OK) {
        return fail_consumer("Boot asset validation", asset_error, false);
    }
    const esp_err_t waking_asset_error = validate_boot_asset(
        CODY_asset::waking_blue, "Waking boot asset"
    );
    if (waking_asset_error != ESP_OK) {
        return fail_consumer("Waking boot asset validation", waking_asset_error, false);
    }
    const esp_err_t ready_asset_error = validate_boot_asset(
        CODY_asset::ready_blue, "Ready boot asset"
    );
    if (ready_asset_error != ESP_OK) {
        return fail_consumer("Ready boot asset validation", ready_asset_error, false);
    }
    const esp_err_t response_config_error = CODY::validate_response_config(CODY::Data::response);
    if (response_config_error != ESP_OK) {
        return fail_consumer("Response configuration", response_config_error, false);
    }
    s_state.response = CODY::make_response_state(CODY::Data::response);
    void *previous_rig_storage = heap_caps_malloc(
        sizeof(CODY::RigScene), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (previous_rig_storage == nullptr) {
        release_companion_resources();
        return fail_consumer("Previous rig allocation", ESP_ERR_NO_MEM, false);
    }
    s_resources.previous_rig = new (previous_rig_storage) CODY::RigScene{};
    void *rig_storage = heap_caps_malloc(sizeof(CODY::RigScene), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rig_storage == nullptr) {
        release_companion_resources();
        return fail_consumer("Rig scene allocation", ESP_ERR_NO_MEM, false);
    }
    s_resources.rig = new (rig_storage) CODY::RigScene{};
    void *debug_stroke_storage = heap_caps_malloc(
        sizeof(CODY::RigDebugStrokeCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (debug_stroke_storage == nullptr) {
        release_companion_resources();
        return fail_consumer("Rig debug stroke cache allocation", ESP_ERR_NO_MEM, false);
    }
    s_resources.rig_debug_strokes = new (debug_stroke_storage) CODY::RigDebugStrokeCache{};
    const DrawResult draw = draw_companion_frame();
    if (draw.error != ESP_OK) {
        release_companion_resources();
        return fail_consumer("Mandatory boot frame", draw.error, false);
    }
    s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
    const TouchPollResult touch = sample_touch();
    if (touch.status == TouchPollStatus::Failed) {
        release_companion_resources();
        return fail_consumer("Mandatory touch input", touch.error, true);
    }
    if (touch.status == TouchPollStatus::NoReport) {
        ESP_LOGI(kTag, "Mandatory touch input initialized; awaiting first hardware report");
    }
    s_start_state.store(StartState::Running, std::memory_order_release);
    TaskHandle_t worker = nullptr;
    if (xTaskCreateWithCaps(
            companion_worker, "CODY_companion", kWorkerStackBytes,
            nullptr, 2, &worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        release_companion_resources();
        return fail_consumer("Worker creation", ESP_ERR_NO_MEM, false);
    }
    ESP_LOGI(kTag, "Blue boot frame and real touch ready; frame-owned internal raster, active=%lu ms standby=%lu ms",
             static_cast<unsigned long>(kSamplePeriodMs),
             static_cast<unsigned long>(kStandbySamplePeriodMs));
    return ESP_OK;
}

// Advance to the real local-services checkpoint under the shared render lock.
extern "C" esp_err_t CODY_companion_boot_midpoint(void)
{
    return advance_boot_phase(BootPhase::Waking, "Boot local-services checkpoint");
}

// Advance to the completed phase only after the startup owner finishes its watchdog.
extern "C" esp_err_t CODY_companion_boot_completed(void)
{
    return advance_boot_phase(BootPhase::Ready, "Boot completed ready face");
}

// Automatic route entry is configurable; ready-frame completion remains a prerequisite.
extern "C" esp_err_t CODY_companion_demo_start(void)
{
    if (!CODY::PocData::autostart) {
        return ESP_OK;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Demo entry lock failed: %s", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    if (s_start_state.load(std::memory_order_acquire) != StartState::Running ||
        s_state.boot_phase != BootPhase::Ready) {
        ESP_LOGE(kTag, "Demo entry state rejected: %s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.rig_visible) {
        return ESP_OK; // A real touch may already have entered while startup finishes.
    }
    s_demo_requested.store(true, std::memory_order_release);
    return ESP_OK;
}

#else

// The companion is absent from this explicit build profile.
extern "C" esp_err_t CODY_companion_demo_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// Start the single companion owner; a failed stage keeps its original error.
extern "C" esp_err_t CODY_companion_start(void)
{
    ESP_LOGE("CODY_companion", "Start rejected: consumer excluded by profile (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t CODY_companion_boot_midpoint(void)
{
    ESP_LOGE("CODY_companion", "Boot midpoint rejected: consumer excluded by profile (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t CODY_companion_boot_completed(void)
{
    ESP_LOGE("CODY_companion", "Boot completion rejected: consumer excluded by profile (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
