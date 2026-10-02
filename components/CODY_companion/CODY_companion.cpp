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
#include "CODY_response.hpp"
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
constexpr uint16_t kStripRows = 8;
constexpr uint16_t kPaletteLimit = 64;
constexpr uint32_t kStripPixels = static_cast<uint32_t>(kPanelWidth) * kStripRows;
constexpr uint32_t kSamplePeriodMs = 50;
constexpr uint32_t kStandbySamplePeriodMs = 200;
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
constexpr uint32_t kWorkerStackBytes = 6144;
constexpr uint32_t kRigFramePeriodMs = 33;

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
uint16_t *s_strip = nullptr;
std::atomic<uint32_t> s_rendered_revision{0};
// Read and written only while the shared BSP display lock is held.
BootPhase s_boot_phase = BootPhase::Sleeping;
CODY::RigScene *s_rig = nullptr;
bool s_rig_visible = false;
bool s_rig_debug = true;
bool s_touch_pressed = false;
CODY::PointId s_selected_point = CODY::kNoPoint;
std::atomic<bool> s_frame_dirty{false};
int64_t s_animation_elapsed_us = 0;
int64_t s_animation_sampled_us = 0;
CODY::ContourState s_contour{};
CODY::Channels s_face{};
CODY::ResponseState s_response{};
CODY::ResponseFrame s_response_frame{};
bool s_response_sources_logged = false;
int64_t s_next_frame_us = 0;
int64_t s_last_timing_log_us = 0;
uint32_t s_rig_frame_count = 0;
CODY::FrameRateMeasurement s_frame_rate{};

// Adopt one successfully acquired BSP lock. Failed acquisition never creates this guard.
class DisplayUnlockOnExit final {
public:
    // The caller already holds one level of the recursive display lock.
    DisplayUnlockOnExit() = default;

    // Return this call's lock level; an outer recursive lock stays owned.
    ~DisplayUnlockOnExit()
    {
        bsp_display_unlock();
    }

    // other: A lock owner cannot be copied into a second owner.
    DisplayUnlockOnExit(const DisplayUnlockOnExit &other) = delete;
    // other: A live lock owner cannot be replaced by another owner.
    DisplayUnlockOnExit &operator=(const DisplayUnlockOnExit &other) = delete;
};

struct FrameTimings {
    int64_t started_us = 0;
    int64_t geometry_us = 0;
    int64_t raster_us = 0;
    int64_t transfer_us = 0;
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

// Before task creation, only startup owns the strip and no renderer can reference it.
void release_startup_strip()
{
    if (s_rig != nullptr) {
        s_rig->~RigScene();
        heap_caps_free(s_rig);
        s_rig = nullptr;
    }
    if (s_strip != nullptr) {
        heap_caps_free(s_strip);
        s_strip = nullptr;
    }
}

// Drain every possible boot-completion writer before releasing the worker's strip.
// A failed lock leaves the buffer retained until reset because its ownership is ambiguous.
esp_err_t release_worker_strip()
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Strip cleanup retained PSRAM until reset: %s",
                 esp_err_to_name(lock_error));
        return lock_error;
    }
    release_startup_strip();
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
    const char *name = source == CODY::StimulusId::MotionAcceleration ? "motion.acceleration" : "power.charge";
    ESP_LOGI(kTag, "Stimulus %s=%s error=%s sample_at=%lldus", name,
             source_health_name(current.health), esp_err_to_name(current.error),
             static_cast<long long>(current.sampled_at_us));
}

// frame_started_us: Real timestamp sampled while the display lock is held.
// resume_animation: Exclude standby time from both independent animation clocks.
esp_err_t evaluate_companion_geometry(int64_t frame_started_us, bool resume_animation)
{
    if (s_rig == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t animation_delta_us = resume_animation || s_animation_sampled_us == 0
        ? 0 : frame_started_us - s_animation_sampled_us;
    const float animation_delta_ms = static_cast<float>(animation_delta_us) / 1000.f;
    CODY::Channels next_face{};
    const esp_err_t face_error = CODY::advance_face(
        s_face, CODY::Data::defaults, animation_delta_ms, next_face
    );
    if (face_error != ESP_OK) {
        return face_error;
    }
    CODY::ResponseInputs inputs{};
    const esp_err_t observation_error = CODY::observe_response_inputs(inputs);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    CODY::ResponseState previous_response = s_response;
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
        CODY::Data::response, next_response, next_face, CODY::contour_defaults(),
        CODY::Data::placement, inputs.rendering_rotation_degrees, true, response_frame
    );
    if (composition_error != ESP_OK) {
        return composition_error;
    }
    CODY::ContourState next_contour{};
    const esp_err_t contour_error = CODY::advance_contour(
        s_contour, response_frame.contour, animation_delta_ms, next_contour
    );
    if (contour_error != ESP_OK) {
        return contour_error;
    }
    const esp_err_t geometry_error = CODY::evaluate_rig(
        response_frame.face, response_frame.transform, *s_rig, &next_contour
    );
    if (geometry_error != ESP_OK) {
        return geometry_error;
    }
    s_contour = next_contour;
    s_face = next_face;
    log_response_source(CODY::StimulusId::MotionAcceleration, s_response.motion_status,
                        next_response.motion_status, !s_response_sources_logged);
    log_response_source(CODY::StimulusId::PowerCharge, s_response.power_status,
                        next_response.power_status, !s_response_sources_logged);
    s_response_sources_logged = true;
    s_response = next_response;
    s_response_frame = response_frame;
    // Parked time never advances the body, breath, blink or contour clocks.
    s_animation_elapsed_us += animation_delta_us;
    s_animation_sampled_us = frame_started_us;
    return ESP_OK;
}

// timings: Completed frame's measured stages, never an estimated CPU metric.
void record_completed_frame(const FrameTimings &timings)
{
    s_frame_dirty.store(false, std::memory_order_release);
    if (!s_rig_visible) {
        return;
    }

    // Let late frames go. Schedule from this frame's real timestamp.
    s_next_frame_us = timings.started_us + kRigFramePeriodMs * 1000;
    ++s_rig_frame_count;
    const int64_t completed_us = esp_timer_get_time();
    CODY::record_frame_rate(s_frame_rate, timings.started_us, completed_us);
    if (s_last_timing_log_us != 0 && completed_us - s_last_timing_log_us < 2000000) {
        return;
    }

    ESP_LOGI(
        kTag,
        "Rig frame=%lu debug=%u geometry=%ldus raster=%ldus transfer=%ldus total=%ldus scene=%uB strip=%luB",
        static_cast<unsigned long>(s_rig_frame_count),
        s_rig_debug,
        static_cast<long>(timings.geometry_us),
        static_cast<long>(timings.raster_us),
        static_cast<long>(timings.transfer_us),
        static_cast<long>(completed_us - timings.started_us),
        static_cast<unsigned>(sizeof(CODY::RigScene)),
        static_cast<unsigned long>(kStripPixels * sizeof(uint16_t))
    );
    ESP_LOGI(kTag,
             "Response MODEL stress=%.3f fatigue=%.3f fatigue_known=%u root_offset=(%.2f,%.2f)px depth=%.4f "
             "motion=%s(%s) power=%s(%s)",
             s_response.stress, s_response.fatigue, static_cast<unsigned>(s_response.fatigue_known),
             s_response_frame.current_root.x - s_response_frame.resting_root.x,
             s_response_frame.current_root.y - s_response_frame.resting_root.y,
             s_response_frame.depth_scale,
             source_health_name(s_response.motion_status.health), esp_err_to_name(s_response.motion_status.error),
             source_health_name(s_response.power_status.health), esp_err_to_name(s_response.power_status.error));
    s_last_timing_log_us = completed_us;
}

// Hold the shared recursive panel lock across every strip so rotation and standby
// cannot alter the panel/touch mapping in the middle of one complete redraw.
// resume_animation: Skip parked time when restarting the animation clocks after standby.
DrawResult draw_companion_frame(bool resume_animation = false)
{
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        return {ESP_ERR_INVALID_STATE, renderer_is_parked(), 0};
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return {lock_error, renderer_is_parked(), 0};
    }
    const DisplayUnlockOnExit display_lock;
    if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
        const esp_err_t error = s_terminal_error.load(std::memory_order_acquire);
        return {error, false, 0};
    }
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        const bool parked = renderer_is_parked();
        return {ESP_ERR_INVALID_STATE, parked, 0};
    }
    const uint32_t revision = LUNASHELL_display_runtime_content_revision();
    if (revision == 0) {
        return {ESP_ERR_INVALID_RESPONSE, false, 0};
    }
    const auto &image = s_boot_phase == BootPhase::Ready
        ? CODY_asset::ready_blue
        : s_boot_phase == BootPhase::Waking
            ? CODY_asset::waking_blue : CODY_asset::sleeping_blue;

    FrameTimings timings{};
    PendingTouchReports pending_touch;
    timings.started_us = esp_timer_get_time();
    CODY::DebugReadout debug_readout;
    if (s_rig_visible) {
        if (resume_animation) {
            s_frame_rate = {};
        }
        const esp_err_t readout_error = CODY::prepare_debug_readout(
            s_frame_rate, timings.started_us, debug_readout
        );
        if (readout_error != ESP_OK) {
            return {readout_error, false, 0};
        }
        const esp_err_t geometry_error = evaluate_companion_geometry(
            timings.started_us, resume_animation
        );
        if (geometry_error != ESP_OK) {
            return {geometry_error, false, 0};
        }
        timings.geometry_us = esp_timer_get_time() - timings.started_us;
    }
    esp_err_t result = ESP_OK;
    for (uint32_t row = 0; row < kPanelHeight; row += kStripRows) {
        if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
            result = s_terminal_error.load(std::memory_order_acquire);
            break;
        }
        // This guard remains explicit even though the shared lock prevents a
        // standby request from closing the gate until the complete frame ends.
        if (!LUNASHELL_display_runtime_writes_allowed()) {
            const bool parked = renderer_is_parked();
            return {ESP_ERR_INVALID_STATE, parked, 0};
        }
        const uint32_t remaining_rows = kPanelHeight - row;
        const uint16_t strip_rows = static_cast<uint16_t>(
            remaining_rows < kStripRows ? remaining_rows : kStripRows
        );
        const CODY2D::Surface strip{
            s_strip, kPanelWidth, strip_rows, kPanelWidth, kStripPixels
        };
        const int64_t raster_started_us = esp_timer_get_time();
        result = s_rig_visible
            ? CODY::draw_rig_rows(*s_rig, strip, static_cast<uint16_t>(row), s_rig_debug, s_selected_point)
            : CODY2D::decode_rows(image, row, strip);
        if (result == ESP_OK && s_rig_visible) {
            result = CODY::draw_debug_readout_rows(
                debug_readout, strip, static_cast<uint16_t>(row)
            );
        }
        timings.raster_us += esp_timer_get_time() - raster_started_us;
        if (result != ESP_OK) {
            break;
        }
        const int64_t transfer_started_us = esp_timer_get_time();
        result = LUNASHELL_display_write_rgb565(
            0, static_cast<int>(row), kPanelWidth,
            static_cast<int>(row + strip_rows), s_strip
        );
        timings.transfer_us += esp_timer_get_time() - transfer_started_us;
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
    if (result == ESP_OK) {
        record_completed_frame(timings);
        const esp_err_t input_error = dispatch_pending_touch(pending_touch);
        if (input_error != ESP_OK) {
            return {fail_consumer("Deferred touch dispatch", input_error, true), false, 0};
        }
    }
    return {result, false, result == ESP_OK ? revision : 0};
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
        (s_boot_phase == BootPhase::Sleeping && requested_phase == BootPhase::Waking) ||
        (s_boot_phase == BootPhase::Waking && requested_phase == BootPhase::Ready);
    if (!is_next_checkpoint) {
        const uint32_t current_phase = static_cast<uint32_t>(s_boot_phase);
        bsp_display_unlock();
        ESP_LOGE(kTag, "%s rejected: current phase=%lu requested phase=%lu (%s)",
                 operation, static_cast<unsigned long>(current_phase),
                 static_cast<unsigned long>(requested_phase),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // Every immutable flash asset completed full validation before worker creation.
    s_boot_phase = requested_phase;
    const DrawResult draw = draw_companion_frame();
    if (draw.error != ESP_OK) {
        const esp_err_t result = fail_consumer(operation, draw.error, false);
        bsp_display_unlock();
        return result;
    }
    s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
    bsp_display_unlock();
    ESP_LOGI(kTag, "%s drawn", operation);
    return ESP_OK;
}

// point: Successfully acquired valid hardware sample, handled under the display lock.
esp_err_t handle_touch_sample(const LUNASHELL_touch_point_t &point)
{
    const bool pressed_edge = point.pressed && !s_touch_pressed;
    s_touch_pressed = point.pressed;
    if (pressed_edge && s_boot_phase == BootPhase::Ready && LUNASHELL_display_runtime_writes_allowed()) {
        if (!s_rig_visible) {
            if (point.x >= 73 && point.x < 393 && point.y >= 73 && point.y < 393) {
                s_rig_visible = true;
                s_rig_debug = true;
                s_animation_elapsed_us = 0;
                s_contour = CODY::make_contour_state();
                s_face = CODY::Data::defaults;
                s_response = CODY::make_response_state(CODY::Data::response);
                s_response_sources_logged = false;
                s_animation_sampled_us = 0;
                s_last_timing_log_us = 0;
                s_rig_frame_count = 0;
                s_frame_rate = {};
                s_frame_dirty.store(true, std::memory_order_release);
                ESP_LOGI(kTag, "Rig debug entered: %s; independent contour, breathing and eyelids", CODY::Data::character_name);
            }
        } else {
            const int32_t dx = static_cast<int32_t>(point.x) - 40;
            const int32_t dy = static_cast<int32_t>(point.y) - 233;
            const int32_t right_dx = static_cast<int32_t>(point.x) - 426;
            if (dx*dx + dy*dy <= 900) {
                s_rig_visible = false;
                s_selected_point = CODY::kNoPoint;
                ESP_LOGI(kTag, "Rig Back -> completed boot checkpoint");
            } else if (right_dx*right_dx + dy*dy <= 900) {
                s_rig_debug = !s_rig_debug;
                s_selected_point = CODY::kNoPoint;
                s_last_timing_log_us = 0;
                ESP_LOGI(kTag, "Rig geometry overlay=%u", s_rig_debug);
            } else if (s_rig_debug) {
                const esp_err_t selection_error = CODY::nearest_rig_point(*s_rig,
                    {static_cast<float>(point.x), static_cast<float>(point.y)}, 12, s_selected_point);
                if (selection_error != ESP_OK) {
                    return selection_error;
                }
                if (s_selected_point != CODY::kNoPoint) {
                    const auto local = s_rig->local[CODY::to_index(s_selected_point)];
                    ESP_LOGI(kTag, "Rig point=%s local=(%.4f,%.4f) touch=(%u,%u)",
                             CODY::rig_point_name(s_selected_point), local.x, local.y, point.x, point.y);
                }
            }
            s_frame_dirty.store(true, std::memory_order_release);
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
    const esp_err_t sample_error = LUNASHELL_touch_read_point(&point);
    if (sample_error == ESP_ERR_NOT_FOUND && point.error == sample_error &&
        !point.valid && point.sample_timestamp_us == 0) {
        bsp_display_unlock();
        return {TouchPollStatus::NoReport, sample_error};
    }
    if (sample_error != ESP_OK || !point.valid) {
        bsp_display_unlock();
        return {TouchPollStatus::Failed,
                sample_error != ESP_OK ? sample_error : ESP_ERR_INVALID_RESPONSE};
    }
    bsp_display_unlock();
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
    LUNASHELL_touch_point_t point{};
    const TouchPollResult sample = read_touch_report(point);
    if (sample.status != TouchPollStatus::Reported) {
        bsp_display_unlock();
        return sample;
    }
    const esp_err_t route_error = handle_touch_sample(point);
    bsp_display_unlock();
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
        const int64_t remaining_us = s_next_frame_us - now_us;
        uint32_t wait_ms = kSamplePeriodMs;
        if (parked) {
            wait_ms = kStandbySamplePeriodMs;
        } else if (s_rig_visible) {
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
        if (!LUNASHELL_display_runtime_writes_allowed()) {
            if (!renderer_is_parked()) {
                fail_consumer("Display readiness", ESP_ERR_INVALID_STATE, false);
                break;
            }
            parked = true;
            continue;
        }
        const uint32_t revision = LUNASHELL_display_runtime_content_revision();
        if (!parked && s_rig_visible && esp_timer_get_time() < s_next_frame_us &&
            !s_frame_dirty.load(std::memory_order_acquire) &&
            revision == s_rendered_revision.load(std::memory_order_acquire)) {
            continue;
        }
        if (!parked && !s_rig_visible && !s_frame_dirty.load(std::memory_order_acquire) &&
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
    const esp_err_t cleanup_error = release_worker_strip();
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
    s_response = CODY::make_response_state(CODY::Data::response);
    s_strip = static_cast<uint16_t *>(heap_caps_malloc(
        kStripPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_strip == nullptr) {
        return fail_consumer("Boot strip allocation", ESP_ERR_NO_MEM, false);
    }
    void *rig_storage = heap_caps_malloc(sizeof(CODY::RigScene), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rig_storage == nullptr) {
        release_startup_strip();
        return fail_consumer("Rig scene allocation", ESP_ERR_NO_MEM, false);
    }
    s_rig = new (rig_storage) CODY::RigScene{};
    const DrawResult draw = draw_companion_frame();
    if (draw.error != ESP_OK) {
        release_startup_strip();
        return fail_consumer("Mandatory boot frame", draw.error, false);
    }
    s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
    const TouchPollResult touch = sample_touch();
    if (touch.status == TouchPollStatus::Failed) {
        release_startup_strip();
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
        release_startup_strip();
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

#else

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
