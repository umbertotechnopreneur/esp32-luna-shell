#include "CODY_companion.h"

#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
#include <atomic>
#include <cstdint>
#include <new>

#include "CODY2D.hpp"
#include "CODY_rig.hpp"
#include "CODY_debug_hud.hpp"
#include "CODY_damage.hpp"
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
// The current and cached tile must fit together in the 64 KiB data cache,
// leaving room for the evaluated rig. The BSP still owns transient DMA slots.
constexpr uint16_t kStripRows = 16;
constexpr uint16_t kPaletteLimit = 64;
constexpr uint32_t kStripPixels = static_cast<uint32_t>(kPanelWidth) * kStripRows;
constexpr uint32_t kFramePixels = static_cast<uint32_t>(kPanelWidth) * kPanelHeight;
constexpr uint32_t kSamplePeriodMs = 50;
constexpr uint32_t kStandbySamplePeriodMs = 200;
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
constexpr uint32_t kWorkerStackBytes = 6144;
constexpr uint32_t kRigFramePeriodMs = 33;
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

// One report per strip fits here. Preserve every press/release until the frame
// ends, so a navigation event cannot change half of the displayed image.
struct PendingTouchReports {
    LUNASHELL_touch_point_t samples[(kPanelHeight + kStripRows - 1U) / kStripRows]{};
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
    uint16_t *strip = nullptr;
    uint16_t *frame_cache = nullptr;
    bool frame_cache_valid = false;
    CODY::RigScene *rig = nullptr;
};
CompanionResources s_resources;
std::atomic<uint32_t> s_rendered_revision{0};
std::atomic<bool> s_demo_requested{false};
// This bounded collector shares the display/state lock with completed frame publication.
CODY::FrameBenchmark s_benchmark;
struct CompanionState {
    // After startup, non-atomic changes use the display lock. Only the worker reads its schedule outside it.
    BootPhase boot_phase = BootPhase::Sleeping;
    bool rig_visible = false;
    bool rig_debug = true;
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
    int64_t started_us = 0;
    int64_t geometry_us = 0;
    int64_t raster_us = 0;
    int64_t reuse_us = 0;
    int64_t transfer_us = 0;
    uint32_t submitted_pixels = 0;
    uint16_t dirty_rectangles = 0;
};

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

// Before task creation, only startup owns the strip and no renderer can reference it.
void release_companion_resources()
{
    s_resources.frame_cache_valid = false;
    if (s_resources.frame_cache != nullptr) {
        heap_caps_free(s_resources.frame_cache);
        s_resources.frame_cache = nullptr;
    }
    if (s_resources.rig != nullptr) {
        s_resources.rig->~RigScene();
        heap_caps_free(s_resources.rig);
        s_resources.rig = nullptr;
    }
    if (s_resources.strip != nullptr) {
        heap_caps_free(s_resources.strip);
        s_resources.strip = nullptr;
    }
}

// Drain every possible boot-completion writer before releasing the worker's strip.
// A failed lock leaves the buffer retained until reset because its ownership is ambiguous.
esp_err_t release_worker_resources()
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        const uint32_t retained_bytes =
            (s_resources.strip != nullptr ? kStripPixels * sizeof(uint16_t) : 0) +
            (s_resources.frame_cache != nullptr ? kFramePixels * sizeof(uint16_t) : 0) +
            (s_resources.rig != nullptr ? sizeof(CODY::RigScene) : 0);
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
    bool rig = false;
    CODY::FrameDistribution distribution{};
    uint32_t frame_count = 0;
    bool debug = false;
    int64_t completed_us = 0;
    CODY::ResponseState response{};
    CODY::ResponseFrame response_frame{};
};

struct CompletedFrameReport {
    FrameTimings timings{};
    CompletedFrameLog log{};
    bool ready = false;
};

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
    if (!s_state.rig_visible) {
        should_log = CODY::PocData::execution_timings;
        return ESP_OK;
    }

    // Let late frames go. Schedule from this frame's real timestamp.
    s_state.next_frame_us = timings.started_us + kRigFramePeriodMs * 1000;
    ++s_state.rig_frame_count;
    const int64_t completed_us = log.completed_us;
    CODY::record_frame_rate(s_state.frame_rate, timings.started_us, completed_us);
    if (!CODY::PocData::execution_timings) {
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
    s_state.last_timing_log_us = completed_us;
    log.frame_count = s_state.rig_frame_count;
    log.debug = s_state.rig_debug;
    log.completed_us = completed_us;
    log.response = s_state.response;
    log.response_frame = s_state.response_frame;
    should_log = true;
    return ESP_OK;
}

// timings: Actual geometry, raster and transfer durations for this completed frame.
// log: Copied state; output runs after releasing the panel lock.
void log_completed_frame(const FrameTimings &timings, const CompletedFrameLog &log)
{
    if (!log.rig) {
        ESP_LOGI(kTag, "TIMING boot_buffer geometry=%ldus raster=%ldus transfer=%ldus total=%ldus",
            static_cast<long>(timings.geometry_us), static_cast<long>(timings.raster_us),
            static_cast<long>(timings.transfer_us), static_cast<long>(log.completed_us - timings.started_us));
        return;
    }
    const auto &statistics = log.distribution;
    ESP_LOGI(kTag, "TIMING window mode=%s n=%lu geometry_mean=%luus raster_mean=%luus "
        "transfer_mean=%luus total_mean=%luus median=%luus p95=%luus max=%luus sd=%.1fus missed=%lu budget=%dus",
        log.debug ? "skeleton" : "artwork", static_cast<unsigned long>(statistics.count),
        static_cast<unsigned long>(statistics.mean.geometry_us), static_cast<unsigned long>(statistics.mean.raster_us),
        static_cast<unsigned long>(statistics.mean.transfer_us), static_cast<unsigned long>(statistics.mean.total_us),
        static_cast<unsigned long>(statistics.median_us), static_cast<unsigned long>(statistics.p95_us),
        static_cast<unsigned long>(statistics.maximum_us), statistics.standard_deviation_us,
        static_cast<unsigned long>(statistics.missed_deadlines), CODY::PocData::frame_budget_us);
    ESP_LOGI(
        kTag,
        "Rig frame=%lu debug=%u geometry=%ldus raster=%ldus transfer=%ldus total=%ldus scene=%uB strip=%luB",
        static_cast<unsigned long>(log.frame_count),
        log.debug,
        static_cast<long>(timings.geometry_us),
        static_cast<long>(timings.raster_us),
        static_cast<long>(timings.transfer_us),
        static_cast<long>(log.completed_us - timings.started_us),
        static_cast<unsigned>(sizeof(CODY::RigScene)),
        static_cast<unsigned long>(kStripPixels * sizeof(uint16_t))
    );
    ESP_LOGI(kTag,
             "Response MODEL stress=%.3f fatigue=%.3f fatigue_known=%u root_offset=(%.2f,%.2f)px depth=%.4f "
             "motion=%s(%s) power=%s(%s)",
             log.response.stress, log.response.fatigue, static_cast<unsigned>(log.response.fatigue_known),
             log.response_frame.current_root.x - log.response_frame.resting_root.x,
             log.response_frame.current_root.y - log.response_frame.resting_root.y,
             log.response_frame.depth_scale,
             source_health_name(log.response.motion_status.health), esp_err_to_name(log.response.motion_status.error),
             source_health_name(log.response.power_status.health), esp_err_to_name(log.response.power_status.error));
    ESP_LOGI(kTag, "Response MODEL attention=%.3f speaking=%.3f idle=%.2fs yawn=%.3f annoyance=%.3f closure=%.3f microphone=%s playback=%s",
        log.response.attention, log.response.speaking, log.response.idle_seconds, log.response.yawn_weight,
        log.response.annoyance, log.response.protective_eye_closure,
        source_health_name(log.response.microphone_status.health), source_health_name(log.response.playback_status.health));
    ESP_LOGI(kTag, "Damage rectangles=%u submitted_pixels=%lu canvas_pixels=%lu tile_rows=%u cache_bytes=%lu",
        timings.dirty_rectangles, static_cast<unsigned long>(timings.submitted_pixels),
        static_cast<unsigned long>(kFramePixels), kStripRows,
        static_cast<unsigned long>(kFramePixels * sizeof(uint16_t)));
    ESP_LOGI(kTag, "Render draw=%ldus reuse=%ldus transfer=%ldus %s %s",
        static_cast<long>(timings.raster_us - timings.reuse_us),
        static_cast<long>(timings.reuse_us), static_cast<long>(timings.transfer_us),
        log.readout.frame_rate, log.readout.processors);
}

// Hold the shared recursive panel lock across every strip so rotation and standby
// cannot alter the panel/touch mapping in the middle of one complete redraw.
// resume_animation: Skip parked time when restarting the animation clocks after standby.
// deferred_log: Startup's outer recursive lock defers output until that owner releases it.
DrawResult draw_companion_frame(bool resume_animation = false, CompletedFrameReport *deferred_log = nullptr)
{
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        return {ESP_ERR_INVALID_STATE, renderer_is_parked(), 0};
    }
    const esp_err_t lock_error = LUNASHELL_display_frame_begin(kFrameBudgetMs);
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

    FrameTimings timings{};
    const bool compare_previous = s_resources.frame_cache_valid && !resume_animation &&
        s_rendered_revision.load(std::memory_order_acquire) == revision;
    // A partial cache is never trusted after an error. Only completed frame
    // retirement below makes these candidate pixels eligible for comparison.
    s_resources.frame_cache_valid = false;
    const CODY2D::Surface frame_cache{
        s_resources.frame_cache, kPanelWidth, kPanelHeight, kPanelWidth, kFramePixels
    };
    PendingTouchReports pending_touch;
    timings.started_us = esp_timer_get_time();
    CODY::DebugReadout debug_readout;
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
        const esp_err_t geometry_error = evaluate_companion_geometry(
            timings.started_us, resume_animation
        );
        if (geometry_error != ESP_OK) {
            return frame.finish({geometry_error, false, 0});
        }
        timings.geometry_us = esp_timer_get_time() - timings.started_us;
    }
    esp_err_t result = ESP_OK;
    for (uint32_t row = 0; row < kPanelHeight; row += kStripRows) {
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
            remaining_rows < kStripRows ? remaining_rows : kStripRows
        );
        const CODY2D::Surface strip{
            s_resources.strip, kPanelWidth, strip_rows, kPanelWidth, kStripPixels
        };
        const int64_t raster_started_us = esp_timer_get_time();
        if (s_state.rig_visible) {
            result = CODY::draw_rig_rows(
                *s_resources.rig, strip, static_cast<uint16_t>(row),
                s_state.rig_debug, s_state.selected_point
            );
            if (result == ESP_OK) {
                result = CODY::draw_debug_readout_rows(
                    debug_readout, strip, static_cast<uint16_t>(row)
                );
            }
        } else {
            result = CODY2D::decode_rows(*image, row, strip);
        }
        CODY::DamageRect damage;
        CODY2D::Surface packed;
        const int64_t reuse_started_us = esp_timer_get_time();
        if (result == ESP_OK) {
            result = CODY::measure_damage_rows(strip, static_cast<uint16_t>(row),
                frame_cache, compare_previous, damage);
        }
        if (result == ESP_OK && !damage.empty()) {
            result = CODY::prepare_damage_rows(strip, static_cast<uint16_t>(row),
                frame_cache, damage, packed);
        }
        // Raster cost includes cache comparison and transfer-payload packing.
        timings.reuse_us += esp_timer_get_time() - reuse_started_us;
        timings.raster_us += esp_timer_get_time() - raster_started_us;
        if (result == ESP_OK && !damage.empty()) {
            const int64_t transfer_started_us = esp_timer_get_time();
            result = LUNASHELL_display_write_rgb565(
                damage.left, damage.top, damage.right, damage.bottom, packed.pixels
            );
            timings.transfer_us += esp_timer_get_time() - transfer_started_us;
            if (result == ESP_OK) {
                timings.submitted_pixels += static_cast<uint32_t>(packed.width) * packed.height;
                ++timings.dirty_rectangles;
            }
        }
        if (result != ESP_OK) {
            // The lock still excludes power transitions here: a returned write
            // error is a real driver/watchdog failure, never a successful park.
            break;
        }
        // Service IRQ reports between strips; the evaluated geometry stays immutable.
        if (s_start_state.load(std::memory_order_acquire) == StartState::Running) {
            const esp_err_t touch_error = capture_pending_touch(pending_touch);
            if (touch_error != ESP_OK) {
                result = fail_consumer("Touch report during frame", touch_error, true);
                break;
            }
        }
    }
    if (result == ESP_OK &&
        s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
        result = s_terminal_error.load(std::memory_order_acquire);
    }
    const uint32_t completed_revision = result == ESP_OK ? revision : 0;
    const DrawResult completed = frame.finish({result, false, completed_revision});
    s_resources.frame_cache_valid = completed.error == ESP_OK;
    CompletedFrameLog log{};
    log.readout = debug_readout;
    bool should_log = false;
    if (completed.error == ESP_OK) {
        const esp_err_t statistics_error = record_completed_frame(timings, log, should_log);
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
    if (should_log) {
        if (deferred_log != nullptr) {
            deferred_log->timings = timings;
            deferred_log->log = log;
            deferred_log->ready = true;
        } else {
            log_completed_frame(timings, log);
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
    SelectPoint,
};

// point: Real logical touch coordinate already mapped by the display owner.
TouchAction classify_rig_touch(const LUNASHELL_touch_point_t &point)
{
    const int32_t vertical_offset = static_cast<int32_t>(point.y) - 233;
    const int32_t back_offset = static_cast<int32_t>(point.x) - 40;
    const int32_t overlay_offset = static_cast<int32_t>(point.x) - 426;
    // Small icons, generous fingers: keep the full 60-pixel circles alive.
    if (back_offset * back_offset + vertical_offset * vertical_offset <= 30 * 30) {
        return TouchAction::ReturnToBoot;
    }
    if (overlay_offset * overlay_offset + vertical_offset * vertical_offset <= 30 * 30) {
        return TouchAction::ToggleOverlay;
    }
    return s_state.rig_debug ? TouchAction::SelectPoint : TouchAction::None;
}

// Shared route entry: touch and automatic demo use the same clocks, buffers and model.
// Caller owns the display/state lock; entering does not draw while another frame is active.
void enter_animation_demo()
{
    s_state.rig_visible = true;
    s_state.rig_debug = true;
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
            if (s_state.rig_frame_count != 0 && action != TouchAction::ReturnToBoot && action != TouchAction::ToggleOverlay) {
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
        // Only this worker changes rig visibility after startup; boot remains static.
        const int64_t remaining_us = s_state.next_frame_us - now_us;
        uint32_t wait_ms = kSamplePeriodMs;
        if (parked) {
            wait_ms = kStandbySamplePeriodMs;
        } else if (s_state.rig_visible) {
            wait_ms = remaining_us > 0 ?
                static_cast<uint32_t>((remaining_us + 999) / 1000) : 1;
        }
        const esp_err_t wait_error = LUNASHELL_touch_wait_for_report(wait_ms);
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
        const DrawResult draw = draw_companion_frame(parked);
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
    s_resources.strip = static_cast<uint16_t *>(heap_caps_malloc(
        kStripPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_resources.strip == nullptr) {
        return fail_consumer("Boot strip allocation", ESP_ERR_NO_MEM, false);
    }
    s_resources.frame_cache = static_cast<uint16_t *>(heap_caps_malloc(
        kFramePixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_resources.frame_cache == nullptr) {
        release_companion_resources();
        return fail_consumer("PSRAM frame cache allocation", ESP_ERR_NO_MEM, false);
    }
    void *rig_storage = heap_caps_malloc(sizeof(CODY::RigScene), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rig_storage == nullptr) {
        release_companion_resources();
        return fail_consumer("Rig scene allocation", ESP_ERR_NO_MEM, false);
    }
    s_resources.rig = new (rig_storage) CODY::RigScene{};
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
    ESP_LOGI(kTag, "Blue boot frame and real touch ready; strip=%lu bytes, active=%lu ms standby=%lu ms",
             static_cast<unsigned long>(kStripPixels * sizeof(uint16_t)),
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
