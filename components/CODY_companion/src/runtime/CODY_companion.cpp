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

#include "CODY_companion.h"
#include "CODY_personality.hpp"
#include "CODY_personality_audio.hpp"
#include "CODY_codex_audio.hpp"
#include "CODY_activity_identity.hpp"
#include "CODY_voice_waveform.hpp"
#include "CODY_happiness.hpp"
#include "CODY_connection_personality.hpp"

#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>

#include "CODY2D.hpp"
#include "CODY_rig.hpp"
#include "CODY_debug_hud.hpp"
#include "CODY_debug_views.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_response.hpp"
#include "CODY_character_direction.hpp"
#include "CODY_companion_mode.hpp"
#include "CODY_petting.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_technical_panels.hpp"
#include "CODY_demo.hpp"
#include "CODY_scene.hpp"
#include "CODY_typewriter_audio.hpp"
#include "CODY_onboarding.hpp"
#include "CODY_navigation_visibility.hpp"
#include "CODY_lifecycle.hpp"
#include "CODY_settings.hpp"
#include "CODY_screenshot.hpp"
#include "CODY_capture_sound.hpp"
#include "CODY_boot_cues.h"
#include "CODY_boot_branding.hpp"
#include "VIBEWARE_boot_animation.h"
#include "vibeware_boot_data.h"
#include "CODY_status_overlay.hpp"
#include "scene_data.hpp"
#include "CODY_benchmark.hpp"
#include "CODY_raster_benchmark.hpp"
#include "CODY_jelly_simd.hpp"
#include "runtime_settings.hpp"
#include "cloud_rig_data.hpp"
#include "personality_rules.hpp"
#include "cody_sleep_pearl.hpp"
#include "cody_waking_pearl.hpp"
#include "cody_ready_pearl.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_voice_input.hpp"
#include "LUNASHELL_codex_debug.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_task_affinity.hpp"
#include "LUNASHELL_platform.h"
#include "bsp/display.h"
#include "bsp/runtime_cores.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
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
constexpr bool kRenderDiagnosticsEnabled = LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled;
constexpr bool kExecutionTimingsEnabled = kRenderDiagnosticsEnabled && CODY::RuntimeData::execution_timings;
constexpr bool kRasterBenchmarkEnabled = kExecutionTimingsEnabled &&
    CODY_ENABLE_PEARL_JELLY && CODY::RuntimeData::raster_benchmark_enabled;
// Touch IRQs wake immediately. Release polls only the functional display gate
// at 1 Hz on static/paused routes and in standby, instead of refreshing diagnostics.
constexpr uint32_t kSamplePeriodMs = kRenderDiagnosticsEnabled ? 50U : 1000U;
constexpr uint32_t kStandbySamplePeriodMs = kRenderDiagnosticsEnabled ? 200U : 1000U;
constexpr uint32_t kDisplayLockTimeoutMs = 1000;
// dd9f05d8's Debug worker/frame/coverage call chain alone occupied 8,176 bytes.
// Keep headroom for nested helpers and register spills without spending internal RAM.
constexpr uint32_t kWorkerStackBytes = 12288;
// Keep the continuous frame owner away from the CPU0 radio/IP interrupt and task set.
constexpr BaseType_t kRenderCoreId = LUNASHELL_RENDER_CORE_ID;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
static_assert(kRenderCoreId == LUNASHELL::task_affinity::kCodyRenderCoreId,
              "Shared voice and board renderer core assignments must agree");
#endif
constexpr int64_t kWorkerStackSamplePeriodUs = 2000000;
// Target the requested 25 FPS cadence; the HUD still reports measured throughput.
#if CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY
constexpr uint32_t kRigFramePeriodMs = 40;
#else
constexpr uint32_t kRigFramePeriodMs = 33;
#endif
constexpr uint32_t kPausedReadoutPeriodMs = 1000;
// This is a failure deadline, not an FPS claim; real frame cost still needs measurement.
constexpr uint32_t kFrameBudgetMs = 500;
constexpr uint32_t kLifecyclePreparationTimeoutMs = 3000;
constexpr int64_t kStatusRefreshUs = 1000000;
constexpr int64_t kCaptureStepUs = 1000000;
constexpr int64_t kCaptureFeedbackUs = 2000000;
constexpr int64_t kCaptureActivityRefreshUs = 250000;
constexpr CODY::DamageRect kCaptureOverlayBounds{153, 174, 313, 292};
constexpr uint16_t kWhitesmoke = 0xF7BE; // #F5F5F5 quantized to RGB565.

enum class CaptureVisual : uint8_t { None, Three, Two, One, Saving, Saved, Failed };

enum class StartState : uint32_t {
    Idle,
    Starting,
    Running,
    Failed,
};

using BootPhase = CODY::BootCheckpoint;

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
    bool acquisition_failed = false;
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
TouchPollResult capture_pending_touch(PendingTouchReports &reports);
// sample: Failed acquisition result retaining its original sensor error.
bool optional_runtime_touch_failure(const TouchPollResult &sample);
// error: Original invalid-report error already published by the acquisition owner.
void interrupt_touch_gestures(esp_err_t error);
// reports: Acquired reports in their original order, under the display lock.
esp_err_t dispatch_pending_touch(const PendingTouchReports &reports);

std::atomic<StartState> s_start_state{StartState::Idle};
std::atomic<esp_err_t> s_terminal_error{ESP_OK};
bool s_touch_contact_interrupted = false; // Worker-owned; never a fabricated release.
struct CompanionResources {
    CODY::RigScene *previous_rig = nullptr;
    bool previous_frame_valid = false;
    CODY::RigScene *rig = nullptr;
    CODY::RigDebugStrokeCache *rig_debug_strokes = nullptr;
    CODY::RasterBenchmarkRun *raster_benchmark = nullptr;
};
CompanionResources s_resources;
std::atomic<uint32_t> s_rendered_revision{0};
std::atomic<bool> s_demo_requested{false};
// The startup owner publishes the immutable deadline before releasing this flag.
// Afterwards only the worker retires it; no unsynchronized 64-bit clock updates.
std::atomic<bool> s_greeting_scheduled{false};
int64_t s_greeting_due_us = 0;
// A generation includes its sleep/wake intent. Only complete frames acknowledge it.
std::atomic<uint32_t> s_lifecycle_request{0};
std::atomic<uint32_t> s_lifecycle_acknowledged{0};
std::atomic<uint32_t> s_lifecycle_generation{0};
// This bounded collector shares the display/state lock with completed frame publication.
EXT_RAM_BSS_ATTR CODY::FrameBenchmark s_benchmark;
// Completed-frame geometry and controls share the display lock.
// They are never advanced by a failed frame or a deferred touch event.
bool s_cache_was_rig = false;
bool s_cached_rig_debug = false;
CODY::PointId s_cached_selected_point = CODY::kNoPoint;
CODY::DebugReadout s_cached_readout;
bool s_cached_readout_visible = false;
bool s_cached_navigation_visible = false;
bool s_cached_scene_visible = false;
uint8_t s_cached_navigation_opacity = 0;
CODY::StatusOverlay s_cached_status_overlay;
bool s_cached_status_visible = false;
CODY::ScreenshotState s_cached_screenshot_state = CODY::ScreenshotState::Idle;
CaptureVisual s_cached_capture_visual = CaptureVisual::None;
std::atomic<bool> s_power_screenshot_enabled{LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled};
std::atomic<bool> s_capture_sequence_active{false};
std::atomic<bool> s_capture_request_pending{false};
CODY::SceneFrame s_cached_scene;
CODY::CompanionPanel s_cached_companion_panel = CODY::CompanionPanel::Home;
uint32_t s_cached_panel_revision = 0;
struct CompanionState {
    CODY::CompanionPanel companion_panel = CODY::CompanionPanel::Home;
    CODY::CompanionTapState companion_tap{};
    CODY::CompanionShakeState companion_shake{};
    CODY::PanelPresentation technical_panel{};
    // Serialized renderer-owned projection stays off the nested raster stack.
    LUNASHELL::CodexTechnicalObservation technical_observation{};
    uint64_t panel_activity_revision = 0;
    uint64_t panel_technical_revision = 0;
    int64_t panel_prepared_second = -1;
    uint32_t panel_source_status = 0;
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    LUNASHELL::CodexDebugScreen debug_screen = LUNASHELL::CodexDebugScreen::Normal;
    uint8_t debug_channel_scroll = 0;
    bool debug_placement_dirty = false;
    int16_t debug_scroll_origin_y = 0;
    uint8_t debug_scroll_origin_row = 0;
    bool debug_scroll_contact = false;
    uint16_t debug_scroll_rotation_degrees = 0;
#endif
    CODY::PersonalityCue requested_personality = CODY::PersonalityCue::None;
    CODY::PettingState petting{};
    int64_t affection_remaining_us = 0;
    bool affection_owns_channel = false;
    CODY::PersonalityCue eye_reaction = CODY::PersonalityCue::None;
    CODY::PersonalityCue last_eye_reaction = CODY::PersonalityCue::None;
    int64_t eye_reaction_remaining_us = 0;
    CODY::PersonalityCue welcome_cue = CODY::PersonalityCue::None;
    int64_t welcome_remaining_us = 0;
    CODY::ConnectionPersonalityState connection_personality{};
    CODY::PersonalityCue connection_cue = CODY::PersonalityCue::None;
    CODY::HappinessState happiness{};
    int64_t happiness_sampled_at_us = 0;
    int64_t last_caress_at_us = 0;
    // Display-lock ownership keeps the bounded projection and transaction off the nested frame stack.
    CODY::CharacterDirectionInputs character_inputs{};
    CODY::CharacterDirectionState character_direction{};
    CODY::CharacterDirectionState next_character_direction{};
    CODY::PersonalityLag personality_lag{};
    CODY::PersonalityBatteryLatch personality_battery{};
    CODY::BatteryNoticeState battery_notice{};
    CODY2D::TextLine battery_notice_line{};
    CODY::BatteryNotice retired_battery_notice = CODY::BatteryNotice::None;
    // Shared canonical status projection is serialized by the display owner, outside nested frame stacks.
    LUNASHELL::DeviceStatusObservation personality_observation{};
    static_assert(sizeof(LUNASHELL::DeviceStatusObservation) <= 1024,
        "Personality status must remain a bounded display-owned canonical projection");
    CODY::PersonalityFrame retired_personality{};
    CODY::PersonalityPresentationStatus personality_status = CODY::PersonalityPresentationStatus::Hidden;
    int64_t personality_delta_us = 0;
    bool personality_was_sleep = false;
    bool personality_release_sleep = false;
    // After startup, non-atomic changes use the display lock. Only the worker reads its schedule outside it.
    BootPhase boot_phase = BootPhase::Sleeping;
    CODY::OnboardingSettings onboarding{};
    CODY::SceneFrame scene{};
    bool resume_scene_pending = false;
    bool animation_paused_before_standby = false;
    bool rig_debug_before_standby = false;
#if CONFIG_LUNASHELL_CODY_DIAGNOSTICS_READOUT
    bool debug_readout_enabled = kRenderDiagnosticsEnabled;
#else
    bool debug_readout_enabled = false;
#endif
    bool rig_visible = false;
    bool rig_debug = false;
    bool raster_benchmark_active = false;
    bool raster_benchmark_attempted = false;
    bool raster_benchmark_resume_greeting = false;
    uint32_t raster_benchmark_revision = 0;
    const char *raster_benchmark_previous_skin = nullptr;
    CODY::JellyRasterKernel raster_benchmark_previous_kernel = CODY::JellyRasterKernel::Scalar;
    bool animation_paused = false;
    CODY::NavigationVisibility navigation_visibility{};
    CODY::StatusOverlay status_overlay{};
    uint8_t capture_digit = 0;
    int64_t capture_digit_shown_at_us = 0;
    int64_t capture_activity_at_us = 0;
    int64_t capture_feedback_shown_at_us = 0;
    bool capture_clear_pending = false;
    bool capture_feedback_visible = false;
    esp_err_t capture_request_error = ESP_OK;
    int64_t status_sampled_at_us = 0;
#if CONFIG_LUNASHELL_CODY_FACE_FOLLOWS_BODY
    bool face_follows_body = true;
#else
    bool face_follows_body = false;
#endif
    bool touch_pressed = false;
    bool setup_hold_candidate = false;
    bool setup_hold_opened = false;
    bool setup_hold_eligible = false;
    LUNASHELL_touch_point_t setup_hold_origin{};
    int64_t setup_hold_last_sample_us = 0;
    uint16_t setup_hold_rotation_degrees = 0;
    bool navigation_touch_active = false;
    bool body_drag_active = false;
    LUNASHELL::CharacterTouchSnapshot body_drag{};
    uint16_t rendered_rotation_degrees = 0;
    CODY::PointId selected_point = CODY::kNoPoint;
    std::atomic<bool> frame_dirty{false};
    int64_t animation_elapsed_us = 0;
    int64_t animation_sampled_us = 0;
    int64_t onboarding_elapsed_us = 0;
    int64_t onboarding_sampled_us = 0;
    CODY::ContourState contour{};
    CODY::Channels face{};
    CODY::LifecycleState lifecycle{};
    uint32_t lifecycle_request = 0;
    bool deep_sleep_wake = false;
    bool branding_complete = false;
    uint16_t branding_frame = 0;
    int64_t branding_frame_due_us = 0;
    CODY::BootBranding::Presentation branding_presentation{};
    CODY::BootBranding::Scratch branding_scratch{};
    CODY::ExpressionId requested_expression = CODY::ExpressionId::Calm;
    bool expression_override = false;
    bool greeting_entered = false;
    bool routine_greeting_pending = true;
    bool routine_scene = false;
    LUNASHELL::RoutinePeriod routine_period = LUNASHELL::RoutinePeriod::Unavailable;
    // Native text borrows this display-owned buffer until the whole frame retires.
    char routine_user_name[25]{};
    CODY2D::TextLine routine_user_name_line{};
    uint32_t routine_user_name_revision = 0;
    uint32_t routine_user_name_content_revision = 0;
    esp_err_t routine_user_name_error = ESP_OK;
    bool routine_user_name_available = false;
    CODY::ResponseState response{};
    CODY::ResponseFrame response_frame{};
    bool response_sources_logged = false;
    int64_t next_frame_us = 0;
    int64_t last_timing_log_us = 0;
    uint32_t rig_frame_count = 0;
    CODY::FrameRateMeasurement frame_rate{};
};
// Preserve nonzero defaults without placing initialized data in the external BSS.
// These are task-only display-lock users; IRQs never access the companion model.
alignas(CompanionState) EXT_RAM_BSS_ATTR uint8_t s_state_storage[sizeof(CompanionState)]{};
CompanionState &s_state = *new (s_state_storage) CompanionState{};

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// Prepared lines borrow these PSRAM-owned strings across every tile of one frame.
EXT_RAM_BSS_ATTR CODY::DebugChannelList s_debug_channel_list;
#endif

// No parameters: overlay permission follows the same active lifecycle and logical display as normal Cody.
bool debug_view_active()
{
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    return s_state.debug_screen != LUNASHELL::CodexDebugScreen::Normal &&
        s_state.rig_visible && s_state.branding_complete && s_state.boot_phase == BootPhase::Ready &&
        CODY::lifecycle_is_resting(s_state.lifecycle) && !s_state.resume_scene_pending;
#else
    return false;
#endif
}

// No parameters: the channel list uses current composed values and owns no character raster.
bool debug_channel_view_active()
{
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    return debug_view_active() && s_state.debug_screen == LUNASHELL::CodexDebugScreen::FaceChannels;
#else
    return false;
#endif
}

// preserve_visible_handoff: Keep the current auto heart only for a visible semantic Sleep fade.
void cancel_transient_affection(bool preserve_visible_handoff = false)
{
    CODY::cancel_petting(s_state.petting);
    s_state.affection_remaining_us = 0;
    s_state.eye_reaction_remaining_us = 0;
    s_state.welcome_cue = CODY::PersonalityCue::None;
    s_state.welcome_remaining_us = 0;
    if (!preserve_visible_handoff && s_state.eye_reaction != CODY::PersonalityCue::None &&
        s_state.personality_lag.cue == s_state.eye_reaction) {
        CODY::clear_personality_presentation(s_state.personality_lag);
    }
    s_state.eye_reaction = CODY::PersonalityCue::None;
    if (preserve_visible_handoff) {
        // Sleep owns the new target. Retain current-channel ownership so an actual
        // park, pause or orientation boundary can still discard an unfinished heart.
        return;
    }
    if (s_state.affection_owns_channel &&
        s_state.personality_lag.cue == CODY::PersonalityCue::Affection) {
        // A hidden/parked gesture must not replay its retained dwell on the next route.
        // The completed-frame dirty planner still owns erasing the old footprint.
        CODY::clear_personality_presentation(s_state.personality_lag);
    }
    s_state.affection_owns_channel = false;
}

// component: Canonical publication whose validity is independent of narrative animation.
bool personality_sample_current(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready && component.last_error == ESP_OK &&
        component.data_valid && !component.data_stale && component.last_success_at_us > 0;
}

// observation: One coherent canonical publication shared by desired and displayed cue admission.
// requested: Device cue request; narrative requests pass through unchanged.
// cue: Receives None unless the fresh canonical observation actually supports the symbol.
// Exception: Returns the original invalid canonical value error.
esp_err_t admit_personality_device_cue(const LUNASHELL::DeviceStatusObservation &observation,
                                      CODY::PersonalityCue requested, CODY::PersonalityCue &cue)
{
    cue = requested;
    if (!CODY::personality_cue_is_device(requested)) {
        return ESP_OK;
    }
    cue = CODY::PersonalityCue::None;
    if (requested == CODY::PersonalityCue::NoWifi) {
        if (personality_sample_current(observation.wifi) && observation.wifi.started &&
            observation.wifi.credentials_configured && !observation.wifi.connected) {
            cue = requested;
        }
        return ESP_OK;
    }
    const auto &battery = observation.battery;
    if (!personality_sample_current(battery) || !battery.battery_present) {
        return ESP_OK;
    }
    if (requested == CODY::PersonalityCue::Charging) {
        // Charging is independent of whether a charge percentage was measured.
        if (std::strcmp(battery.power_state, "charging") == 0) {
            cue = requested;
        } else if (std::strcmp(battery.power_state, "discharging") != 0 &&
                   std::strcmp(battery.power_state, "standby") != 0) {
            ESP_LOGE(kTag, "Personality charge state='%s' rejected (%s)",
                battery.power_state, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else if (s_state.personality_battery.low_battery_active) {
        cue = requested;
    }
    return ESP_OK;
}

// requested: Latest desired symbol, after scene and semantic sleep selection.
// displayed: Symbol owned by the channel, including a zero-opacity fade-in entry.
// desired: Receives the currently admitted request.
// invalidate_current: Receives true when a displayed device fact no longer has canonical support.
// Exception: Returns the original canonical read or value-contract failure.
esp_err_t resolve_personality_device_cues(CODY::PersonalityCue requested,
                                         CODY::PersonalityCue displayed,
                                         CODY::PersonalityCue &desired, bool &invalidate_current)
{
    desired = requested;
    invalidate_current = false;
    auto &observation = s_state.personality_observation;
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readDeviceStatusObservation(observation);
    if (error != ESP_OK) {
        s_state.personality_battery.low_battery_active = false;
        ESP_LOGE(kTag, "Personality canonical observation failed (%s)", esp_err_to_name(error));
        return error;
    }
    const auto &battery = observation.battery;
    CODY::PersonalityPowerObservation power{};
    power.current = personality_sample_current(battery);
    power.battery_present = battery.battery_present;
    power.percentage_valid = battery.level_percent_valid;
    power.level_percent = battery.level_percent;
    power.charging = std::strcmp(battery.power_state, "charging") == 0;
    if (power.current && power.battery_present && !power.charging &&
        std::strcmp(battery.power_state, "discharging") != 0 &&
        std::strcmp(battery.power_state, "standby") != 0) {
        ESP_LOGE(kTag, "Battery notice power state='%s' rejected (%s)",
            battery.power_state, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    CODY::PersonalityCue automatic = CODY::PersonalityCue::None;
    error = CODY::select_personality_power_cue(power, s_state.personality_battery, automatic);
    if (error != ESP_OK) {
        return error;
    }
    const bool idle = s_state.lifecycle.phase == CODY::LifecyclePhase::Idle &&
        s_state.companion_panel == CODY::CompanionPanel::Home &&
        (!s_state.scene.valid || s_state.routine_scene ||
         s_state.scene.definition->id == CODY::SceneId::CompanionIdle) &&
        !s_state.resume_scene_pending && !s_state.touch_pressed && !s_state.animation_paused &&
        !debug_view_active() && !s_state.rig_debug &&
        s_state.affection_remaining_us == 0 && s_state.eye_reaction_remaining_us == 0 &&
        requested == CODY::PersonalityCue::None;
    // A notice's dwell begins only after its caption and manga reached a retired frame.
    const int64_t notice_delta_us = s_state.battery_notice.active != CODY::BatteryNotice::None &&
        s_state.retired_battery_notice == CODY::BatteryNotice::None ? 0 : s_state.personality_delta_us;
    CODY::advance_battery_notice(power, idle, notice_delta_us, s_state.battery_notice);
    s_state.battery_notice_line = {};
    if (s_state.battery_notice.active != CODY::BatteryNotice::None) {
        const char *text = CODY::battery_notice_text(s_state.battery_notice.active);
        const auto &font = CODY::SceneData::body_font;
        const uint16_t length = static_cast<uint16_t>(std::strlen(text));
        const uint16_t width = static_cast<uint16_t>((length - 1U) * font.advance + font.width);
        if (width > 320 || font.height > 35) {
            ESP_LOGE(kTag, "Battery notice caption exceeds safe bounds (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        error = CODY2D::prepare_text_line(font, {text, length},
            {static_cast<int16_t>((kPanelWidth - width) / 2), 345}, 0xF7BE,
            s_state.battery_notice_line);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Battery notice caption preparation failed (%s)", esp_err_to_name(error));
            return error;
        }
        requested = CODY::battery_notice_cue(s_state.battery_notice.active);
    }
    error = admit_personality_device_cue(observation, requested, desired);
    if (error != ESP_OK) {
        return error;
    }
    CODY::PersonalityCue admitted_current = displayed;
    error = admit_personality_device_cue(observation, displayed, admitted_current);
    if (error != ESP_OK) {
        return error;
    }
    // Presentation dwell cannot preserve a stale or disproven device fact.
    invalidate_current = CODY::personality_cue_is_device(displayed) &&
        admitted_current == CODY::PersonalityCue::None;
    // Full-charge Joy also loses support immediately if its observation expires.
    if (displayed == CODY::PersonalityCue::Joy &&
        s_state.retired_battery_notice == CODY::BatteryNotice::Full &&
        s_state.battery_notice.active != CODY::BatteryNotice::Full) {
        invalidate_current = true;
    }
    return ESP_OK;
}

// rig_visible: The current frame really renders the evaluated character.
bool main_companion_view_active(bool rig_visible)
{
    if (debug_view_active() || s_state.companion_panel != CODY::CompanionPanel::Home) {
        return false;
    }
    // The automatic clock greeting is the ordinary idle companion, not an
    // onboarding route. Its retained SceneFrame must not disable interactions.
    const bool routine_main = s_state.routine_scene && s_state.scene.valid &&
        s_state.scene.definition != nullptr && s_state.scene.definition->id == CODY::SceneId::Greeting;
    return rig_visible && CODY::lifecycle_is_resting(s_state.lifecycle) &&
        (!s_state.scene.valid || routine_main) && !s_state.resume_scene_pending;
}

// No parameters: admit only ordinary companion routes, never setup or remote controls.
bool companion_home_route_active()
{
    if (s_state.resume_scene_pending || s_state.boot_phase != BootPhase::Ready ||
        !s_state.rig_visible || debug_view_active() || !CODY::lifecycle_is_resting(s_state.lifecycle)) {
        return false;
    }
    if (!s_state.scene.valid || s_state.routine_scene) {
        return true;
    }
    switch (s_state.scene.definition->id) {
    case CODY::SceneId::Connected:
    case CODY::SceneId::ProjectActive:
    case CODY::SceneId::Working:
    case CODY::SceneId::ResponseReady:
    case CODY::SceneId::SessionPaused:
    case CODY::SceneId::SessionEnded:
    case CODY::SceneId::ConnectionLost:
    case CODY::SceneId::CompanionIdle:
    case CODY::SceneId::ActivityUnavailable:
        return true;
    default:
        return false;
    }
}

// No parameters: stored panel choice survives explicit route preemption without owning hidden touch.
bool technical_panel_view_active()
{
    // Remote questions, approvals, setup and Debug retain their real controls and attention.
    // Completing that explicit route resumes the selected panel without a timeout or remote ACK.
    return s_state.companion_panel != CODY::CompanionPanel::Home && companion_home_route_active();
}

// panel: Local information view; never acknowledges or changes a remote request.
void select_companion_panel(CODY::CompanionPanel panel)
{
    if (s_state.companion_panel == panel) {
        return;
    }
    s_state.companion_panel = panel;
    CODY::cancel_companion_tap(s_state.companion_tap);
    CODY::cancel_petting(s_state.petting);
    s_state.setup_hold_candidate = false;
    s_state.setup_hold_eligible = false;
    s_state.body_drag_active = false;
    s_resources.previous_frame_valid = false;
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Local companion panel selected: %u", static_cast<unsigned>(panel));
}

// No parameters: advance the current local information view after an admitted release.
// Exception: Returns an invalid typed panel identity without changing the route.
esp_err_t advance_local_companion_panel()
{
    CODY::CompanionPanel next = CODY::CompanionPanel::Home;
    const esp_err_t error = CODY::next_companion_panel(s_state.companion_panel, next);
    if (error != ESP_OK) {
        return error;
    }
    select_companion_panel(next);
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// No parameters: only a retired input view owns its shake gesture.
bool voice_input_view_active();
#endif

// now_us: Monotonic frame time used to exclude wake-causing movement.
// resumed: Whether this frame resumes after a display park.
// Exception: Returns the original canonical projection failure; animation stays frozen.
esp_err_t observe_paused_companion_navigation(int64_t now_us, bool resumed)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (voice_input_view_active()) return ESP_OK;
#endif
    if (resumed) {
        CODY::suspend_companion_shake(s_state.companion_shake, now_us);
        CODY::cancel_companion_tap(s_state.companion_tap);
    }
    const bool eligible = companion_home_route_active();
    if (!eligible) {
        CODY::suspend_companion_shake(s_state.companion_shake, now_us);
        return ESP_OK;
    }
    // A bounded canonical projection keeps navigation independent of the artistic Play/Pause clock.
    CODY::ResponseInputs inputs{};
    const esp_err_t error = CODY::observe_response_inputs(inputs);
    if (error != ESP_OK) {
        return error;
    }
    if (CODY::observe_companion_shake(s_state.companion_shake, inputs.navigation_motion,
            esp_timer_get_time(), eligible)) {
        select_companion_panel(CODY::CompanionPanel::Home);
    }
    return ESP_OK;
}

// rig_visible: The current frame really renders the evaluated character.
// Scene permission depends on semantic route/pose identity, independently of character size.
CODY::PersonalityPresentation personality_presentation(bool rig_visible)
{
    if (!rig_visible || debug_view_active()) {
        return CODY::PersonalityPresentation::Disabled;
    }
    if (s_state.lifecycle.phase == CODY::LifecyclePhase::Sleep) {
        return CODY::PersonalityPresentation::Sleep;
    }
    const bool reactive_scene = s_state.scene.valid && s_state.scene.definition != nullptr &&
        CODY::scene_personality_cue(s_state.scene.definition->id) != CODY::PersonalityCue::None;
    if (!main_companion_view_active(rig_visible) && !reactive_scene) {
        return CODY::PersonalityPresentation::Disabled;
    }
    const bool sleeping_pose = s_state.expression_override &&
        s_state.requested_expression == CODY::ExpressionId::Sleeping;
    return sleeping_pose ? CODY::PersonalityPresentation::Sleep : CODY::PersonalityPresentation::Main;
}

// No parameters: use the current route, including boots that skip first-run Greeting 04.
bool measured_readout_route_active()
{
    // Pairing is retained across boots. Its ordinary main view still owns a
    // measurement window even when the onboarding greeting was never entered.
    return s_state.greeting_entered || main_companion_view_active(s_state.rig_visible);
}

// No parameters: derive visible feedback from the independently completed SD write.
CaptureVisual capture_visual()
{
    if (s_state.capture_digit == 3U) return CaptureVisual::Three;
    if (s_state.capture_digit == 2U) return CaptureVisual::Two;
    if (s_state.capture_digit == 1U) return CaptureVisual::One;
    if (!s_state.capture_feedback_visible) return CaptureVisual::None;
    if (s_state.capture_request_error != ESP_OK) return CaptureVisual::Failed;
    switch (CODY::screenshot_state()) {
    case CODY::ScreenshotState::Saving: return CaptureVisual::Saving;
    case CODY::ScreenshotState::Saved: return CaptureVisual::Saved;
    case CODY::ScreenshotState::Failed: return CaptureVisual::Failed;
    case CODY::ScreenshotState::Idle:
    case CODY::ScreenshotState::Capturing: return CaptureVisual::None;
    }
    return CaptureVisual::None;
}

// strip: Current compact display tile in panel byte order.
// first_row: Logical top of the tile.
// first_column: Logical left of the tile.
// rectangle: Complete-canvas fill bounds, clipped to the tile.
// color: Native RGB565 fill color.
// Exception: Returns the actual raster error.
esp_err_t fill_capture_rect(CODY2D::Surface strip, uint16_t first_row,
                            uint16_t first_column, CODY2D::Rect rectangle,
                            uint16_t color)
{
    const uint16_t left = std::max<uint16_t>(first_column, rectangle.x);
    const uint16_t top = std::max<uint16_t>(first_row, rectangle.y);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width,
                                             rectangle.x + rectangle.width);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height,
                                              rectangle.y + rectangle.height);
    if (left >= right || top >= bottom) {
        return ESP_OK;
    }
    return CODY2D::fill_rect(strip,
        {static_cast<uint16_t>(left - first_column),
         static_cast<uint16_t>(top - first_row),
         static_cast<uint16_t>(right - left),
         static_cast<uint16_t>(bottom - top)}, color);
}

// visual: Retired countdown or asynchronous storage state.
// line: Prepared only for one of the three feedback words.
// Exception: Returns the actual text-layout error.
esp_err_t prepare_capture_feedback(CaptureVisual visual, CODY2D::TextLine &line)
{
    const char *label = nullptr;
    uint16_t color = kWhitesmoke;
    switch (visual) {
    case CaptureVisual::Saving: label = "SAVING"; color = 0xF5A9; break;
    case CaptureVisual::Saved: label = "SAVED"; color = 0x26F8; break;
    case CaptureVisual::Failed: label = "FAILED"; color = 0xF9CB; break;
    default: return ESP_OK;
    }
    const uint16_t length = static_cast<uint16_t>(std::strlen(label));
    const auto &font = CODY::SceneData::title_font;
    const uint16_t width = static_cast<uint16_t>((length - 1U) * font.advance + font.width);
    return CODY2D::prepare_text_line(font, {label, length},
        {static_cast<int16_t>((kPanelWidth - width) / 2U), 213}, color, line);
}

// visual: Current countdown digit or truthful storage result.
// feedback_line: Prepared feedback text, or null for a countdown digit.
// strip: Current compact display tile.
// first_row: Logical top of the tile.
// first_column: Logical left of the tile.
// Exception: Returns the actual raster or text error.
esp_err_t draw_capture_overlay(CaptureVisual visual,
                               const CODY2D::TextLine *feedback_line,
                               CODY2D::Surface strip, uint16_t first_row,
                               uint16_t first_column)
{
    if (visual == CaptureVisual::None) {
        return ESP_OK;
    }
    esp_err_t error = fill_capture_rect(strip, first_row, first_column,
        {kCaptureOverlayBounds.left, kCaptureOverlayBounds.top,
         static_cast<uint16_t>(kCaptureOverlayBounds.right - kCaptureOverlayBounds.left),
         static_cast<uint16_t>(kCaptureOverlayBounds.bottom - kCaptureOverlayBounds.top)},
        0x1082);
    if (error != ESP_OK) return error;
    if (feedback_line != nullptr && feedback_line->valid) {
        return CODY2D::draw_text_rows(*feedback_line, feedback_line->text.length,
                                      strip, first_row, first_column);
    }
    constexpr CODY2D::Rect segments[] = {
        {207, 190, 52, 8}, {259, 198, 8, 29}, {259, 234, 8, 29},
        {207, 263, 52, 8}, {199, 234, 8, 29}, {199, 198, 8, 29},
        {207, 226, 52, 8},
    };
    const bool visible[] = {
        visual != CaptureVisual::One,
        true,
        visual != CaptureVisual::Two,
        visual != CaptureVisual::One,
        visual == CaptureVisual::Two,
        false,
        visual != CaptureVisual::One,
    };
    for (size_t index = 0; index < std::size(segments); ++index) {
        if (!visible[index]) continue;
        error = fill_capture_rect(strip, first_row, first_column,
                                  segments[index], kWhitesmoke);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}

// No parameters: advance only after each numeral has a retired visible frame.
// Exception: Returns the real display-lock failure.
esp_err_t advance_capture_timeline()
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) return lock_error;
    const int64_t now_us = esp_timer_get_time();
    if (!LUNASHELL_display_runtime_writes_allowed()) {
        // A real standby/park cancels the visual request. The power owner keeps
        // its own wake and three-second hold behavior.
        s_capture_request_pending.store(false, std::memory_order_release);
        s_capture_sequence_active.store(false, std::memory_order_release);
        s_state.capture_digit = 0;
        s_state.capture_clear_pending = false;
        s_state.capture_feedback_visible = false;
        s_state.capture_feedback_shown_at_us = 0;
        bsp_display_unlock();
        return ESP_OK;
    }
    if (s_capture_request_pending.exchange(false, std::memory_order_acq_rel)) {
        s_state.capture_digit = 3U;
        s_state.capture_digit_shown_at_us = 0;
        s_state.capture_clear_pending = false;
        s_state.capture_feedback_shown_at_us = 0;
        s_state.capture_feedback_visible = false;
        s_state.capture_request_error = ESP_OK;
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    if (s_state.capture_digit != 0U && s_state.capture_digit_shown_at_us > 0 &&
        now_us - s_state.capture_digit_shown_at_us >= kCaptureStepUs) {
        if (s_state.capture_digit == 1U) {
            s_state.capture_digit = 0U;
            s_state.capture_clear_pending = true;
        } else {
            --s_state.capture_digit;
        }
        s_state.capture_digit_shown_at_us = 0;
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    const CaptureVisual visual = capture_visual();
    // Storage completes on another task. Wake even a paused/static scene when
    // its async result changes, otherwise SAVING can remain painted forever.
    if (visual != s_cached_capture_visual) {
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    if (s_state.capture_feedback_visible &&
        (visual == CaptureVisual::Saved || visual == CaptureVisual::Failed) &&
        s_state.capture_feedback_shown_at_us > 0 &&
        now_us - s_state.capture_feedback_shown_at_us >= kCaptureFeedbackUs) {
        s_state.capture_feedback_visible = false;
        s_state.capture_feedback_shown_at_us = 0;
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    if ((s_capture_sequence_active.load(std::memory_order_acquire) ||
         s_state.capture_feedback_visible) &&
        now_us - s_state.capture_activity_at_us >= kCaptureActivityRefreshUs) {
        LUNASHELL_power_manager_note_power_activity();
        s_state.capture_activity_at_us = now_us;
    }
    bsp_display_unlock();
    return ESP_OK;
}

// claimed: Set true when Cody's enabled runtime flag consumes this active PWR tap.
// Exception: Returns the real renderer or capture-admission error without entering standby.
esp_err_t handle_active_power_short_press(bool *claimed)
{
    if (claimed == nullptr) {
        ESP_LOGE(kTag, "PWR consumer output rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if constexpr (!LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
        *claimed = false;
        return ESP_OK;
    }
    *claimed = s_power_screenshot_enabled.load(std::memory_order_acquire);
    if (!*claimed) return ESP_OK;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running || !LUNASHELL_display_runtime_writes_allowed()) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "PWR capture unavailable during renderer transition (%s)",
                 esp_err_to_name(error));
        return error;
    }
    bool available = false;
    if (!s_capture_sequence_active.compare_exchange_strong(
            available, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        ESP_LOGW(kTag, "PWR capture ignored while countdown/capture/save is active");
        return ESP_OK;
    }
    const auto screenshot = CODY::screenshot_state();
    if (screenshot == CODY::ScreenshotState::Capturing ||
        screenshot == CODY::ScreenshotState::Saving) {
        s_capture_sequence_active.store(false, std::memory_order_release);
        ESP_LOGW(kTag, "PWR capture ignored while screenshot owner is busy");
        return ESP_OK;
    }
    s_capture_request_pending.store(true, std::memory_order_release);
    s_state.frame_dirty.store(true, std::memory_order_release);
    LUNASHELL_power_manager_note_power_activity();
    ESP_LOGI(kTag, "PWR capture countdown requested");
    return ESP_OK;
}

struct PausedSceneModel {
    CODY::Channels face{};
    CODY::ResponseState response{};
    CODY::ResponseFrame response_frame{};
    CODY::ContourState contour{};
    const CODY::SceneDefinition *definition = nullptr;
    bool valid = false;
};
// Preserve the compact artistic model, not another PSRAM geometry/framebuffer copy.
alignas(PausedSceneModel) EXT_RAM_BSS_ATTR uint8_t s_paused_model_storage[sizeof(PausedSceneModel)]{};
PausedSceneModel &s_paused_scene_model = *new (s_paused_model_storage) PausedSceneModel{};
const CODY::SceneDefinition *s_standby_scene_definition = nullptr;
const CODY::SceneDefinition *s_typewriter_scene_definition = nullptr;
const char *s_typewriter_title_text = nullptr;
uint32_t s_typewriter_content_generation = 0;
uint32_t s_typewriter_visible_characters = 0;
bool s_typewriter_audio_prepared = false;
bool s_typewriter_audio_suspended = false;
esp_err_t s_optional_typewriter_error = ESP_OK;
uint32_t s_optional_typewriter_repeated_failures = 0;
int64_t s_optional_typewriter_reminder_at_us = 0;
bool s_personality_audio_prepared = false;
bool s_personality_audio_suspended = false;
CODY::PersonalityCue s_pending_personality_sound = CODY::PersonalityCue::None;
int64_t s_pending_personality_sound_until_us = 0;
uint32_t s_pending_personality_sound_sequence = 0;
CODY::IdleSceneId s_retired_idle_scene = CODY::IdleSceneId::None;
uint32_t s_retired_idle_generation = 0;
bool s_idle_whistle_attempted = false;
esp_err_t s_optional_personality_error = ESP_OK;
uint32_t s_optional_personality_repeated_failures = 0;
int64_t s_optional_personality_reminder_at_us = 0;

// error: Actual optional owner failure; dispatch success is not playback recovery.
void log_optional_personality_error(esp_err_t error)
{
    if (error == ESP_OK) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (error != s_optional_personality_error) {
        s_optional_personality_error = error;
        s_optional_personality_repeated_failures = 0;
        s_optional_personality_reminder_at_us = now_us + 60000000LL;
        ESP_LOGE(kTag, "Optional personality audio unavailable (%s)", esp_err_to_name(error));
        return;
    }
    if (s_optional_personality_repeated_failures != UINT32_MAX) {
        ++s_optional_personality_repeated_failures;
    }
    if (now_us >= s_optional_personality_reminder_at_us) {
        ESP_LOGE(kTag, "Optional personality audio still unavailable: %s; repeated_failures=%lu",
            esp_err_to_name(error), static_cast<unsigned long>(s_optional_personality_repeated_failures));
        s_optional_personality_reminder_at_us = now_us + 60000000LL;
    }
}

// suspended: Visibility/power policy; optional audio errors stay owned and published by audio.
void suspend_optional_personality_audio(bool suspended)
{
    if (!s_personality_audio_prepared || suspended == s_personality_audio_suspended) {
        return;
    }
    const esp_err_t error = CODY::set_personality_audio_suspended(suspended);
    if (error == ESP_OK) {
        s_personality_audio_suspended = suspended;
    } else {
        // This optional component retains its real fault; visual rendering may continue.
        log_optional_personality_error(error);
    }
}

// previous: Last fully retired visual cue before this frame.
// current: Newly retired visual cue; busy admission can retry during its 400-ms fade.
// idle_view_visible: The completed frame actually rendered the character in Main.
void notify_completed_personality(const CODY::PersonalityFrame &previous,
                                  const CODY::PersonalityFrame &current, bool idle_view_visible)
{
    const bool visible = current.status == CODY::PersonalityPresentationStatus::Visible &&
        current.opacity != 0;
    const bool idle_whistle_visible = !s_state.animation_paused &&
        s_state.response.idle_scene == CODY::IdleSceneId::Whistle && idle_view_visible;
    suspend_optional_personality_audio(!visible && !idle_whistle_visible);
    const bool appeared = visible && (previous.opacity == 0 || previous.cue != current.cue);
    const int64_t now_us = esp_timer_get_time();
    if (appeared) {
        s_pending_personality_sound = current.cue;
        s_pending_personality_sound_until_us = now_us + 400000;
        s_pending_personality_sound_sequence = 0;
    }
    if (!visible || current.cue != s_pending_personality_sound ||
        now_us >= s_pending_personality_sound_until_us) {
        s_pending_personality_sound = CODY::PersonalityCue::None;
    }
    if (!s_personality_audio_prepared || s_pending_personality_sound == CODY::PersonalityCue::None) {
        return;
    }
    CODY::PersonalityAudioStatus status{};
    if (s_pending_personality_sound_sequence != 0) {
        const esp_err_t observation_error = CODY::get_personality_audio_status(status);
        if (observation_error != ESP_OK) {
            log_optional_personality_error(observation_error);
            s_pending_personality_sound = CODY::PersonalityCue::None;
            return;
        }
        if (status.request_sequence != s_pending_personality_sound_sequence || status.cue != current.cue) {
            // Another explicit cue owns the shared mailbox; never replay a displaced request.
            s_pending_personality_sound = CODY::PersonalityCue::None;
            return;
        }
        if (status.state == CODY::PersonalityAudioState::Dispatched) {
            return;
        }
        if (status.state != CODY::PersonalityAudioState::DroppedBusy) {
            s_pending_personality_sound = CODY::PersonalityCue::None;
            return;
        }
        s_pending_personality_sound_sequence = 0;
    }
    const esp_err_t error = CODY::notify_personality_audio(current.cue, status);
    const bool retry_busy = error == ESP_OK && status.state == CODY::PersonalityAudioState::DroppedBusy;
    if (!retry_busy || appeared) {
        ESP_LOGI(kTag, "Personality sound: cue=%u disposition=%u sequence=%lu error=%s",
            static_cast<unsigned>(current.cue), static_cast<unsigned>(status.state),
            static_cast<unsigned long>(status.request_sequence), esp_err_to_name(error));
    }
    if (error == ESP_OK && status.state == CODY::PersonalityAudioState::Dispatched) {
        s_pending_personality_sound_sequence = status.request_sequence;
    } else if (!retry_busy) {
        s_pending_personality_sound = CODY::PersonalityCue::None;
    }
    if (error != ESP_OK) {
        log_optional_personality_error(error);
    }
    // Keep only the current visible symbol for a bounded admission retry. A dispatched
    // clip is never repeated, and changing/hiding the icon cancels its pending sound.
}

// visible: This completed frame actually presented the rig in the ordinary Idle view.
void notify_completed_idle_scene(bool visible)
{
    const CODY::IdleSceneId current = visible && !s_state.animation_paused
        ? s_state.response.idle_scene : CODY::IdleSceneId::None;
    if (current != s_retired_idle_scene) {
        s_retired_idle_scene = current;
        ESP_LOGI(kTag, "Idle scene retired: identity=%lu", static_cast<unsigned long>(current));
    }
    if (s_state.response.idle_scene_generation != s_retired_idle_generation) {
        s_retired_idle_generation = s_state.response.idle_scene_generation;
        s_idle_whistle_attempted = false;
    }
    // Request once only when the pursed mouth is visible, never on an evaluated
    // or partially transferred frame. A busy/suppressed clip is not replayed.
    if (!s_personality_audio_prepared || s_idle_whistle_attempted ||
        current != CODY::IdleSceneId::Whistle || s_state.response.idle_scene_weight < .8f) {
        return;
    }
    s_idle_whistle_attempted = true;
    CODY::PersonalityAudioStatus status{};
    const esp_err_t error = CODY::notify_idle_whistle_audio(status);
    ESP_LOGI(kTag, "Idle whistle: disposition=%u sequence=%lu error=%s",
        static_cast<unsigned>(status.state), static_cast<unsigned long>(status.request_sequence),
        esp_err_to_name(error));
    if (error != ESP_OK) {
        log_optional_personality_error(error);
    }
}

// error: Actual audio admission/result; successful no-op frames never report recovery.
void log_optional_typewriter_result(esp_err_t error)
{
    if (error == ESP_OK) {
        if (s_optional_typewriter_error != ESP_OK) {
            ESP_LOGI(kTag, "Typewriter audio recovered after %lu repeated failures",
                     static_cast<unsigned long>(s_optional_typewriter_repeated_failures));
            s_optional_typewriter_error = ESP_OK;
            s_optional_typewriter_repeated_failures = 0;
            s_optional_typewriter_reminder_at_us = 0;
        }
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (error != s_optional_typewriter_error) {
        s_optional_typewriter_error = error;
        s_optional_typewriter_repeated_failures = 0;
        s_optional_typewriter_reminder_at_us = now_us + 60000000LL;
        ESP_LOGE(kTag, "Completed typewriter audio unavailable (%s)", esp_err_to_name(error));
        return;
    }
    if (s_optional_typewriter_repeated_failures != UINT32_MAX) {
        ++s_optional_typewriter_repeated_failures;
    }
    if (now_us >= s_optional_typewriter_reminder_at_us) {
        ESP_LOGE(kTag, "Completed typewriter audio still unavailable: %s; repeated_failures=%lu",
                 esp_err_to_name(error), static_cast<unsigned long>(s_optional_typewriter_repeated_failures));
        s_optional_typewriter_reminder_at_us = now_us + 60000000LL;
    }
}

// No parameters: restore the paused model under the display lock before enabling pause.
// Exception: Returns the real missing-model, placement or geometry error.
esp_err_t restore_paused_scene_model()
{
    if (!s_state.animation_paused_before_standby) {
        return ESP_OK;
    }
    if (!s_paused_scene_model.valid || s_resources.rig == nullptr) {
        ESP_LOGE(kTag, "Paused wake model is unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    CODY::ResponseFrame restored_frame = s_paused_scene_model.response_frame;
    const CODY::SceneDefinition *definition = s_state.scene.valid ? s_state.scene.definition : nullptr;
    const bool same_scene = definition == s_paused_scene_model.definition;
    if (!same_scene && definition != nullptr) {
        // The canonical backend can close pairing during standby. Preserve the
        // paused shape, but use the real replacement route's face and placement.
        const esp_err_t placement_error = CODY::scene_cloud_transform(s_state.scene, restored_frame.transform);
        if (placement_error != ESP_OK) {
            return placement_error;
        }
        restored_frame.face = definition->face;
    }
    const esp_err_t geometry_error = CODY::evaluate_rig(restored_frame.face, restored_frame.transform,
        *s_resources.rig, &s_paused_scene_model.contour, s_state.face_follows_body);
    if (geometry_error != ESP_OK) {
        return geometry_error;
    }
    s_state.face = same_scene ? s_paused_scene_model.face : restored_frame.face;
    s_state.response = s_paused_scene_model.response;
    CODY::rebase_response_motion(s_state.response);
    s_state.response_frame = restored_frame;
    s_state.contour = s_paused_scene_model.contour;
    return ESP_OK;
}

// scene: Next scene selected under the display/state lock; empty means inherit settings.
// Exception: Returns the real scene-definition or power-owner admission error.
esp_err_t apply_scene_standby_policy(const CODY::SceneFrame &scene)
{
    if (scene.valid && scene.definition == nullptr) {
        ESP_LOGE(kTag, "Scene standby policy has no definition (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const CODY::SceneDefinition *definition = scene.valid ? scene.definition : nullptr;
    if (definition == s_standby_scene_definition) {
        return ESP_OK;
    }
    const uint32_t seconds = definition == nullptr ? 0 : definition->standby_override_seconds;
    const uint32_t previous_seconds = s_standby_scene_definition == nullptr
        ? 0 : s_standby_scene_definition->standby_override_seconds;
    // Initial inherited scenes need no power request before the startup owner
    // starts the service. Each positive scene entry gets exactly one fresh anchor.
    if (seconds != 0 || previous_seconds != 0) {
        const esp_err_t error = LUNASHELL_power_manager_set_scene_standby_override(seconds);
        if (error != ESP_OK) {
            return error;
        }
    }
    s_standby_scene_definition = definition;
    return ESP_OK;
}

// completed: True only after the entire measurement interval and successful final summary.
// reason: Explicit end/interrupt source, retained in the diagnostic log.
// Exception: Returns policy/skin/kernel restoration or verification errors without claiming completion.
esp_err_t stop_raster_benchmark(bool completed, const char *reason)
{
    if (!s_state.raster_benchmark_active) {
        return ESP_OK;
    }
    if (!completed) {
        s_resources.raster_benchmark->interrupt();
    }
    const esp_err_t policy_error = LUNASHELL_power_manager_set_scene_standby_override(0);
    if (policy_error != ESP_OK) {
        ESP_LOGE(kTag, "BENCH policy restore failed: reason=%s (%s)", reason, esp_err_to_name(policy_error));
        return policy_error;
    }
#if CODY_ENABLE_PEARL_JELLY
    const esp_err_t skin_error = CODY::select_jelly_skin(s_state.raster_benchmark_previous_skin);
    if (skin_error != ESP_OK) {
        ESP_LOGE(kTag, "BENCH skin restore failed: reason=%s (%s)", reason, esp_err_to_name(skin_error));
        return skin_error;
    }
    const esp_err_t kernel_error = CODY::select_jelly_raster_kernel(s_state.raster_benchmark_previous_kernel);
    if (kernel_error != ESP_OK) {
        return kernel_error;
    }
#endif
    s_state.raster_benchmark_active = false;
    s_state.raster_benchmark_previous_skin = nullptr;
    if (s_state.raster_benchmark_resume_greeting) {
        s_greeting_scheduled.store(true, std::memory_order_release);
        s_state.raster_benchmark_resume_greeting = false;
    }
    s_state.animation_sampled_us = 0;
    s_state.frame_rate = {};
    s_benchmark.reset();
    s_state.frame_dirty.store(true, std::memory_order_release);
    s_resources.previous_frame_valid = false;
    const auto &summary = s_resources.raster_benchmark->summary();
    if (!completed) {
        ESP_LOGW(kTag, "BENCH interrupted reason=%s n=%lu; no completed baseline",
                 reason, static_cast<unsigned long>(summary.frames));
        return ESP_OK;
    }
    if (summary.kernel == CODY::JellyRasterKernel::SimdVerify) {
        if (summary.verified_samples == 0 || summary.vectorized_pixels == 0) {
            ESP_LOGE(kTag, "BENCH verification rejected: no compared SIMD samples (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(kTag, "BENCH verification complete n=%lu compared=%llu vectorized=%llu tail=%llu zero=%llu; timings excluded",
                 static_cast<unsigned long>(summary.frames),
                 static_cast<unsigned long long>(summary.verified_samples),
                 static_cast<unsigned long long>(summary.vectorized_pixels),
                 static_cast<unsigned long long>(summary.scalar_tail_pixels),
                 static_cast<unsigned long long>(summary.zero_footprint_pixels));
        return ESP_OK;
    }
    ESP_LOGI(kTag, "BENCH complete n=%lu window_us=%lld pixels=%lu budget_us=%lu missed=%lu",
             static_cast<unsigned long>(summary.frames),
             static_cast<long long>(CODY::RasterBenchmarkRun::kMeasurementUs),
             static_cast<unsigned long>(summary.pixels_per_frame),
             static_cast<unsigned long>(CODY::RasterBenchmarkRun::kBudgetUs),
             static_cast<unsigned long>(summary.missed));
    ESP_LOGI(kTag, "BENCH frame_us mean=%llu p95=%lu max=%lu geo=%llu raster=%llu transfer=%llu",
             static_cast<unsigned long long>(summary.total_us / summary.frames),
             static_cast<unsigned long>(summary.p95_us), static_cast<unsigned long>(summary.maximum_us),
             static_cast<unsigned long long>(summary.geometry_us / summary.frames),
             static_cast<unsigned long long>(summary.raster_us / summary.frames),
             static_cast<unsigned long long>(summary.transfer_us / summary.frames));
    ESP_LOGI(kTag, "BENCH artwork_us jelly_mean=%llu vector_mean=%llu detail=%u",
             static_cast<unsigned long long>(summary.jelly_us / summary.frames),
             static_cast<unsigned long long>(summary.vector_us / summary.frames),
             static_cast<unsigned>(CODY::RuntimeData::raster_benchmark_profile_blocks));
    if (CODY::RuntimeData::raster_benchmark_profile_blocks) {
        ESP_LOGI(kTag, "BENCH detail prep_us=%llu interp_us=%llu compose_us=%llu sampled=%llu written=%llu",
                 static_cast<unsigned long long>(summary.preparation_us),
                 static_cast<unsigned long long>(summary.interpolation_us),
                 static_cast<unsigned long long>(summary.composition_us),
                 static_cast<unsigned long long>(summary.sampled_pixels),
                 static_cast<unsigned long long>(summary.composited_pixels));
    }
    if (summary.kernel == CODY::JellyRasterKernel::Simd) {
        ESP_LOGI(kTag, "BENCH SIMD vectorized=%llu tail=%llu zero=%llu coverage=%llu; packing included in frame/jelly time",
                 static_cast<unsigned long long>(summary.vectorized_pixels),
                 static_cast<unsigned long long>(summary.scalar_tail_pixels),
                 static_cast<unsigned long long>(summary.zero_footprint_pixels),
                 static_cast<unsigned long long>(summary.coverage_skipped_pixels));
    }
    return ESP_OK;
}

// No parameters: enter once after boot animations settle, under the shared display lock.
// Exception: Returns real profile, policy, skin, kernel verification, clock or allocation-state errors.
esp_err_t begin_raster_benchmark()
{
    if (!kRasterBenchmarkEnabled || s_resources.raster_benchmark == nullptr ||
        !CODY::lifecycle_is_resting(s_state.lifecycle) || s_state.raster_benchmark_attempted) {
        ESP_LOGE(kTag, "BENCH entry state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_state.raster_benchmark_attempted = true;
    esp_err_t error = apply_scene_standby_policy({});
    if (error != ESP_OK) {
        return error;
    }
#if CODY_ENABLE_PEARL_JELLY
    CODY::JellySkinSelection previous;
    error = CODY::selected_jelly_skin(previous);
    if (error != ESP_OK) {
        return error;
    }
    s_state.raster_benchmark_previous_skin = previous.id;
    s_state.raster_benchmark_previous_kernel = CODY::selected_jelly_raster_kernel();
#endif
    error = LUNASHELL_power_manager_set_scene_standby_override(
        CODY::RasterBenchmarkRun::kStandbyOverrideSeconds);
    if (error != ESP_OK) {
        return error;
    }
    // From this point the ordinary failure cleanup owns policy and skin restoration.
    s_state.raster_benchmark_active = true;
#if CODY_ENABLE_PEARL_JELLY
    error = CODY::select_jelly_skin("cody.skin.pearl_mist.v1");
    if (error != ESP_OK) {
        return error;
    }
    error = CODY::select_jelly_raster_kernel(CODY::RuntimeData::raster_benchmark_kernel);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(kTag, "BENCH kernel=%s; mode=%s",
             CODY::jelly_raster_kernel_name(CODY::RuntimeData::raster_benchmark_kernel),
             CODY::RuntimeData::raster_benchmark_kernel == CODY::JellyRasterKernel::SimdVerify
                 ? "pixel-verification-no-performance-result" : "timing");
    if (CODY::RuntimeData::raster_benchmark_kernel == CODY::JellyRasterKernel::SimdVerify) {
        error = CODY::verify_jelly_simd_kernel();
        if (error != ESP_OK) {
            return error;
        }
    }
#endif
    error = s_resources.raster_benchmark->begin(esp_timer_get_time());
    if (error != ESP_OK) {
        return error;
    }
    s_state.raster_benchmark_resume_greeting = s_greeting_scheduled.exchange(false, std::memory_order_acq_rel);
    s_demo_requested.store(false, std::memory_order_release);
    s_state.scene = {};
    s_state.resume_scene_pending = false;
    s_state.routine_greeting_pending = false;
    s_state.routine_scene = false;
    s_state.rig_visible = true;
    s_state.rig_debug = false;
    s_state.animation_paused = false;
    s_state.animation_elapsed_us = 0;
    s_state.animation_sampled_us = 0;
    s_state.rig_frame_count = 0;
    s_state.last_timing_log_us = 0;
    s_state.frame_rate = {};
    s_benchmark.reset();
    s_resources.previous_frame_valid = false;
    s_state.raster_benchmark_revision = LUNASHELL_display_runtime_content_revision();
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "BENCH begin pose=ready scale=%u skin=pearl_mist detail=%u core=1 revision=%lu warmup_s=5 measure_s=40 face=follow",
             static_cast<unsigned>(CODY::RuntimeData::raster_benchmark_cloud_scale_percent),
             static_cast<unsigned>(CODY::RuntimeData::raster_benchmark_profile_blocks),
             static_cast<unsigned long>(s_state.raster_benchmark_revision));
    return ESP_OK;
}

// suspended: Clear queued clicks while parked; resuming primes a fresh displayed baseline.
// Exception: Returns the audio owner's real suspension error for an admitted feature.
esp_err_t set_companion_typewriter_suspended(bool suspended)
{
    if (!s_typewriter_audio_prepared || suspended == s_typewriter_audio_suspended) {
        return ESP_OK;
    }
    const esp_err_t error = CODY::set_typewriter_audio_suspended(suspended);
    if (error != ESP_OK) {
        return error;
    }
    s_typewriter_audio_suspended = suspended;
    s_typewriter_scene_definition = nullptr;
    s_typewriter_title_text = nullptr;
    s_typewriter_content_generation = 0;
    s_typewriter_visible_characters = 0;
    return ESP_OK;
}

// scene: Immutable scene prefix from a completely retired panel frame.
// presentation: Actual content visibility used to draw that same frame.
// Exception: Returns the real audio admission or retained asynchronous error.
esp_err_t notify_completed_typewriter(const CODY::SceneFrame &scene,
                                     const CODY::ScenePresentation &presentation)
{
    if (!s_typewriter_audio_prepared) {
        return ESP_OK;
    }
    if (!scene.valid || !presentation.content_visible || s_typewriter_audio_suspended) {
        if (s_typewriter_scene_definition != nullptr && !s_typewriter_audio_suspended) {
            esp_err_t error = set_companion_typewriter_suspended(true);
            if (error == ESP_OK) {
                error = set_companion_typewriter_suspended(false);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
        s_typewriter_scene_definition = nullptr;
        s_typewriter_title_text = nullptr;
        s_typewriter_content_generation = 0;
        s_typewriter_visible_characters = 0;
        return ESP_OK;
    }
    const uint32_t visible = (scene.definition->title_visible ? scene.visible_characters : 0U) +
        static_cast<uint32_t>(scene.content.visible_characters);
    if (s_typewriter_scene_definition != scene.definition ||
        s_typewriter_title_text != scene.title.text.characters ||
        s_typewriter_content_generation != scene.content.text_generation ||
        visible < s_typewriter_visible_characters) {
        esp_err_t error = set_companion_typewriter_suspended(true);
        if (error == ESP_OK) {
            error = set_companion_typewriter_suspended(false);
        }
        if (error != ESP_OK) {
            return error;
        }
        // New scenes and rewound text prime the baseline; already-visible text
        // never becomes a burst of clicks after wake, navigation or a redraw.
        s_typewriter_scene_definition = scene.definition;
        s_typewriter_title_text = scene.title.text.characters;
        s_typewriter_content_generation = scene.content.text_generation;
        s_typewriter_visible_characters = visible;
        return ESP_OK;
    }
    const uint32_t newly_visible = visible - s_typewriter_visible_characters;
    if (newly_visible != 0) {
        const esp_err_t error = CODY::notify_typewriter_audio(newly_visible);
        s_typewriter_visible_characters = visible;
        if (error != ESP_OK) {
            return error;
        }
        log_optional_typewriter_result(ESP_OK);
    }
    return ESP_OK;
}

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

// sampled_at_us: Current monotonic owner time, independent of animation/frame clocks.
// awake: False while power ownership parks Cody; only the clock baseline is then discarded.
// Exception: Returns invalid monotonic time or the original happiness model failure.
esp_err_t advance_happiness_clock_locked(int64_t sampled_at_us, bool awake)
{
    if (sampled_at_us <= 0 ||
        (s_state.happiness_sampled_at_us != 0 && sampled_at_us < s_state.happiness_sampled_at_us)) {
        ESP_LOGE(kTag, "Happiness clock rejected: now_us=%lld previous_us=%lld (%s)",
                 static_cast<long long>(sampled_at_us),
                 static_cast<long long>(s_state.happiness_sampled_at_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!awake) {
        s_state.happiness_sampled_at_us = 0;
        return ESP_OK;
    }
    const int64_t elapsed_us = s_state.happiness_sampled_at_us == 0
        ? 0 : sampled_at_us - s_state.happiness_sampled_at_us;
    const uint16_t previous_happiness = s_state.happiness.happiness;
    const esp_err_t error = CODY::advance_happiness(CODY::PersonalityData::happiness,
                                                  elapsed_us, s_state.happiness);
    if (error != ESP_OK) {
        return error;
    }
    s_state.happiness_sampled_at_us = sampled_at_us;
    if (s_state.happiness.happiness != previous_happiness) {
        ESP_LOGI(kTag, "Mood decay: happiness=%u sadness=%u",
            static_cast<unsigned>(s_state.happiness.happiness),
            static_cast<unsigned>(CODY::PersonalityData::happiness.maximum - s_state.happiness.happiness));
    }
    return ESP_OK;
}

// force_rebase: True after an actual parked draw, before a later wake can accumulate elapsed time.
// Exception: Returns the real display lock, monotonic clock or character-state model error.
esp_err_t update_worker_happiness(bool force_rebase = false)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Happiness owner lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const bool awake = !force_rebase && LUNASHELL_display_runtime_writes_allowed() &&
        s_state.lifecycle.phase != CODY::LifecyclePhase::Sleep;
    return advance_happiness_clock_locked(esp_timer_get_time(), awake);
}

// cue: Already validated typed identity for a single narrative or device request.
// Exception: Returns the real consumer-state or display lock failure.
esp_err_t set_personality_request(CODY::PersonalityCue cue)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Personality request lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Personality request state rejected (%s)", esp_err_to_name(error));
        return error;
    }
    s_state.requested_personality = cue;
    // Priority arbitration follows canonical admission. An unavailable device request
    // cannot cancel an artistic reaction merely because its raw identity was selected.
    if (cue == CODY::PersonalityCue::None || cue == CODY::PersonalityCue::Affection) {
        s_state.affection_remaining_us = 0;
        s_state.affection_owns_channel = false;
    }
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Personality cue=%u selected (session request)", static_cast<unsigned>(cue));
    return ESP_OK;
}



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
        return &CODY_asset::sleeping_pearl;
    case BootPhase::Waking:
        return &CODY_asset::waking_pearl;
    case BootPhase::Ready:
        return &CODY_asset::ready_pearl;
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
    if (s_resources.raster_benchmark != nullptr) {
        s_resources.raster_benchmark->~RasterBenchmarkRun();
        heap_caps_free(s_resources.raster_benchmark);
        s_resources.raster_benchmark = nullptr;
    }
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

// No parameters: drain boot-completion writers and restore inherited scene policy.
// A failed lock leaves the buffer retained until reset because its ownership is ambiguous.
esp_err_t release_worker_resources()
{
    suspend_optional_personality_audio(true);
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        // The worker is retiring after a failure. Admission can be stopped even
        // when the display lock cannot safely be reacquired for state cleanup.
        if (s_typewriter_audio_prepared) {
            const esp_err_t audio_error = CODY::set_typewriter_audio_suspended(true);
            if (audio_error != ESP_OK) {
                ESP_LOGE(kTag, "Typewriter cleanup suspension failed (%s)", esp_err_to_name(audio_error));
            }
        }
        if (s_state.raster_benchmark_active ||
            (s_standby_scene_definition != nullptr && s_standby_scene_definition->standby_override_seconds != 0)) {
            const esp_err_t policy_error = LUNASHELL_power_manager_set_scene_standby_override(0);
            if (policy_error != ESP_OK) {
                ESP_LOGE(kTag, "Scene standby cleanup failed (%s)", esp_err_to_name(policy_error));
            }
        }
        const uint32_t retained_bytes =
            (s_resources.previous_rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig_debug_strokes != nullptr ? sizeof(CODY::RigDebugStrokeCache) : 0) +
            (s_resources.raster_benchmark != nullptr ? sizeof(CODY::RasterBenchmarkRun) : 0);
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayConsumerRetention(
            retained_bytes, lock_error
        );
        ESP_LOGE(kTag, "Companion cleanup retained PSRAM until reset: %s",
                 esp_err_to_name(lock_error));
        return lock_error;
    }
    const esp_err_t benchmark_error = stop_raster_benchmark(false, "renderer-cleanup");
    if (benchmark_error != ESP_OK) {
        const uint32_t retained_bytes = sizeof(CODY::RasterBenchmarkRun) +
            (s_resources.previous_rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig != nullptr ? sizeof(CODY::RigScene) : 0) +
            (s_resources.rig_debug_strokes != nullptr ? sizeof(CODY::RigDebugStrokeCache) : 0);
        LUNASHELL::DeviceSnapshotService::instance().reportDisplayConsumerRetention(
            retained_bytes, benchmark_error);
        bsp_display_unlock();
        return benchmark_error;
    }
    const esp_err_t audio_error = set_companion_typewriter_suspended(true);
    if (audio_error != ESP_OK) {
        ESP_LOGE(kTag, "Typewriter cleanup suspension failed (%s)", esp_err_to_name(audio_error));
    }
    const esp_err_t policy_error = apply_scene_standby_policy({});
    if (policy_error != ESP_OK) {
        bsp_display_unlock();
        return policy_error;
    }
    release_companion_resources();
    bsp_display_unlock();
    return audio_error;
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

// period: Canonical local-time period or explicit unavailable/disabled state.
// title: Receives immutable English text only after the identity is accepted.
esp_err_t routine_greeting_title(LUNASHELL::RoutinePeriod period, CODY2D::TextSpan &title)
{
    using LUNASHELL::RoutinePeriod;
    switch (period) {
    case RoutinePeriod::Morning:
        title = {"Good morning!", 13};
        return ESP_OK;
    case RoutinePeriod::Afternoon:
        title = {"Good afternoon!", 15};
        return ESP_OK;
    case RoutinePeriod::Evening:
        title = {"Good evening!", 13};
        return ESP_OK;
    case RoutinePeriod::Night:
        title = {"Hello, night owl!", 16};
        return ESP_OK;
    case RoutinePeriod::Unavailable:
        title = {"Hello!", 6};
        return ESP_OK;
    }
    ESP_LOGE(kTag, "Routine period identity rejected: %u (%s)",
        static_cast<unsigned>(period), esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
    return ESP_ERR_INVALID_RESPONSE;
}

// state: Display-owned native name line, stable until every tile retires.
// strip: Borrowed native RGB565 tile already containing the character and greeting.
// first_row: Logical y origin shared by text, clipping and orientation.
// first_column: Logical x origin shared by text, clipping and orientation.
// Exception: Returns missing state or the original native text drawing error.
esp_err_t draw_routine_user_name(const void *state, CODY2D::Surface strip,
                                uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) {
        ESP_LOGE(kTag, "Routine name raster state unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto &line = *static_cast<const CODY2D::TextLine *>(state);
    return CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
}

// profile: Readback-confirmed canonical user name or its explicit unavailable state.
// scene: Existing idle greeting; a name never changes the user's current route.
// Exception: Returns genuine metadata or native layout errors without substituting a name.
esp_err_t prepare_routine_user_name(const LUNASHELL::CodyUserNameSnapshot &profile,
                                  CODY::SceneFrame &scene)
{
    const bool available = profile.data_valid && !profile.data_stale && profile.last_error == ESP_OK;
    const char *text = available ? profile.name : "NAME UNAVAILABLE";
    const size_t length = std::strlen(text);
    if (length >= sizeof(s_state.routine_user_name)) {
        ESP_LOGE(kTag, "Routine user name length rejected: length=%u (%s)",
            static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    scene.content = {};
    s_state.routine_user_name_line = {};
    if (length > 0) {
        std::memcpy(s_state.routine_user_name, text, length + 1U);
        const auto &font = CODY::SceneData::body_font;
        const uint16_t width = (length - 1U) * font.advance + font.width;
        const int16_t left = (kPanelWidth - width) / 2;
        const uint16_t bottom = 390U + font.height;
        // The maximum 24-character name fits the narrowest occupied lower chord.
        const int32_t horizontal = kPanelWidth / 2 - left;
        const int32_t vertical = bottom - 1U - kPanelHeight / 2;
        if (horizontal * horizontal + vertical * vertical >= (kPanelWidth / 2) * (kPanelWidth / 2)) {
            ESP_LOGE(kTag, "Routine user name exceeds its circular chord (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        const esp_err_t error = CODY2D::prepare_text_line(font,
            {s_state.routine_user_name, static_cast<uint16_t>(length)}, {left, 390}, 0xF7BE,
            s_state.routine_user_name_line);
        if (error != ESP_OK) {
            return error;
        }
        scene.content.bounds = {static_cast<uint16_t>(left), 390,
            static_cast<uint16_t>(left + width), bottom};
        scene.content.state = &s_state.routine_user_name_line;
        scene.content.draw_rows = draw_routine_user_name;
    }
    // Availability can change without a durable name revision; retire those visual changes too.
    scene.content.revision = ++s_state.routine_user_name_content_revision;
    s_state.routine_user_name_revision = profile.revision;
    s_state.routine_user_name_error = profile.last_error;
    s_state.routine_user_name_available = available;
    return ESP_OK;
}

// clock: Canonical time and validated configuration, never a direct sensor read.
// lifecycle: Existing phase; routines cannot wake the device or interrupt boot/Sleep/Wake.
// scene: Caller-owned next frame; the immutable generic layout is reused, not cloned.
// Exception: Returns the real routine identity or scene preparation failure.
esp_err_t update_routine_greeting(const LUNASHELL::ClockSnapshot &clock,
                                 const CODY::LifecycleState &lifecycle, CODY::SceneFrame &scene)
{
    if (s_state.boot_phase != BootPhase::Ready || lifecycle.phase != CODY::LifecyclePhase::Idle ||
        s_greeting_scheduled.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    if (!clock.preferences_valid || !clock.routines_enabled) {
        if (s_state.routine_scene) {
            scene = {};
            s_state.routine_scene = false;
        }
        return ESP_OK;
    }
    const auto period = LUNASHELL::clock_observation_current(clock)
        ? clock.period : LUNASHELL::RoutinePeriod::Unavailable;
    if (!s_state.routine_scene && (!s_state.routine_greeting_pending || scene.valid)) {
        return ESP_OK;
    }
    // This bounded canonical preference projection adds no sensor or storage reads.
    // Keep its nested greeting-frame contribution explicit as the profile schema grows.
    static_assert(sizeof(LUNASHELL::CodyOptionsObservation) <= 256U);
    LUNASHELL::CodyOptionsObservation options;
    const esp_err_t options_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
    if (options_error != ESP_OK) {
        ESP_LOGE(kTag, "Routine user name observation failed (%s)", esp_err_to_name(options_error));
        return options_error;
    }
    const auto &profile = options.preferences.user_name;
    const bool profile_available = profile.data_valid && !profile.data_stale && profile.last_error == ESP_OK;
    const bool title_changed = !s_state.routine_scene || period != s_state.routine_period;
    if (!s_state.routine_scene) {
        const CODY::SceneDefinition *definition = nullptr;
        esp_err_t error = CODY::find_scene(CODY::SceneId::Greeting, definition);
        if (error == ESP_OK) {
            error = CODY::begin_scene(*definition, scene);
        }
        if (error != ESP_OK) {
            return error;
        }
        // Wake/boot already owns its presentation. Only start the English title.
        scene.intro_phase = CODY::SceneIntroPhase::Complete;
        scene.intro_elapsed_us = 0;
    } else if (period == s_state.routine_period && profile.revision == s_state.routine_user_name_revision &&
               profile.last_error == s_state.routine_user_name_error &&
               profile_available == s_state.routine_user_name_available) {
        return ESP_OK;
    }
    esp_err_t error = ESP_OK;
    if (title_changed) {
        // A name-only update retains the greeting's reveal clock and typing audio count.
        CODY2D::TextSpan title;
        error = routine_greeting_title(period, title);
        if (error == ESP_OK) {
            error = CODY::set_scene_title(scene, title);
        }
    }
    if (error == ESP_OK) {
        error = prepare_routine_user_name(profile, scene);
    }
    if (error != ESP_OK) {
        return error;
    }
    s_state.routine_period = period;
    s_state.routine_greeting_pending = false;
    s_state.routine_scene = true;
    ESP_LOGI(kTag, "Routine greeting selected: period=%u clock=%s sample_at=%lld us; no wake scheduled",
        static_cast<unsigned>(period), esp_err_to_name(clock.last_error),
        static_cast<long long>(clock.last_success_at_us));
    return ESP_OK;
}

// active_delta_us: Active animation time, excluding standby and pause.
// state: Connection edge history, committed by the caller after successful rendering preparation.
// cue: Receives a reaction only from current canonical connectivity.
// Exception: Returns the original canonical observation or reaction contract error.
esp_err_t observe_connection_personality(int64_t active_delta_us,
    CODY::ConnectionPersonalityState &state, CODY::PersonalityCue &cue)
{
    LUNASHELL::ConnectivityObservation connectivity;
    const esp_err_t error =
        LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(connectivity);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Connection expression observation failed (%s)", esp_err_to_name(error));
        return error;
    }
    const auto &wifi = connectivity.radios.wifi;
    const auto &pc = connectivity.radios.codex_link;
    CODY::ConnectionPersonalityInputs inputs{};
    inputs.wifi_current = personality_sample_current(wifi);
    inputs.wifi_searching = inputs.wifi_current && wifi.started && wifi.credentials_configured &&
        !wifi.connected && (wifi.connect_requested || wifi.reconnect_pending);
    inputs.pc_current = personality_sample_current(pc);
    inputs.pc_connected = inputs.pc_current && pc.state == LUNASHELL::CodexLinkState::Connected;
    inputs.pc_searching = inputs.pc_current && pc.state == LUNASHELL::CodexLinkState::Discovering;
    return CODY::advance_connection_personality(inputs, active_delta_us, state, cue);
}

// inputs: Current acquired response projection; gaze never reads a touch driver.
// scene: Current authored scene; gaze follows only actual visible information.
// lifecycle: Next native lifecycle, preserving exact startup/Sleep/Wake ownership.
// placement: Nominal logical character placement, including the technical artwork slot.
// now_us: Observation time after snapshot reads, used only for acquired touch freshness.
// rebase: Resume discards compaction continuity and consumed contacts across standby.
// Exception: Returns the original canonical projection or artistic mood validation failure.
esp_err_t observe_character_direction(const CODY::ResponseInputs &inputs,
    const CODY::SceneFrame &scene, const CODY::LifecycleState &lifecycle,
    CODY::Transform placement, int64_t now_us, bool rebase)
{
    auto &direction = s_state.character_inputs;
    direction = {};
    direction.placement = placement;
    direction.paused = s_state.animation_paused;
    direction.rebase = rebase;
    direction.caress_at_us = s_state.last_caress_at_us;
    const bool approval_view = scene.valid && scene.definition->id == CODY::SceneId::ApprovalOnPc;
    direction.enabled = CODY::lifecycle_is_resting(lifecycle) && !s_state.resume_scene_pending &&
        (companion_home_route_active() || approval_view) && !debug_view_active() &&
        !s_state.rig_debug && !s_state.expression_override;
    if (CODY::lifecycle_is_resting(lifecycle)) {
        // Reuse the serialized technical buffer; never copy the full canonical snapshot onto this stack.
        esp_err_t error = LUNASHELL::DeviceSnapshotService::instance()
            .readCodexTechnicalObservation(s_state.technical_observation);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Character activity observation failed (%s)", esp_err_to_name(error));
            return error;
        }
        error = CODY::project_character_activity(s_state.technical_observation.link,
            s_state.technical_observation.technical, direction.activity);
        if (error != ESP_OK) {
            return error;
        }
    }
    const esp_err_t mood_error = CODY::positive_character_affect(CODY::PersonalityData::happiness,
        s_state.happiness, direction.positive_affect);
    if (mood_error != ESP_OK) {
        return mood_error;
    }
    const bool low_battery = inputs.power.stamp.health == CODY::SampleHealth::Current &&
        inputs.power.stamp.error == ESP_OK && inputs.power.level_percent <= CODY::PersonalityConfig::low_battery_enter_percent;
    const CODY::PersonalityCue scene_cue = CODY::resolve_scene_personality_cue(
        scene.valid ? scene.definition->id : CODY::SceneId::CompanionIdle, s_state.connection_cue);
    direction.affection_allowed = direction.enabled && !low_battery &&
        scene_cue != CODY::PersonalityCue::Fear && scene_cue != CODY::PersonalityCue::Frustration &&
        s_state.eye_reaction_remaining_us == 0 && s_state.response.annoyance < .1f;
    const auto &touch = inputs.body_touch;
    const int64_t touch_limit_us = static_cast<int64_t>(CODY::Data::response.touch.event_timeout_seconds * 1000000.f);
    const bool touch_current = touch.stamp.health == CODY::SampleHealth::Current && touch.stamp.error == ESP_OK &&
        touch.stamp.sampled_at_us > 0 && now_us >= touch.stamp.sampled_at_us &&
        now_us - touch.stamp.sampled_at_us <= touch_limit_us && touch.pressed &&
        touch.rendering_rotation_degrees == inputs.rendering_rotation_degrees;
    if (touch_current) {
        direction.gaze_visible = true;
        direction.gaze_point = touch.point_px;
        return ESP_OK;
    }
    if (direction.activity.attention) {
        // An approval faces the user; no person tracking is inferred.
        return ESP_OK;
    }
    const bool panel_artwork = technical_panel_view_active();
    // Use last fully retired manga geometry, breaking the face/icon evaluation cycle.
    const uint32_t route = !panel_artwork && scene.valid ? static_cast<uint32_t>(scene.definition->id) : 0;
    const auto &manga = s_state.retired_personality;
    if (manga.status == CODY::PersonalityPresentationStatus::Visible && manga.opacity > 0 &&
        s_state.personality_lag.route == route && s_state.personality_lag.rotation == inputs.rendering_rotation_degrees) {
        direction.gaze_visible = true;
        direction.gaze_point = {(manga.bounds.left + manga.bounds.right) * .5f,
            (manga.bounds.top + manga.bounds.bottom) * .5f};
        return ESP_OK;
    }
    if (panel_artwork && s_state.technical_panel.valid &&
        s_state.technical_panel.panel == s_state.companion_panel) {
        direction.gaze_visible = direction.activity.working || direction.activity.compacting;
        direction.gaze_point = {233, 225};
    } else if (!panel_artwork && scene.valid && scene.intro_phase == CODY::SceneIntroPhase::Complete) {
        if (scene.activity_overlay.state != CODY::ActivityOverlayState::Hidden) {
            const auto bounds = CODY::activity_overlay_bounds();
            direction.gaze_visible = true;
            direction.gaze_point = {(bounds.left + bounds.right) * .5f, (bounds.top + bounds.bottom) * .5f};
        } else if (scene.title.valid && scene.visible_characters > 0) {
            direction.gaze_visible = direction.activity.working || direction.activity.compacting;
            direction.gaze_point = {233, scene.title.bounds.y + scene.title.bounds.height * .5f};
        }
    }
    return ESP_OK;
}

// frame_started_us: Real timestamp sampled while the display lock is held.
// resume_animation: Exclude standby time from both independent animation clocks.
// Exception: Returns the original snapshot, direction, lifecycle, scene or rig evaluation failure.
esp_err_t evaluate_companion_geometry(int64_t frame_started_us, bool resume_animation)
{
    if (s_resources.rig == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.raster_benchmark_active && CODY::lifecycle_is_resting(s_state.lifecycle) &&
        !s_state.resume_scene_pending) {
        CODY::Transform placement = CODY::Data::placement;
        placement.scale *= static_cast<float>(CODY::RuntimeData::raster_benchmark_cloud_scale_percent) / 100.f;
        CODY::ContourState contour = CODY::make_contour_state();
        const esp_err_t geometry_error = CODY::evaluate_rig(
            CODY::Data::ready, placement, *s_resources.rig, &contour, true
        );
        if (geometry_error != ESP_OK) {
            return geometry_error;
        }
        // This route is an authored fixed pose. It neither reads nor publishes synthetic sensors.
        s_state.contour = contour;
        s_state.face = CODY::Data::ready;
        s_state.response_frame.face = CODY::Data::ready;
        s_state.response_frame.transform = placement;
        s_state.animation_sampled_us = frame_started_us;
        return ESP_OK;
    }
    const int64_t animation_delta_us = resume_animation || s_state.animation_sampled_us == 0
        ? 0 : frame_started_us - s_state.animation_sampled_us;
    const float animation_delta_ms = static_cast<float>(animation_delta_us) / 1000.f;
    CODY::ResponseInputs inputs{};
    LUNASHELL::ClockSnapshot clock;
    const esp_err_t observation_error = CODY::observe_response_inputs(inputs, &clock);
    if (observation_error != ESP_OK) {
        return observation_error;
    }
    // Persistent preferences have independent validity; an invalid mode is not a Desk default.
    // Only a confirmed Away value may opt into the optional artistic policy.
    LUNASHELL::CodyOptionsObservation options;
    static_assert(sizeof(options) <= 256, "Away observation exceeds the bounded render projection budget");
    const esp_err_t options_error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
    if (options_error != ESP_OK) {
        ESP_LOGE(kTag, "Companion mode observation failed (%s)", esp_err_to_name(options_error));
        return options_error;
    }
    const bool away = options.preferences.usage_mode_valid &&
        options.preferences.usage_mode_error == ESP_OK &&
        options.preferences.usage_mode == LUNASHELL::CodyUsageMode::Away;
    if (resume_animation) {
        CODY::suspend_companion_shake(s_state.companion_shake, frame_started_us);
        CODY::cancel_companion_tap(s_state.companion_tap);
    }
    const bool gesture_eligible = companion_home_route_active() || technical_panel_view_active();
    if (CODY::observe_companion_shake(s_state.companion_shake, inputs.navigation_motion,
            esp_timer_get_time(), gesture_eligible)) {
        // Home consumes a deliberate shake without changing chats or remote state.
        select_companion_panel(CODY::CompanionPanel::Home);
    }
    const bool routine_current = LUNASHELL::clock_observation_current(clock) && clock.routines_enabled;
    const bool softer_routine = routine_current && (clock.quiet ||
        clock.period == LUNASHELL::RoutinePeriod::Evening || clock.period == LUNASHELL::RoutinePeriod::Night);
    CODY::Channels target_face{};
    CODY::ConnectionPersonalityState next_connection = s_state.connection_personality;
    CODY::PersonalityCue next_connection_cue = CODY::PersonalityCue::None;
    CODY::ContourControls target_contour{};
    CODY::Transform base_transform = CODY::Data::placement;
    CODY::SceneFrame next_scene = s_state.scene;
    CODY::LifecycleState next_lifecycle{};
    esp_err_t lifecycle_error = CODY::advance_lifecycle(
        s_state.lifecycle, animation_delta_us, s_state.boot_phase, next_lifecycle);
    if (lifecycle_error == ESP_OK) {
        lifecycle_error = CODY::lifecycle_targets(next_lifecycle, target_face, target_contour);
    }
    if (lifecycle_error != ESP_OK) {
        return lifecycle_error;
    }
    if (CODY::lifecycle_is_resting(next_lifecycle)) {
        const esp_err_t connection_error = observe_connection_personality(
            s_state.animation_paused ? 0 : animation_delta_us, next_connection, next_connection_cue);
        if (connection_error != ESP_OK) {
            return connection_error;
        }
    }
    const bool scene_active = CODY::lifecycle_is_resting(next_lifecycle) && !s_state.resume_scene_pending;
    const esp_err_t routine_error = scene_active && !debug_view_active()
        ? update_routine_greeting(clock, next_lifecycle, next_scene) : ESP_OK;
    if (routine_error != ESP_OK) {
        return routine_error;
    }
    if (scene_active && next_scene.valid) {
        target_face = next_scene.definition->face;
    }
    if (CODY::lifecycle_is_resting(next_lifecycle) && s_state.expression_override) {
        const esp_err_t expression_error = CODY::expression_channels(s_state.requested_expression, target_face);
        if (expression_error != ESP_OK) {
            return expression_error;
        }
    } else if (next_lifecycle.phase == CODY::LifecyclePhase::Idle && (routine_current || s_state.routine_scene) &&
               (!next_scene.valid || s_state.routine_scene)) {
        CODY::ExpressionId expression = CODY::ExpressionId::Calm;
        if (routine_current && clock.period == LUNASHELL::RoutinePeriod::Night) {
            expression = CODY::ExpressionId::Drowsy;
        } else if (routine_current && clock.period == LUNASHELL::RoutinePeriod::Morning && !clock.quiet) {
            expression = CODY::ExpressionId::Happy;
        }
        const esp_err_t expression_error = CODY::expression_channels(expression, target_face);
        if (expression_error != ESP_OK) {
            return expression_error;
        }
    }
    if (scene_active && next_scene.valid) {
        // The overlay retains its route and title timing while live facial response continues.
        esp_err_t scene_error = debug_view_active() ? ESP_OK : CODY::advance_scene(next_scene, animation_delta_us);
        if (scene_error == ESP_OK) {
            scene_error = CODY::scene_cloud_transform(next_scene, base_transform);
        }
        if (scene_error != ESP_OK) {
            return scene_error;
        }
    }
    const bool panel_artwork = technical_panel_view_active();
    if (panel_artwork) {
        base_transform = CODY::kCompanionPanelCharacterTransform;
    }
    if (CODY::lifecycle_is_resting(next_lifecycle) && !s_state.expression_override &&
        next_connection_cue != CODY::PersonalityCue::None) {
        const bool reaction_scene = !next_scene.valid || s_state.routine_scene ||
            next_scene.definition->id == CODY::SceneId::Connected ||
            next_scene.definition->id == CODY::SceneId::LookingForPc;
        if (reaction_scene) {
            const CODY::ExpressionId reaction = next_connection_cue == CODY::PersonalityCue::Joy
                ? CODY::ExpressionId::Happy
                : next_connection_cue == CODY::PersonalityCue::Curiosity
                    ? CODY::ExpressionId::Curious
                    : next_connection_cue == CODY::PersonalityCue::Fear
                        ? CODY::ExpressionId::Afraid : CODY::ExpressionId::Worried;
            const esp_err_t expression_error = CODY::expression_channels(reaction, target_face);
            if (expression_error != ESP_OK) {
                return expression_error;
            }
        }
    }
    // Eyes must actually close before the hold begins, not merely approach zero.
    CODY::Channels previous_face = s_state.face;
    if (next_lifecycle.phase == CODY::LifecyclePhase::Sleep &&
        next_lifecycle.phase_elapsed_us >= 400000) {
        previous_face[CODY::FaceChannel::EyeLeft] = 0;
        previous_face[CODY::FaceChannel::EyeRight] = 0;
    }
    CODY::Channels next_face{};
    const esp_err_t face_error = CODY::advance_face(
        previous_face, target_face, animation_delta_ms, next_face
    );
    if (face_error != ESP_OK) {
        return face_error;
    }
    CODY::ResponseState previous_response = s_state.response;
    if (resume_animation) {
        CODY::rebase_response_motion(previous_response);
    }
    CODY::ResponseState next_response{};
    const bool ordinary_idle = next_lifecycle.phase == CODY::LifecyclePhase::Idle &&
        (!next_scene.valid || s_state.routine_scene) && !s_state.resume_scene_pending;
    const bool resting_expression = !s_state.expression_override ||
        s_state.requested_expression == CODY::ExpressionId::Calm ||
        s_state.requested_expression == CODY::ExpressionId::Happy ||
        s_state.requested_expression == CODY::ExpressionId::Drowsy ||
        s_state.requested_expression == CODY::ExpressionId::Curious;
    inputs.idle_scene_allowed = ordinary_idle && !debug_view_active() && !s_state.animation_paused && !s_state.rig_debug &&
        resting_expression && s_state.affection_remaining_us == 0 && s_state.eye_reaction_remaining_us == 0 &&
        s_state.welcome_remaining_us == 0 &&
        next_connection_cue == CODY::PersonalityCue::None &&
        s_state.requested_personality == CODY::PersonalityCue::None &&
        s_state.personality_lag.cue == CODY::PersonalityCue::None;
    // A publication can occur after frame_started_us while we wait for the snapshot.
    // Freshness uses the later observation time, not a potentially earlier frame clock.
    CODY::ResponseConfig rhythm_config = CODY::Data::response;
    if (next_lifecycle.phase == CODY::LifecyclePhase::Idle && softer_routine) {
        // Calm the authored idle rhythm, not the IMU/touch spring or sampled data.
        rhythm_config.expression.base_breath_hz = .20f;
        rhythm_config.expression.stress_breath_gain *= .5f;
        target_contour[CODY::ContourControl::Amplitude] *= .55f;
        target_contour[CODY::ContourControl::Speed] *= .55f;
    }
    if (next_lifecycle.phase == CODY::LifecyclePhase::Sleep) {
        // Artistic breathing has its own clock; sleep slows it without
        // resetting phase or changing any real published sensor values.
        rhythm_config.expression.base_breath_hz = .15f;
        rhythm_config.expression.stress_breath_gain = 0;
        rhythm_config.expression.fatigue_breath_slowdown = 0;
    }
    CODY::apply_away_rhythm(away && gesture_eligible && next_lifecycle.phase == CODY::LifecyclePhase::Idle &&
        !debug_view_active() && !s_state.expression_override, rhythm_config, target_contour, inputs);
    const esp_err_t response_error = CODY::advance_response(
        rhythm_config, previous_response, inputs, animation_delta_ms,
        esp_timer_get_time(), true, next_response
    );
    if (response_error != ESP_OK) {
        return response_error;
    }
    CODY::ResponseFrame response_frame{};
    const esp_err_t composition_error = CODY::compose_response(
        CODY::Data::response, next_response, next_face, target_contour,
        base_transform, inputs.rendering_rotation_degrees, true, response_frame
    );
    if (composition_error != ESP_OK) {
        return composition_error;
    }
    if (panel_artwork) {
        // Keep the small character in its reserved slot while its face and contour remain alive.
        response_frame.transform = CODY::kCompanionPanelCharacterTransform;
    }
    esp_err_t direction_error = observe_character_direction(inputs, next_scene, next_lifecycle,
        base_transform, esp_timer_get_time(), resume_animation ||
        s_state.rendered_rotation_degrees != inputs.rendering_rotation_degrees);
    if (direction_error == ESP_OK) {
        direction_error = CODY::advance_character_direction(s_state.character_inputs,
            animation_delta_us, s_state.character_direction, s_state.next_character_direction);
    }
    if (direction_error == ESP_OK) {
        direction_error = CODY::compose_character_direction(s_state.next_character_direction, response_frame);
    }
    if (direction_error != ESP_OK) {
        return direction_error;
    }
    CODY::ContourState next_contour{};
    const esp_err_t contour_error = CODY::advance_contour(
        s_state.contour, response_frame.contour, animation_delta_ms, next_contour
    );
    if (contour_error != ESP_OK) {
        return contour_error;
    }
    const esp_err_t geometry_error = CODY::evaluate_rig(
        response_frame.face, response_frame.transform, *s_resources.rig, &next_contour,
        s_state.face_follows_body
    );
    if (geometry_error != ESP_OK) {
        return geometry_error;
    }
    const esp_err_t scene_policy_error = apply_scene_standby_policy(scene_active ? next_scene : CODY::SceneFrame{});
    if (scene_policy_error != ESP_OK) {
        return scene_policy_error;
    }
    if (s_state.lifecycle.phase == CODY::LifecyclePhase::BootReady &&
        next_lifecycle.phase == CODY::LifecyclePhase::Idle && !s_state.onboarding.effective_first_boot) {
        // Returning devices keep their stored onboarding/pairing decision.
        // This is one visual greeting after real readiness, never a pairing reset.
        s_state.welcome_cue = (esp_random() & 1U) == 0
            ? CODY::PersonalityCue::Affection : CODY::PersonalityCue::Joy;
        s_state.welcome_remaining_us = 4000000;
        ESP_LOGI(kTag, "Returning-device welcome: cue=%u", static_cast<unsigned>(s_state.welcome_cue));
    }
    s_state.contour = next_contour;
    s_state.connection_personality = next_connection;
    s_state.connection_cue = next_connection_cue;
    s_state.lifecycle = next_lifecycle;
    s_state.scene = next_scene;
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
    if (next_response.consumed_eye_tap_at_us > previous_response.consumed_eye_tap_at_us &&
        main_companion_view_active(true) && !s_state.rig_debug && !s_state.animation_paused) {
        // Use stress BEFORE this contact adds its protective stress impulse.
        // Repeated contacts alternate symbols so the poke is not always identical.
        const bool stressed_pose = s_state.expression_override &&
            s_state.requested_expression == CODY::ExpressionId::Stressed;
        const bool stressed = stressed_pose ||
            previous_response.stress >= CODY::Data::response.idle.activity_stress_threshold;
        CODY::PersonalityCue reaction = (esp_random() & 3U) == 0
            ? CODY::PersonalityCue::Frustration : CODY::PersonalityCue::Fear;
        if (stressed) {
            reaction = reaction == CODY::PersonalityCue::Fear
                ? CODY::PersonalityCue::Frustration : CODY::PersonalityCue::Fear;
        }
        if (reaction == s_state.last_eye_reaction) {
            reaction = reaction == CODY::PersonalityCue::Fear
                ? CODY::PersonalityCue::Frustration : CODY::PersonalityCue::Fear;
        }
        s_state.eye_reaction = reaction;
        s_state.last_eye_reaction = reaction;
        s_state.eye_reaction_remaining_us = 4000000;
        ESP_LOGI(kTag, "Eye reaction: cue=%u prior_stress=%.3f stressed_pose=%u",
            static_cast<unsigned>(reaction), static_cast<double>(previous_response.stress),
            static_cast<unsigned>(stressed_pose));
    }
    s_state.response = next_response;
    s_state.character_direction = s_state.next_character_direction;
    s_state.response_frame = response_frame;
    if (s_state.rendered_rotation_degrees != inputs.rendering_rotation_degrees) {
        cancel_transient_affection();
    }
    s_state.rendered_rotation_degrees = inputs.rendering_rotation_degrees;
    // Route timers and teletype keep their original active clock through the
    // Sleep/Wake presentation; lifecycle breathing still has its own live clock.
    if (scene_active) {
        s_state.animation_elapsed_us += animation_delta_us;
    }
    s_state.animation_sampled_us = frame_started_us;
    s_state.personality_delta_us = animation_delta_us;
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
        should_log = kExecutionTimingsEnabled;
        return ESP_OK;
    }

    // Let late frames go. Schedule from this frame's real timestamp.
    const bool paused_navigation = s_state.animation_paused && companion_home_route_active();
    const uint32_t period_ms = s_state.animation_paused && !paused_navigation
        ? kPausedReadoutPeriodMs : kRigFramePeriodMs;
    s_state.next_frame_us = timings.started_us + period_ms * 1000LL;
    if (s_state.raster_benchmark_active) {
        const CODY::FrameCosts measured{
            static_cast<uint32_t>(timings.geometry_us),
            static_cast<uint32_t>(timings.raster_us),
            static_cast<uint32_t>(timings.transfer_us),
            static_cast<uint32_t>(log.completed_us - timings.started_us)};
        const auto previous_phase = s_resources.raster_benchmark->phase();
        const esp_err_t sample_error = s_resources.raster_benchmark->record(
            timings.started_us, log.completed_us, measured, timings.submitted_pixels,
            timings.raster_profile.jelly, timings.raster_profile.vector_us);
        if (sample_error != ESP_OK) {
            return sample_error;
        }
        if (CODY::RuntimeData::raster_benchmark_kernel == CODY::JellyRasterKernel::SimdVerify) {
            // Comparing both implementations perturbs this run. Never publish PERF/STAGE as a baseline.
            return ESP_OK;
        }
        if (s_resources.raster_benchmark->phase() == CODY::RasterRunPhase::Warmup) {
            return ESP_OK;
        }
        if (previous_phase == CODY::RasterRunPhase::Warmup) {
            s_benchmark.reset();
            s_state.frame_rate = {};
        }
    }
    if (!measured_readout_route_active() && !s_state.raster_benchmark_active) {
        return ESP_OK;
    }
    ++s_state.rig_frame_count;
    log.frame_count = s_state.rig_frame_count;
    const int64_t completed_us = log.completed_us;
    if constexpr (kRenderDiagnosticsEnabled) {
        CODY::record_frame_rate(s_state.frame_rate, timings.started_us, completed_us);
    }
    if (!kExecutionTimingsEnabled) {
        return ESP_OK;
    }
    if (s_state.animation_paused) {
        // Paused HUD refreshes are not animation samples or frame-budget misses.
        should_log = s_state.last_timing_log_us == 0 || completed_us - s_state.last_timing_log_us >=
            CODY::RuntimeData::report_interval_ms * 1000LL;
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
        CODY::RuntimeData::report_interval_ms * 1000LL) {
        return ESP_OK;
    }
    error = s_benchmark.summarize(mode, CODY::RuntimeData::frame_budget_us, log.distribution);
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
// log: Bounded copied state; these compact records run outside the panel lock.
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
    ESP_LOGI(kTag, "DMA slots=%u rows=%u rects=%lu owner_us=%lu completion=%s retained=%lu",
        static_cast<unsigned>(log.transfer.last_buffer_count),
        static_cast<unsigned>(log.transfer.last_transfer_rows),
        static_cast<unsigned long>(log.transfer.last_rectangle_count),
        static_cast<unsigned long>(log.transfer.last_duration_us),
        esp_err_to_name(log.transfer.last_completion_error),
        static_cast<unsigned long>(log.transfer.retained_dma_bytes));
    ESP_LOGI(kTag, "DRAW ms clear=%ld art=%ld skel=%ld nav=%ld links=%ld tangents=%ld markers=%ld px=%lu bytes=%lu rects=%u tiles=%u/%u",
        rounded_ms(timings.raster_profile.clear_us), rounded_ms(timings.raster_profile.artwork_us),
        rounded_ms(timings.raster_profile.skeleton_us), rounded_ms(timings.raster_profile.navigation_us),
        rounded_ms(timings.raster_profile.skeleton_links_us),
        rounded_ms(timings.raster_profile.skeleton_tangents_us),
        rounded_ms(timings.raster_profile.skeleton_markers_us),
        static_cast<unsigned long>(timings.submitted_pixels),
        static_cast<unsigned long>(timings.submitted_pixels * sizeof(uint16_t)),
        timings.dirty_rectangles, timings.rasterized_tiles, timings.visited_tiles);
    // Substages are part of artwork, not extra work. Keep microseconds and a
    // separate short record so the sink's 255-byte limit cannot hide the split.
    const auto &jelly = timings.raster_profile.jelly;
    ESP_LOGI(kTag, "ART us jelly=%lld vector=%lld personality=%lld total=%lld detail=%u",
        static_cast<long long>(jelly.total_us),
        static_cast<long long>(timings.raster_profile.vector_us),
        static_cast<long long>(timings.raster_profile.personality_us),
        static_cast<long long>(timings.raster_profile.artwork_us),
        static_cast<unsigned>(jelly.blocks_enabled));
    if (!jelly.blocks_enabled) {
        return;
    }
    ESP_LOGI(kTag, "JELLY us prep=%lld interp=%lld compose=%lld sampled=%lu transparent=%lu written=%lu blocks=%lu",
        static_cast<long long>(jelly.preparation_us),
        static_cast<long long>(jelly.interpolation_us),
        static_cast<long long>(jelly.blend_us),
        static_cast<unsigned long>(jelly.sampled_pixels),
        static_cast<unsigned long>(jelly.transparent_pixels),
        static_cast<unsigned long>(jelly.blended_pixels),
        static_cast<unsigned long>(jelly.timed_blocks));
    if (jelly.sampled_pixels != 0) {
        // Composition visits transparent samples too. Both rates describe the same input population.
        ESP_LOGI(kTag, "JELLY ns_per_sample interp=%llu compose=%llu",
            static_cast<unsigned long long>(jelly.interpolation_us) * 1000U / jelly.sampled_pixels,
            static_cast<unsigned long long>(jelly.blend_us) * 1000U / jelly.sampled_pixels);
    }
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

// state: Asynchronous screenshot lifecycle to expose through the camera icon.
CODY::RigControlState screenshot_control_state(CODY::ScreenshotState state)
{
    switch (state) {
    case CODY::ScreenshotState::Idle:
        return CODY::RigControlState::Idle;
    case CODY::ScreenshotState::Capturing:
    case CODY::ScreenshotState::Saving:
        return CODY::RigControlState::Busy;
    case CODY::ScreenshotState::Saved:
        return CODY::RigControlState::Succeeded;
    case CODY::ScreenshotState::Failed:
        return CODY::RigControlState::Failed;
    }
    return CODY::RigControlState::Failed;
}

// frame_index: Original GIF ordinal whose immutable pixels are displayed.
// Exception: Retains every decode, deadline, lease and transfer error for frame retirement.
esp_err_t draw_branding_rows(uint16_t frame_index)
{
    for (uint16_t row = 0; row < kPanelHeight;) {
        esp_err_t error = LUNASHELL_display_frame_check_deadline();
        if (error != ESP_OK) {
            return error;
        }
        void *storage = nullptr;
        size_t capacity_bytes = 0;
        error = LUNASHELL_display_frame_buffer(&storage, &capacity_bytes);
        if (error != ESP_OK) {
            return error;
        }
        const size_t capacity_rows = (capacity_bytes / (kPanelWidth * sizeof(uint16_t))) & ~size_t{1};
        if (storage == nullptr || capacity_rows < 2 || capacity_rows > kPanelHeight ||
            reinterpret_cast<uintptr_t>(storage) % alignof(uint16_t) != 0) {
            ESP_LOGE(kTag, "VibeWare DMA slot rejected: bytes=%u (%s)",
                static_cast<unsigned>(capacity_bytes), esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        const uint16_t rows = static_cast<uint16_t>(std::min<size_t>(capacity_rows, kPanelHeight - row));
        auto *pixels = static_cast<uint16_t *>(storage);
        // Prepared text and the single source row live in the display-locked
        // companion's PSRAM state; no bitmap or glyph buffer enters this stack.
        CODY2D::Surface strip{pixels, kPanelWidth, rows, kPanelWidth,
            static_cast<uint32_t>(capacity_bytes / sizeof(uint16_t)),
            CODY2D::Rgb565ByteOrder::ByteSwapped};
        error = CODY::BootBranding::draw_rows(s_state.branding_presentation,
            s_state.branding_scratch, frame_index, strip, row);
        if (error != ESP_OK) {
            return error;
        }
        // The next borrow cannot reuse an in-flight DMA slot; frame end retires the final one.
        error = LUNASHELL_display_frame_submit_rgb565(0, row, kPanelWidth, row + rows, pixels);
        if (error != ESP_OK) {
            return error;
        }
        row += rows;
    }
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
// Display-owned bounded projection and prepared glyphs never enter nested frame stacks.
LUNASHELL::VoiceSnapshot s_voice_input_view{};
CODY2D::TextLine s_voice_title{}, s_voice_shake{}, s_voice_timer{};
char s_voice_countdown[32]{};
enum class VoiceCaptionKind : uint8_t { Model, Cost, Effort, Branch, Project };
struct VoiceCaption {
    VoiceCaptionKind kind;
    char text[144]{};
    CODY2D::TextLine line{};
    uint16_t length = 0;
    uint16_t width = 0;
    uint16_t color = 0xF7BE;
};
VoiceCaption s_voice_captions[] = {{VoiceCaptionKind::Model}, {VoiceCaptionKind::Cost},
    {VoiceCaptionKind::Effort}, {VoiceCaptionKind::Branch}, {VoiceCaptionKind::Project}};
int64_t s_voice_carousel_started_us = 0;
CODY2D::TextLine s_voice_carousel_line{};
static_assert(sizeof(s_voice_captions) <= 1280, "Voice carousel storage belongs to the display owner, never its frame stack");
LUNASHELL::CodexTechnicalObservation s_voice_metadata{};
CODY::ResponseInputs s_voice_motion{};
bool s_voice_view_visible = false;
uint16_t s_voice_view_rotation = 0;
bool s_voice_force_full = false;
LUNASHELL::VoiceInputPhase s_voice_view_phase = LUNASHELL::VoiceInputPhase::Idle;
int64_t s_voice_view_started_at_us = 0;
int64_t s_voice_tap_due_us = 0;
uint16_t s_voice_tap_rotation = 0;
int16_t s_voice_tap_x = 0, s_voice_tap_y = 0;

// No parameters: only the fully retired voice view can own shake cancellation.
bool voice_input_view_active()
{
    return s_voice_view_visible;
}

// text: Stable display-owned printable caption.
// top: Logical top edge within the available circular chord.
// color: Semantic native RGB565 foreground.
// line: Receives glyph references retained until frame retirement.
// Exception: Returns the original font or measured-layout failure.
esp_err_t prepare_voice_caption(const char *text, int16_t top, uint16_t color, CODY2D::TextLine &line)
{
    const auto &font = CODY::SceneData::body_font;
    const uint16_t length = static_cast<uint16_t>(std::strlen(text));
    if (length == 0 || font.height > 28) {
        ESP_LOGE(kTag, "Voice caption rejected: length=%u font_height=%u", unsigned(length), unsigned(font.height));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t width = static_cast<uint16_t>((length - 1U) * font.advance + font.width);
    if (width > 320) {
        ESP_LOGE(kTag, "Voice caption width rejected: width=%u", unsigned(width));
        return ESP_ERR_INVALID_SIZE;
    }
    return CODY2D::prepare_text_line(font, {text, length},
        {static_cast<int16_t>((466 - width) / 2), top}, color, line);
}

// strip: Borrowed DMA tile in logical coordinates.
// row: First logical row of the tile.
// pose: Authored synthetic waveform identity, never measured microphone amplitude.
// Exception: Returns the original native raster failure.
esp_err_t draw_voice_ribbon(CODY2D::Surface strip, uint16_t row, size_t pose)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) return surface_error;
    if (strip.width != kPanelWidth || row + strip.height > kPanelHeight || pose >= CODY::kVoiceWaveformFrameCount) {
        ESP_LOGE(kTag, "Voice ribbon bounds rejected: width=%u row=%u height=%u pose=%u",
            unsigned(strip.width), unsigned(row), unsigned(strip.height), unsigned(pose));
        return ESP_ERR_INVALID_ARG;
    }
    if (row >= 210 || row + strip.height <= 170) return ESP_OK;
    for (uint16_t x = 13; x < 453; ++x) {
        const uint32_t position = (x - 13U) * 63U;
        const size_t first = std::min<size_t>(62, position / 439U);
        const int fraction = static_cast<int>(position - first * 439U);
        const uint16_t color = CODY2D::blend_rgb565(0x9B1F, 0x2E7F,
            static_cast<uint8_t>((x - 13U) * 255U / 439U));
        constexpr int scale[] = {256, 192, 128, 224};
        constexpr int shift[] = {0, 3, -3, -1};
        constexpr uint8_t strength[] = {230, 155, 120, 170};
        for (size_t layer = 0; layer < 4; ++layer) {
            const size_t wave_pose = (pose + layer * 4) % CODY::kVoiceWaveformFrameCount;
            // Q8 interpolation gives subpixel edge coverage without floats, trigonometry or supersampled buffers.
            const int value_q8 = int(CODY::kVoiceWaveform[wave_pose][first]) * 256 +
                (CODY::kVoiceWaveform[wave_pose][first + 1] - CODY::kVoiceWaveform[wave_pose][first]) * fraction * 256 / 439;
            const int center_q8 = (190 + shift[layer]) * 256 + value_q8 * scale[layer] / 256;
            for (uint16_t offset = 0; offset < strip.height; ++offset) {
                const int y = row + offset;
                if (y < 170 || y >= 210) continue;
                const int coverage = std::clamp(512 - std::abs(y * 256 + 128 - center_q8), 0, 256);
                if (coverage == 0) continue;
                const size_t pixel = offset * strip.stride + x;
                const uint16_t background = CODY2D::rgb565_stored_to_native(strip.pixels[pixel], strip.byte_order);
                const uint8_t opacity = static_cast<uint8_t>(coverage * strength[layer] / 256);
                const uint16_t blended = CODY2D::blend_rgb565(color, background, opacity);
                strip.pixels[pixel] = CODY2D::rgb565_native_to_stored(blended, strip.byte_order);
            }
        }
    }
    return ESP_OK;
}

// No parameters: fresh canonical metadata feeds the existing measured fade carousel.
// Exception: Returns original snapshot, metadata or glyph errors; unavailable values stay empty.
esp_err_t prepare_voice_identity()
{
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readCodexTechnicalObservation(s_voice_metadata);
    if (error != ESP_OK) return error;
    const auto &link = s_voice_metadata.link;
    const auto &activity = link.activity;
    const bool current = link.data_valid && !link.data_stale && link.state == LUNASHELL::CodexLinkState::Connected &&
        activity.data_valid && !activity.data_stale;
    const char *project = current ? activity.project : "";
    const char *branch = current ? activity.branch : "";
    const int64_t now = esp_timer_get_time();
    const auto model_cost = CODY::observed_model_cost(link, s_voice_metadata.technical);
    const auto &technical = s_voice_metadata.technical;
    const bool technical_current = current && technical.data_valid && !technical.data_stale && technical.chat_id[0] != '\0' &&
        technical.revision == activity.revision && std::strcmp(technical.chat_id, activity.chat_id) == 0;
    const char *effort = technical_current && technical.effort_valid && !technical.effort_stale ? technical.effort : "";
    bool changed = !s_voice_view_visible || s_voice_view_started_at_us != s_voice_input_view.input.started_at_us;
    size_t count = 0;
    int64_t total_us = 0;
    for (auto &caption : s_voice_captions) {
        const char *source = "";
        const char *prefix = "";
        switch (caption.kind) {
        case VoiceCaptionKind::Model: source = model_cost.model; prefix = "model: "; break;
        case VoiceCaptionKind::Cost:
            source = model_cost.band == LUNASHELL::CodexCostBand::Unknown ? "" : CODY::cost_band_text(model_cost.band);
            prefix = "cost: "; break;
        case VoiceCaptionKind::Effort: source = effort; prefix = "effort: "; break;
        case VoiceCaptionKind::Branch: source = branch; prefix = "branch: "; break;
        case VoiceCaptionKind::Project: source = project; prefix = ""; break;
        }
        const char *old_source = caption.length == 0 ? "" : caption.text + std::strlen(prefix);
        changed = changed || std::strcmp(old_source, source) != 0;
        caption.color = caption.kind == VoiceCaptionKind::Cost ? CODY::cost_band_color(model_cost.band) : 0xF7BE;
        error = CODY::prepare_activity_identity_caption(CODY::SceneData::body_font, source, prefix,
            caption.text, caption.line, caption.color, caption.length, caption.width, 128);
        if (error != ESP_OK) return error;
        if (caption.length == 0) continue;
        ++count;
        // Long labels finish their measured scroll before the fade advances.
        const uint16_t overflow = caption.width > 320 ? caption.width - 320 : 0;
        total_us += 3500000 + int64_t(overflow) * 35000 + 500000;
    }
    if (changed || s_voice_carousel_started_us == 0) s_voice_carousel_started_us = now;
    s_voice_carousel_line = {};
    if (count == 0) return ESP_OK;
    int64_t elapsed = (now - s_voice_carousel_started_us) % total_us;
    for (auto &caption : s_voice_captions) {
        if (caption.length == 0) continue;
        const uint16_t overflow = caption.width > 320 ? caption.width - 320 : 0;
        const int64_t hold = 3500000 + int64_t(overflow) * 35000;
        if (elapsed >= hold + 500000) { elapsed -= hold + 500000; continue; }
        uint8_t opacity = 255;
        if (count > 1 && elapsed < 200000) opacity = static_cast<uint8_t>(elapsed * 255 / 200000);
        if (count > 1 && elapsed > hold) opacity = static_cast<uint8_t>(std::max<int64_t>(0, 200000 - (elapsed - hold)) * 255 / 200000);
        const uint16_t offset = static_cast<uint16_t>(std::min<int64_t>(overflow, std::max<int64_t>(0, elapsed - 1500000) / 35000));
        return CODY::prepare_activity_identity_window(CODY::SceneData::body_font, caption.text,
            caption.length, caption.width, offset, CODY2D::fade_rgb565_to_black(caption.color, opacity), s_voice_carousel_line);
    }
    return ESP_OK;
}

// No parameters: immutable hints alternate on the current input's monotonic clock.
// Exception: Returns original native font or glyph-window errors.
esp_err_t prepare_voice_hint()
{
    const auto &font = CODY::SceneData::body_font;
    esp_err_t error = CODY2D::validate_mono_font(font);
    if (error != ESP_OK) return error;
    if (font.height > 28) {
        ESP_LOGE(kTag, "Voice hint height rejected: height=%u", unsigned(font.height));
        return ESP_ERR_INVALID_SIZE;
    }
    static constexpr char transcription[] = "We will send the audio to Codex for transcription";
    const uint16_t transcription_length = sizeof(transcription) - 1;
    const uint16_t transcription_width = (transcription_length - 1U) * font.advance + font.width;
    const uint16_t overflow = transcription_width > 320 ? transcription_width - 320 : 0;
    constexpr int64_t shake_duration = 4000000;
    const int64_t transcription_duration = 3500000 + int64_t(overflow) * 35000;
    const int64_t elapsed = std::max<int64_t>(0,
        esp_timer_get_time() - s_voice_input_view.input.started_at_us) %
        (shake_duration + transcription_duration);
    const bool show_transcription = elapsed >= shake_duration;
    const char *text = show_transcription ? transcription : "Shake to fade";
    const uint16_t length = static_cast<uint16_t>(std::strlen(text));
    const uint16_t width = (length - 1U) * font.advance + font.width;
    const int64_t caption_elapsed = show_transcription ? elapsed - shake_duration : elapsed;
    const uint16_t offset = show_transcription ? static_cast<uint16_t>(std::min<int64_t>(overflow,
        std::max<int64_t>(0, caption_elapsed - 1500000) / 35000)) : 0;
    // Native measured windows retain the complete literal; the renderer clips the moving edge.
    error = CODY::prepare_activity_identity_window(font, text, length, width, offset, 0xAD75, s_voice_shake);
    if (error != ESP_OK) return error;
    s_voice_shake.origin.y += 65;
    s_voice_shake.bounds.y += 65;
    return ESP_OK;
}

// No parameters: priority input uses a small ribbon, genuine identity carousel and countdown.
// Exception: Returns canonical, prepared-text, DMA lease or original transfer failure.
esp_err_t draw_voice_input_rows()
{
    using Phase = LUNASHELL::VoiceInputPhase;
    const auto phase = s_voice_input_view.input.phase;
    const char *title = phase == Phase::Listening ? "Listening" : phase == Phase::Sending ? "Sending audio" :
        phase == Phase::Cancelling ? "Cancelling" : phase == Phase::Received ? "On your PC" :
        phase == Phase::Delivered ? "Sent to Codex" : phase == Phase::Cancelled ? "Cancelled" :
        phase == Phase::Unconfirmed ? "Check on your PC" : "Audio failed";
    esp_err_t error = prepare_voice_caption(title, 92, 0xF7BE, s_voice_title);
    if (error != ESP_OK) return error;
    error = prepare_voice_hint();
    if (error != ESP_OK) return error;
    const int64_t elapsed = std::max<int64_t>(0, esp_timer_get_time() - s_voice_input_view.input.started_at_us);
    const uint32_t used = std::min<uint32_t>(60, phase == Phase::Listening ?
        static_cast<uint32_t>(elapsed / 1000000) : s_voice_input_view.input.captured_samples / 16000U);
    const int count = std::snprintf(s_voice_countdown, sizeof(s_voice_countdown), "%02us elapsed / %02us left",
        static_cast<unsigned>(used), static_cast<unsigned>(60U - used));
    if (count <= 0 || count >= int(sizeof(s_voice_countdown))) return ESP_ERR_INVALID_SIZE;
    error = prepare_voice_caption(s_voice_countdown, 402, 0xF7BE, s_voice_timer);
    if (error != ESP_OK) return error;
    error = prepare_voice_identity();
    if (error != ESP_OK) return error;
    const bool cancellable = phase == Phase::Listening || phase == Phase::Sending;
    error = CODY::observe_response_inputs(s_voice_motion);
    if (error != ESP_OK) return error;
    if (CODY::observe_companion_shake(s_state.companion_shake, s_voice_motion.navigation_motion,
            esp_timer_get_time(), cancellable && s_voice_view_visible)) {
        error = LUNASHELL::cancelVoiceInput();
        if (error != ESP_OK) return error;
    }
    const bool full = s_voice_force_full || !s_voice_view_visible || s_voice_view_phase != phase ||
        s_voice_view_started_at_us != s_voice_input_view.input.started_at_us ||
        s_voice_view_rotation != s_state.rendered_rotation_degrees;
    const size_t pose = static_cast<size_t>((esp_timer_get_time() / 125000) % CODY::kVoiceWaveformFrameCount);
    for (uint16_t row = 0; row < kPanelHeight;) {
        // First retirement clears the old route. Later frames transfer only the changing bands.
        if (!full && !((row >= 170 && row < 210) || (row >= 300 && row < 328) ||
            (row >= 364 && row < 394) || (row >= 402 && row < 430))) {
            row += 2;
            continue;
        }
        error = LUNASHELL_display_frame_check_deadline();
        if (error != ESP_OK) return error;
        void *storage = nullptr;
        size_t capacity = 0;
        error = LUNASHELL_display_frame_buffer(&storage, &capacity);
        if (error != ESP_OK) return error;
        const size_t available_rows = (capacity / (466 * sizeof(uint16_t))) & ~size_t{1};
        if (storage == nullptr || available_rows < 2 || available_rows > 466) return ESP_ERR_INVALID_SIZE;
        // Two rows keep all partial regions aligned with the native DMA contract.
        constexpr uint16_t rows = 2;
        CODY2D::Surface strip{static_cast<uint16_t *>(storage), 466, rows, 466,
            static_cast<uint32_t>(capacity / sizeof(uint16_t)), CODY2D::Rgb565ByteOrder::ByteSwapped};
        error = CODY2D::clear(strip, 0);
        if (error == ESP_OK) error = draw_voice_ribbon(strip, row, pose);
        if (error == ESP_OK) error = CODY2D::draw_text_rows(s_voice_title, s_voice_title.text.length, strip, row, 0);
        if (error == ESP_OK && s_voice_carousel_line.valid) {
            const uint16_t top = std::max<uint16_t>(row, 300);
            const uint16_t bottom = std::min<uint16_t>(row + rows, 328);
            if (top < bottom) {
                CODY2D::Surface clipped = strip;
                clipped.pixels += (top - row) * strip.stride + 73;
                clipped.pixel_capacity -= (top - row) * strip.stride + 73;
                clipped.width = 320;
                clipped.height = bottom - top;
                error = CODY2D::draw_text_rows(s_voice_carousel_line, s_voice_carousel_line.text.length, clipped, top, 73);
            }
        }
        if (error == ESP_OK && cancellable) {
            const uint16_t top = std::max<uint16_t>(row, 365);
            const uint16_t bottom = std::min<uint16_t>(row + rows, 393);
            if (top < bottom) {
                CODY2D::Surface clipped = strip;
                clipped.pixels += (top - row) * strip.stride + 73;
                clipped.pixel_capacity -= (top - row) * strip.stride + 73;
                clipped.width = 320;
                clipped.height = bottom - top;
                error = CODY2D::draw_text_rows(s_voice_shake, s_voice_shake.text.length, clipped, top, 73);
            }
        }
        if (error == ESP_OK) error = CODY2D::draw_text_rows(s_voice_timer, s_voice_timer.text.length, strip, row, 0);
        if (error == ESP_OK) error = LUNASHELL_display_frame_submit_rgb565(0, row, 466, row + rows, storage);
        if (error != ESP_OK) return error;
        row += rows;
    }
    return ESP_OK;
}
#endif

// Hold the shared recursive panel lock across every strip so rotation and standby
// cannot alter the panel/touch mapping in the middle of one complete redraw.
// resume_animation: Skip parked time when restarting the animation clocks after standby.
// deferred_log: Startup's outer recursive lock defers output until that owner releases it.
// worker_trace: Optional timings captured by the worker before it enters the frame owner.
// Exception: Retains the original frame, state, raster or transfer failure in DrawResult.
DrawResult draw_companion_frame(bool resume_animation = false,
                                CompletedFrameReport *deferred_log = nullptr,
                                const FrameTimings::WorkerLoopTrace *worker_trace = nullptr)
{
    FrameTimings timings{};
    if (worker_trace != nullptr) {
        timings.worker = *worker_trace;
    }
    bool admission_parked = false;
    timings.frame_begin_started_us = esp_timer_get_time();
    const esp_err_t lock_error = LUNASHELL_display_frame_begin(kFrameBudgetMs, &admission_parked);
    timings.frame_begin_completed_us = esp_timer_get_time();
    if (lock_error != ESP_OK) {
        return {lock_error, admission_parked, 0};
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
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (s_voice_tap_due_us != 0 && esp_timer_get_time() >= s_voice_tap_due_us) {
        s_voice_tap_due_us = 0;
        if (companion_home_route_active() && !s_state.touch_pressed &&
            s_voice_tap_rotation == s_state.rendered_rotation_degrees) {
            const esp_err_t panel_error = advance_local_companion_panel();
            if (panel_error != ESP_OK) return frame.finish({panel_error, false, 0});
        }
    }
    const esp_err_t voice_read = LUNASHELL::DeviceSnapshotService::instance().readVoiceObservation(s_voice_input_view);
    if (voice_read != ESP_OK) return frame.finish({voice_read, false, 0});
    using VoicePhase = LUNASHELL::VoiceInputPhase;
    const auto voice_phase = s_voice_input_view.input.phase;
    const bool voice_active = voice_phase == VoicePhase::Listening || voice_phase == VoicePhase::Sending ||
        voice_phase == VoicePhase::Cancelling || voice_phase == VoicePhase::Received;
    const bool voice_terminal = voice_phase == VoicePhase::Delivered || voice_phase == VoicePhase::Cancelled ||
        voice_phase == VoicePhase::Unconfirmed || voice_phase == VoicePhase::Failed;
    const bool voice_visible = voice_active || (voice_terminal && s_voice_input_view.input.changed_at_us > 0 &&
        esp_timer_get_time() - s_voice_input_view.input.changed_at_us < 3000000);
    if (voice_visible) {
        s_voice_force_full = resume_animation;
        esp_err_t error = set_companion_typewriter_suspended(true);
        suspend_optional_personality_audio(true);
        if (error == ESP_OK) error = CODY::set_codex_audio_suspended(true);
        if (error == ESP_OK) error = draw_voice_input_rows();
        const DrawResult completed = frame.finish({error, false, LUNASHELL_display_runtime_content_revision()});
        if (completed.error == ESP_OK) {
            // Only a fully retired frame admits the visible shake-only input gesture.
            if (!s_voice_view_visible) {
                CODY::cancel_companion_tap(s_state.companion_tap);
                CODY::cancel_petting(s_state.petting);
                s_state.setup_hold_candidate = false;
                s_state.setup_hold_eligible = false;
                s_state.body_drag_active = false;
            }
            s_voice_view_visible = true;
            s_voice_view_rotation = s_state.rendered_rotation_degrees;
            s_voice_view_phase = voice_phase;
            s_voice_view_started_at_us = s_voice_input_view.input.started_at_us;
            s_state.frame_dirty.store(false, std::memory_order_release);
        }
        s_voice_tap_due_us = 0;
        s_state.animation_sampled_us = 0;
        s_state.onboarding_sampled_us = 0;
        s_resources.previous_frame_valid = false;
        s_state.next_frame_us = esp_timer_get_time() + 125000;
        return completed;
    }
    if (s_voice_view_visible) {
        s_voice_view_visible = false;
        const esp_err_t codex_audio_error = CODY::set_codex_audio_suspended(false);
        if (codex_audio_error != ESP_OK) return frame.finish({codex_audio_error, false, 0});
        s_state.animation_sampled_us = 0;
        s_state.onboarding_sampled_us = 0;
        s_resources.previous_frame_valid = false;
    }
#endif
    if (!s_state.branding_complete) {
        const int64_t now_us = esp_timer_get_time();
        uint16_t candidate = s_state.branding_frame;
        const bool due = !resume_animation && s_state.branding_frame_due_us != 0 &&
            now_us >= s_state.branding_frame_due_us;
        if (due && candidate + 1 == VIBEWARE_BOOT_FRAME_COUNT &&
            s_state.boot_phase == BootPhase::Ready) {
            s_state.branding_complete = true;
            s_state.animation_sampled_us = 0;
            s_state.onboarding_sampled_us = 0;
            s_resources.previous_frame_valid = false;
            if (s_state.onboarding.force_first_boot) {
                s_greeting_due_us = now_us + CODY::SceneData::boot_delay_ms * 1000LL;
                s_greeting_scheduled.store(true, std::memory_order_release);
            }
            ESP_LOGI(kTag, "VibeWare cycle retired; entering the existing Cody startup sequence");
        } else {
            if (due) {
                candidate = static_cast<uint16_t>((candidate + 1) % VIBEWARE_BOOT_FRAME_COUNT);
            }
            const esp_err_t draw_error = draw_branding_rows(candidate);
            const uint32_t revision = LUNASHELL_display_runtime_content_revision();
            const DrawResult completed = frame.finish({draw_error, false, revision});
            if (completed.error == ESP_OK) {
                // Timings start after complete retirement, so every original frame gets its visible dwell.
                if (s_state.branding_frame_due_us == 0 || due || resume_animation) {
                    s_state.branding_frame_due_us = esp_timer_get_time() +
                        VIBEWARE_boot_frames[candidate].duration_ms * 1000LL;
                }
                s_state.branding_frame = candidate;
                s_state.next_frame_us = s_state.branding_frame_due_us;
                s_state.animation_sampled_us = 0;
                s_state.onboarding_sampled_us = 0;
                s_state.frame_dirty.store(false, std::memory_order_release);
            }
            return completed;
        }
    }

    const esp_err_t audio_resume_error = set_companion_typewriter_suspended(
        s_state.lifecycle.phase == CODY::LifecyclePhase::Sleep || debug_view_active());
    if (audio_resume_error != ESP_OK) {
        return frame.finish({audio_resume_error, false, 0});
    }
    const uint32_t revision = LUNASHELL_display_runtime_content_revision();
    // Capture requests can arrive between scene dispatch and capture-timeline advancement.
    const bool capture_interrupted = capture_visual() != CaptureVisual::None ||
        s_capture_sequence_active.load(std::memory_order_acquire);
    if (s_state.raster_benchmark_active &&
        (revision != s_state.raster_benchmark_revision || capture_interrupted)) {
        const esp_err_t benchmark_error = stop_raster_benchmark(false,
            capture_interrupted ? "power-capture" : "display-revision");
        if (benchmark_error != ESP_OK) {
            return frame.finish({benchmark_error, false, 0});
        }
    }
    timings.raster_profile.jelly.blocks_enabled = s_state.raster_benchmark_active &&
        CODY::RuntimeData::raster_benchmark_profile_blocks;
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
    const bool previous_frame_available = s_resources.previous_frame_valid;
    // A failed partial update never becomes the reference for the next frame.
    s_resources.previous_frame_valid = false;
    PendingTouchReports pending_touch;
    timings.buffer_ready_us = esp_timer_get_time();
    timings.started_us = timings.buffer_ready_us;
    CODY::DebugReadout debug_readout{};
    CODY::DirtyPlan dirty_plan;
    const bool frame_overlay = debug_view_active();
    const bool frame_channel_list = debug_channel_view_active();
    bool frame_technical = technical_panel_view_active();
    const bool frame_debug = s_state.rig_debug && !frame_overlay && !frame_technical;
    // Real content in the lower arc reserves it, including a personalized greeting.
    const bool footer_blocks_readout = frame_technical || (!frame_overlay && s_state.scene.valid &&
        (s_state.scene.content.bottom_action_visible ||
         (s_state.scene.content.draw_rows != nullptr && s_state.scene.content.bounds.bottom > 390)));
    // A paused redraw has no active animation interval, even after a long wall-clock gap.
    s_state.personality_delta_us = 0;
    if (s_state.rig_visible) {
        if (s_state.animation_paused) {
            const esp_err_t navigation_error = observe_paused_companion_navigation(timings.started_us, resume_animation);
            if (navigation_error != ESP_OK) {
                return frame.finish({navigation_error, false, 0});
            }
            frame_technical = technical_panel_view_active();
        }
        if (resume_animation) {
            s_state.frame_rate = {};
            s_benchmark.reset();
        }
        if constexpr (kRenderDiagnosticsEnabled) {
            if ((s_state.raster_benchmark_active || (frame_overlay && !frame_channel_list) ||
                 (measured_readout_route_active() && s_state.debug_readout_enabled)) &&
                !footer_blocks_readout) {
                const esp_err_t readout_error = CODY::prepare_debug_readout(
                    s_state.frame_rate, timings.started_us, debug_readout
                );
                if (readout_error != ESP_OK) {
                    return frame.finish({readout_error, false, 0});
                }
                if (s_state.animation_paused) {
                    std::snprintf(debug_readout.frame_rate, sizeof(debug_readout.frame_rate), "PAUSED");
                }
            }
        }
        // Freeze the evaluated scene as well as the clocks. Fresh sensors keep
        // publishing independently, but cannot deform a paused character.
        esp_err_t geometry_error = ESP_OK;
        if (!s_state.animation_paused) {
            geometry_error = evaluate_companion_geometry(timings.started_us, resume_animation);
        } else if (!s_resources.rig->valid) {
            geometry_error = ESP_ERR_INVALID_STATE;
        } else if (s_cached_companion_panel != (frame_technical
                       ? s_state.companion_panel : CODY::CompanionPanel::Home)) {
            // A passive page change repositions a paused pose without advancing its expression.
            CODY::Transform placement = CODY::kCompanionPanelCharacterTransform;
            if (!frame_technical) {
                geometry_error = s_state.scene.valid
                    ? CODY::scene_cloud_transform(s_state.scene, placement) : ESP_OK;
                if (!s_state.scene.valid) {
                    placement = CODY::Data::placement;
                }
            }
            if (geometry_error == ESP_OK) {
                geometry_error = CODY::evaluate_rig(s_state.response_frame.face, placement,
                    *s_resources.rig, &s_state.contour, s_state.face_follows_body);
            }
            if (geometry_error == ESP_OK) {
                s_state.response_frame.transform = placement;
            }
        } else if (s_state.scene.valid && (!s_cached_scene.valid ||
                   s_cached_scene.definition != s_state.scene.definition)) {
            // Navigation may change layout while animation is paused. Reposition
            // the saved face/contour without advancing time or sampling sensors.
            CODY::Transform placement;
            geometry_error = CODY::scene_cloud_transform(s_state.scene, placement);
            if (frame_technical) {
                placement = CODY::kCompanionPanelCharacterTransform;
            }
            if (geometry_error == ESP_OK) {
                geometry_error = CODY::evaluate_rig(s_state.response_frame.face, placement,
                    *s_resources.rig, &s_state.contour, s_state.face_follows_body);
            }
            if (geometry_error == ESP_OK) {
                s_state.response_frame.transform = placement;
            }
        } else if (s_resources.rig->face_deformed != s_state.face_follows_body) {
            // A preference switch must also work while paused. Rebuild only the
            // saved pose; do not sample sensors or advance expression/motion clocks.
            geometry_error = CODY::evaluate_rig(
                s_state.response_frame.face, s_state.response_frame.transform,
                *s_resources.rig, &s_state.contour, s_state.face_follows_body
            );
        }
        if (geometry_error != ESP_OK) {
            return frame.finish({geometry_error, false, 0});
        }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        if (frame_overlay || s_state.debug_placement_dirty) {
            // Keep the composed normal placement intact so Back restores it even while paused.
            const CODY::Transform placement = frame_overlay
                ? CODY::Transform{233, 205, 130} : s_state.response_frame.transform;
            geometry_error = CODY::evaluate_rig(s_state.response_frame.face, placement,
                *s_resources.rig, &s_state.contour, s_state.face_follows_body);
            if (geometry_error != ESP_OK) {
                return frame.finish({geometry_error, false, 0});
            }
            s_state.debug_placement_dirty = false;
        }
        if (frame_channel_list) {
            const esp_err_t list_error = CODY::prepare_debug_channel_list(
                s_state.response_frame.face, s_state.debug_channel_scroll,
                CODY::SceneData::body_font, s_debug_channel_list);
            if (list_error != ESP_OK) {
                return frame.finish({list_error, false, 0});
            }
        }
#endif
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
    // A deliberate shake may select Home during the canonical motion observation.
    frame_technical = technical_panel_view_active();
    CODY::ScenePresentation presentation{};
    const bool frame_scene_visible = !frame_overlay && !frame_technical && s_state.scene.valid &&
        CODY::lifecycle_is_resting(s_state.lifecycle) && !s_state.resume_scene_pending;
    if (frame_technical) {
        const esp_err_t observation_error = LUNASHELL::DeviceSnapshotService::instance()
            .readCodexTechnicalObservation(s_state.technical_observation);
        if (observation_error != ESP_OK) {
            return frame.finish({observation_error, false, 0});
        }
        const auto &link = s_state.technical_observation.link;
        const auto &technical = s_state.technical_observation.technical;
        const int64_t panel_now_us = esp_timer_get_time();
        const int64_t second = panel_now_us / 1000000;
        const uint32_t status = static_cast<uint32_t>(link.state) |
            (static_cast<uint32_t>(link.data_valid) << 8) |
            (static_cast<uint32_t>(link.data_stale) << 9) |
            (static_cast<uint32_t>(link.activity.data_valid) << 10) |
            (static_cast<uint32_t>(link.activity.data_stale) << 11) |
            (static_cast<uint32_t>(technical.data_valid) << 12) |
            (static_cast<uint32_t>(technical.data_stale) << 13) |
            (static_cast<uint32_t>(technical.model_stale) << 14) |
            (static_cast<uint32_t>(technical.context_stale) << 15);
        esp_err_t panel_error = ESP_OK;
        if (!s_state.technical_panel.valid || s_state.technical_panel.panel != s_state.companion_panel ||
            s_state.panel_activity_revision != link.activity.revision ||
            s_state.panel_technical_revision != technical.revision ||
            s_state.panel_source_status != status || s_state.panel_prepared_second != second) {
            CODY::SceneContent panel_content{};
            panel_error = CODY::prepare_companion_panel_from_observation(
                s_state.companion_panel, link, technical, CODY::SceneData::body_font,
                panel_now_us, s_state.technical_panel, panel_content);
            if (panel_error == ESP_OK) {
                s_state.panel_activity_revision = link.activity.revision;
                s_state.panel_technical_revision = technical.revision;
                s_state.panel_source_status = status;
                s_state.panel_prepared_second = second;
            }
        }
        if (panel_error == ESP_OK) {
            bool animation_changed = false;
            panel_error = CODY::advance_companion_panel(
                s_state.technical_panel, panel_now_us, animation_changed);
        }
        if (panel_error != ESP_OK) {
            return frame.finish({panel_error, false, 0});
        }
    }
    if (frame_scene_visible) {
        const esp_err_t presentation_error = CODY::prepare_scene_presentation(s_state.scene, presentation);
        if (presentation_error != ESP_OK) {
            return frame.finish({presentation_error, false, 0});
        }
    }
    presentation.opacity = static_cast<uint8_t>(
        static_cast<uint32_t>(presentation.opacity) * CODY::lifecycle_opacity(s_state.lifecycle) / 255U);
    if (frame_overlay || !CODY::lifecycle_is_resting(s_state.lifecycle) || s_state.resume_scene_pending) {
        presentation.content_visible = false;
    }
    const bool frame_was_rig = s_state.rig_visible && !presentation.boot_bitmap_visible;
    const bool lifecycle_navigation_allowed = CODY::lifecycle_is_resting(s_state.lifecycle) &&
        !s_state.resume_scene_pending;
    const uint8_t navigation_opacity = s_state.navigation_visibility.opacity(timings.started_us);
    const uint16_t scene_background = frame_scene_visible ? s_state.scene.definition->background_rgb565 :
        CODY::Data::palette[CODY::to_index(CODY::Data::background_color)];
    const bool frame_navigation_visible = !frame_overlay && !frame_technical && !s_state.raster_benchmark_active && kRenderDiagnosticsEnabled && navigation_opacity != 0 &&
        lifecycle_navigation_allowed && !presentation.boot_bitmap_visible;
    const bool frame_status_visible = !frame_overlay && !s_state.raster_benchmark_active && !s_state.onboarding.effective_first_boot &&
        navigation_opacity != 0 && lifecycle_navigation_allowed && frame_was_rig;
    if (s_state.rig_visible && !frame_overlay) {
        const int64_t cue_preparation_started_us = esp_timer_get_time();
        CODY::CodySize personality_size{};
        const float nominal_scale = frame_scene_visible
            ? s_state.scene.definition->cloud.scale : CODY::Data::placement.scale;
        esp_err_t cue_error = CODY::cody_size_for_scale(nominal_scale, personality_size);
        CODY::PersonalityContext context{};
        context.presentation = personality_presentation(frame_was_rig);
        context.nominal_scale = nominal_scale;
        context.route = frame_scene_visible ? static_cast<uint32_t>(s_state.scene.definition->id) : 0;
        context.rotation = s_state.rendered_rotation_degrees;
        if (context.presentation != CODY::PersonalityPresentation::Main || s_state.animation_paused) {
            const bool visible_sleep_handoff =
                context.presentation == CODY::PersonalityPresentation::Sleep && !s_state.animation_paused;
            cancel_transient_affection(visible_sleep_handoff);
        } else {
            s_state.affection_remaining_us -= std::min(s_state.affection_remaining_us,
                                                     s_state.personality_delta_us);
            s_state.eye_reaction_remaining_us -= std::min(s_state.eye_reaction_remaining_us,
                                                         s_state.personality_delta_us);
            if (s_state.eye_reaction_remaining_us == 0) {
                s_state.eye_reaction = CODY::PersonalityCue::None;
            }
            s_state.welcome_remaining_us -= std::min(s_state.welcome_remaining_us,
                                                    s_state.personality_delta_us);
            if (s_state.welcome_remaining_us == 0) {
                s_state.welcome_cue = CODY::PersonalityCue::None;
            }
        }
        const bool sleeping = context.presentation == CODY::PersonalityPresentation::Sleep;
        if (sleeping) {
            s_state.personality_release_sleep = false;
        } else if (s_state.personality_was_sleep) {
            s_state.personality_release_sleep = true;
        }
        s_state.personality_was_sleep = sleeping;
        context.release_immediately = s_state.personality_release_sleep;
        CODY::PersonalityCue cue = sleeping ? CODY::PersonalityCue::Sleep : s_state.requested_personality;
        if (!sleeping && cue == CODY::PersonalityCue::None) {
            cue = CODY::resolve_scene_personality_cue(frame_scene_visible
                ? s_state.scene.definition->id : CODY::SceneId::CompanionIdle,
                s_state.connection_cue);
        }
        if (!sleeping && cue == CODY::PersonalityCue::None && s_state.welcome_remaining_us > 0) {
            cue = s_state.welcome_cue;
        }
        if (!sleeping && cue == CODY::PersonalityCue::None) {
            cue = CODY::character_direction_cue(s_state.character_direction);
        }
        if (cue_error == ESP_OK) {
            // Device truth is checked before artistic arbitration, including while
            // Charging waits behind a still-live heart. No device fact bypasses validity.
            cue_error = resolve_personality_device_cues(cue, s_state.personality_lag.cue,
                                                       cue, context.invalidate_current);
            if (context.presentation == CODY::PersonalityPresentation::Disabled) {
                cue = CODY::PersonalityCue::None;
                s_state.battery_notice.active = CODY::BatteryNotice::None;
                s_state.battery_notice.idle_us = 0;
                s_state.battery_notice_line = {};
            }
            if (cue_error == ESP_OK) {
                bool release_affection = false;
                cue_error = CODY::select_personality_target(cue, sleeping,
                    s_state.affection_remaining_us > 0, cue, release_affection, s_state.eye_reaction);
                if (cue_error == ESP_OK && release_affection) {
                    // Retain current opacity/ownership; the channel completes its smooth exit.
                    s_state.affection_remaining_us = 0;
                }
                if (cue_error == ESP_OK && s_state.eye_reaction != CODY::PersonalityCue::None &&
                    cue != s_state.eye_reaction) {
                    // Urgent device truth drops this contact rather than replaying it later.
                    s_state.eye_reaction = CODY::PersonalityCue::None;
                    s_state.eye_reaction_remaining_us = 0;
                }
                if (cue_error == ESP_OK && s_state.welcome_cue != CODY::PersonalityCue::None &&
                    cue != s_state.welcome_cue) {
                    s_state.welcome_cue = CODY::PersonalityCue::None;
                    s_state.welcome_remaining_us = 0;
                }
            }
        }
        // The full-screen layout reserves status space even when its touch fade is hidden.
        // This prevents touch from resizing or relocating an already visible symbol.
        CODY::DamageRect exclusions[7] = {
            {10, 203, 70, 263}, CODY::status_overlay_damage_bounds()
        };
        uint8_t exclusion_count = 2;
        if (s_state.battery_notice_line.valid) {
            exclusions[exclusion_count++] = {73, 345, 393, 380};
        }
        if (frame_scene_visible) {
            if (s_state.scene.activity_overlay.state != CODY::ActivityOverlayState::Hidden) {
                // Manga keeps its own admitted footprint; activity never follows or covers it.
                exclusions[exclusion_count++] = CODY::activity_overlay_bounds();
            }
            // Connection reactions cannot borrow QR, narrative or action space.
            const auto &content = s_state.scene.content;
            if (content.bounds.right > content.bounds.left && content.bounds.bottom > content.bounds.top) {
                exclusions[exclusion_count++] = content.bounds;
            }
            if (content.bottom_action_visible) {
                exclusions[exclusion_count++] = content.bottom_action_bounds;
            }
            if (s_state.scene.title.valid) {
                const auto &title = s_state.scene.title.bounds;
                exclusions[exclusion_count++] = {title.x, title.y,
                    static_cast<uint16_t>(title.x + title.width),
                    static_cast<uint16_t>(title.y + title.height)};
            }
        }
        if (cue_error == ESP_OK) {
            cue_error = CODY::prepare_personality_frame(
                *s_resources.rig, cue, personality_size, context, s_state.personality_delta_us,
                s_state.animation_paused, resume_animation || !previous_frame_available,
                exclusions, exclusion_count, s_state.personality_lag, s_resources.rig->personality);
        }
        if (cue_error != ESP_OK) {
            return frame.finish({cue_error, false, 0});
        }
        if (s_state.battery_notice_line.valid &&
            (s_resources.rig->personality.cue != CODY::battery_notice_cue(s_state.battery_notice.active) ||
             s_resources.rig->personality.status != CODY::PersonalityPresentationStatus::Visible ||
             s_resources.rig->personality.opacity < 128)) {
            s_state.battery_notice_line = {};
        }
        const auto status = s_resources.rig->personality.status;
        if (status != s_state.personality_status) {
            if (status == CODY::PersonalityPresentationStatus::BlockedByLayout) {
                ESP_LOGW(kTag, "Personality layout blocked: cue=%u presentation=%u; character remains active",
                    static_cast<unsigned>(cue), static_cast<unsigned>(context.presentation));
            } else if (s_state.personality_status == CODY::PersonalityPresentationStatus::BlockedByLayout) {
                ESP_LOGI(kTag, "Personality layout admission changed: status=%u", static_cast<unsigned>(status));
            }
            s_state.personality_status = status;
        }
        if (s_state.personality_lag.cue != CODY::PersonalityCue::Sleep ||
            s_state.personality_lag.phase == CODY::PersonalityPhase::FadeOut) {
            s_state.personality_release_sleep = false;
        }
        timings.geometry_completed_us = esp_timer_get_time();
        timings.geometry_us += timings.geometry_completed_us - cue_preparation_started_us;
    }
    if (frame_overlay) {
        // Symbols and motion marks cannot fade through the isolated diagnostic face.
        s_resources.rig->personality = {};
    }
    if (!frame_was_rig || frame_overlay) {
        s_state.battery_notice_line = {};
        s_state.battery_notice.active = CODY::BatteryNotice::None;
        s_state.battery_notice.idle_us = 0;
    }
    if (frame_status_visible &&
        (s_state.status_sampled_at_us == 0 ||
         timings.started_us - s_state.status_sampled_at_us >= kStatusRefreshUs)) {
        LUNASHELL::DeviceStatusObservation observation;
        const esp_err_t observation_error =
            LUNASHELL::DeviceSnapshotService::instance().readDeviceStatusObservation(observation);
        if (observation_error != ESP_OK) {
            return frame.finish({observation_error, false, 0});
        }
        CODY::StatusOverlay next_status;
        const esp_err_t status_error = CODY::prepare_status_overlay(observation, next_status);
        if (status_error != ESP_OK) {
            return frame.finish({status_error, false, 0});
        }
        s_state.status_overlay = next_status;
        s_state.status_sampled_at_us = timings.started_us;
    }
    CODY::ScreenshotCapture screenshot_capture;
    const esp_err_t screenshot_begin_error = CODY::begin_screenshot_capture(
        kPanelWidth, kPanelHeight, screenshot_capture
    );
    if (screenshot_begin_error != ESP_OK && screenshot_begin_error != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(kTag, "On-demand screenshot capture start failed (%s)",
                 esp_err_to_name(screenshot_begin_error));
    }
    const bool screenshot_capture_active = screenshot_capture.pixels != nullptr;
    const CODY::ScreenshotState frame_screenshot_state = CODY::screenshot_state();
    const CaptureVisual frame_capture_visual = capture_visual();
    CODY2D::TextLine capture_feedback_line{};
    if (frame_capture_visual == CaptureVisual::Saving ||
        frame_capture_visual == CaptureVisual::Saved ||
        frame_capture_visual == CaptureVisual::Failed) {
        const esp_err_t feedback_error = prepare_capture_feedback(
            frame_capture_visual, capture_feedback_line);
        if (feedback_error != ESP_OK) {
            if (screenshot_capture_active) {
                (void)CODY::finish_screenshot_capture(screenshot_capture, feedback_error);
            }
            return frame.finish({feedback_error, false, 0});
        }
    }
    const bool frame_readout_visible = !s_state.raster_benchmark_active && kRenderDiagnosticsEnabled &&
        ((frame_overlay && !frame_channel_list) ||
         (measured_readout_route_active() && s_state.debug_readout_enabled && presentation.content_visible &&
          !footer_blocks_readout && (!s_state.scene.valid || s_state.scene.definition->readout_visible)));
    // Fade a bounded palette once, not 466x466 boot pixels or the physical
    // backlight setting. The immutable source asset remains untouched.
    CODY2D::IndexedRleImage boot_image = *image;
    uint16_t faded_palette[kPaletteLimit];
    if (presentation.boot_bitmap_visible && presentation.opacity != 255) {
        for (uint16_t index = 0; index < image->palette_count; ++index) {
            faded_palette[index] = CODY2D::fade_rgb565_to_black(image->palette[index], presentation.opacity);
        }
        boot_image.palette = faded_palette;
    }
    const int64_t damage_started_us = timings.geometry_completed_us;
    // Geometry can enter/leave the reusable routine scene. Decide reuse after
    // that transition, or old title pixels would survive (or damage would fail).
    const bool reuse_previous = !frame_overlay && previous_frame_available && !resume_animation &&
        s_rendered_revision.load(std::memory_order_acquire) == revision &&
        s_state.rig_visible && s_cache_was_rig &&
        s_cached_companion_panel == (frame_technical ? s_state.companion_panel : CODY::CompanionPanel::Home) &&
        s_cached_scene_visible == frame_scene_visible &&
        s_cached_scene.valid == s_state.scene.valid &&
        s_cached_scene.definition == s_state.scene.definition;
    esp_err_t plan_error = ESP_OK;
    if (!reuse_previous || presentation.boot_bitmap_visible || screenshot_capture_active) {
        plan_error = CODY::include_damage(dirty_plan, {0, 0, kPanelWidth, kPanelHeight});
    } else if (frame_technical) {
        // The detail layer is transparent; keep the character's old and new pixels in damage.
        plan_error = CODY::plan_rig_damage(*s_resources.previous_rig, *s_resources.rig,
            false, false, CODY::kNoPoint, CODY::kNoPoint, dirty_plan);
        if (plan_error == ESP_OK && s_cached_panel_revision != s_state.technical_panel.revision) {
            plan_error = CODY::include_damage(dirty_plan, CODY::kCompanionPanelContentBounds);
        }
        if (plan_error == ESP_OK &&
            (s_cached_navigation_opacity != navigation_opacity ||
             s_cached_status_visible != frame_status_visible ||
             !CODY::status_overlay_equal(s_cached_status_overlay, s_state.status_overlay))) {
            plan_error = CODY::include_damage(dirty_plan, CODY::status_overlay_damage_bounds());
        }
    } else if (s_state.raster_benchmark_active) {
        // A static pose would otherwise disappear from the dirty plan after its first frame.
        // Force the exact same artwork region; warmup excludes the initial full-canvas clear.
        plan_error = CODY::include_rig_artwork_damage(*s_resources.rig, dirty_plan);
    } else {
        plan_error = CODY::plan_rig_damage(*s_resources.previous_rig, *s_resources.rig,
            s_cached_rig_debug, frame_debug, s_cached_selected_point, s_state.selected_point,
            dirty_plan);
        if (plan_error == ESP_OK && s_state.scene.valid) {
            plan_error = CODY::plan_scene_damage(s_cached_scene, s_state.scene, dirty_plan);
            CODY::ScenePresentation previous_presentation{};
            if (plan_error == ESP_OK) {
                plan_error = CODY::prepare_scene_presentation(s_cached_scene, previous_presentation);
            }
            if (plan_error == ESP_OK && previous_presentation.opacity != presentation.opacity) {
                // Unchanged geometry still needs every painted pixel updated
                // when layer brightness changes. Retain old/new footprints.
                plan_error = CODY::include_rig_artwork_damage(*s_resources.previous_rig, dirty_plan);
                if (plan_error == ESP_OK) {
                    plan_error = CODY::include_rig_artwork_damage(*s_resources.rig, dirty_plan);
                }
            }
        }
        if (plan_error == ESP_OK && kRenderDiagnosticsEnabled &&
            (s_cached_readout_visible != frame_readout_visible ||
             (frame_readout_visible &&
              (std::strcmp(s_cached_readout.frame_rate, debug_readout.frame_rate) != 0 ||
               std::strcmp(s_cached_readout.processors, debug_readout.processors) != 0 ||
               std::strcmp(s_cached_readout.network, debug_readout.network) != 0 ||
               std::strcmp(s_cached_readout.memory, debug_readout.memory) != 0 ||
               std::strcmp(s_cached_readout.storage_state, debug_readout.storage_state) != 0 ||
               std::strcmp(s_cached_readout.storage_free, debug_readout.storage_free) != 0)))) {
            plan_error = CODY::include_damage(dirty_plan, CODY::debug_readout_damage_bounds());
        }
        if (plan_error == ESP_OK && s_cached_navigation_visible != frame_navigation_visible) {
            plan_error = include_control_damage(
                dirty_plan, CODY::RigControl::Screenshot
            );
        }
        if (plan_error == ESP_OK && s_cached_navigation_opacity != navigation_opacity) {
            if (frame_navigation_visible || s_cached_navigation_visible) {
                plan_error = CODY::include_damage(
                    dirty_plan, CODY::rig_navigation_damage_bounds()
                );
            }
            if (plan_error == ESP_OK &&
                (frame_status_visible || s_cached_status_visible)) {
                plan_error = CODY::include_damage(
                    dirty_plan, CODY::status_overlay_damage_bounds()
                );
            }
        }
        if (plan_error == ESP_OK &&
            (s_cached_status_visible != frame_status_visible ||
             (frame_status_visible && !CODY::status_overlay_equal(
                 s_cached_status_overlay, s_state.status_overlay)))) {
            plan_error = CODY::include_damage(
                dirty_plan, CODY::status_overlay_damage_bounds()
            );
        }
        if (plan_error == ESP_OK && frame_navigation_visible &&
            s_cached_screenshot_state != frame_screenshot_state) {
            plan_error = include_control_damage(dirty_plan, CODY::RigControl::Screenshot);
        }
        if (plan_error == ESP_OK && s_cached_capture_visual != frame_capture_visual) {
            plan_error = CODY::include_damage(dirty_plan, kCaptureOverlayBounds);
        }
    }
    timings.damage_us = esp_timer_get_time() - damage_started_us;
    if (plan_error == ESP_OK && (s_state.battery_notice_line.valid ||
        s_state.retired_battery_notice != CODY::BatteryNotice::None)) {
        plan_error = CODY::include_damage(dirty_plan, {73, 345, 393, 380});
    }
    if (plan_error != ESP_OK) {
        if (screenshot_capture_active) {
            (void)CODY::finish_screenshot_capture(screenshot_capture, plan_error);
        }
        return frame.finish({plan_error, false, 0});
    }
    esp_err_t result = ESP_OK;
    bool parked_during_frame = false;
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
            parked_during_frame = renderer_is_parked();
            result = ESP_ERR_INVALID_STATE;
            break;
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
            // Reborrow for every dirty tile. The previously submitted slot is
            // immutable until DMA completion; a second slot overlaps its transfer.
            const int64_t borrow_started_us = esp_timer_get_time();
            size_t slot_capacity_bytes = 0;
            result = LUNASHELL_display_frame_buffer(&raster_storage, &slot_capacity_bytes);
            timings.transfer_us += esp_timer_get_time() - borrow_started_us;
            if (result != ESP_OK) {
                break;
            }
            if (raster_storage == nullptr || slot_capacity_bytes != raster_capacity_bytes ||
                reinterpret_cast<uintptr_t>(raster_storage) % alignof(uint16_t) != 0) {
                ESP_LOGE(kTag, "Borrowed frame slot changed capacity/alignment: bytes=%u (%s)",
                    static_cast<unsigned>(slot_capacity_bytes), esp_err_to_name(ESP_ERR_INVALID_SIZE));
                result = ESP_ERR_INVALID_SIZE;
                break;
            }
            const uint16_t width = damage.right - damage.left;
            const uint16_t height = damage.bottom - damage.top;
            // Raster directly into the leased DMA base with a compact stride.
            // All current layers are recomposed, including unchanged overlaps.
            const CODY2D::Surface strip{
                static_cast<uint16_t *>(raster_storage), width, height, width,
                static_cast<uint32_t>(raster_capacity_bytes / sizeof(uint16_t)),
                CODY2D::Rgb565ByteOrder::ByteSwapped
            };
            const int64_t raster_started_us = esp_timer_get_time();
            if (frame_was_rig) {
                result = frame_channel_list ? CODY2D::clear(strip, scene_background) : CODY::draw_rig_rows(
                    *s_resources.rig, strip, damage.top, frame_debug, s_state.selected_point,
                    kExecutionTimingsEnabled ? &timings.raster_profile : nullptr,
                    s_state.animation_paused, damage.left,
                    frame_debug ? s_resources.rig_debug_strokes : nullptr,
                    false, navigation_opacity, scene_background,
                    screenshot_control_state(frame_screenshot_state),
                    frame_scene_visible ? &s_state.scene.activity_overlay : nullptr
                );
                if (result == ESP_OK && presentation.opacity != 255) {
                    result = CODY2D::fade_to_black(strip, presentation.opacity);
                }
                if (result == ESP_OK && frame_scene_visible && presentation.content_visible) {
                    result = CODY::draw_scene_rows(s_state.scene, strip, damage.top, damage.left);
                }
                if (result == ESP_OK && s_state.battery_notice_line.valid) {
                    result = CODY2D::draw_text_rows(s_state.battery_notice_line,
                        s_state.battery_notice_line.text.length, strip, damage.top, damage.left);
                }
                if (result == ESP_OK && frame_technical) {
                    result = CODY::draw_companion_panel_rows(
                        s_state.technical_panel, strip, damage.top, damage.left);
                }
                if (result == ESP_OK && presentation.content_visible && !frame_technical) {
                    result = CODY::draw_onboarding_navigation(strip, damage.top, damage.left);
                }
                if (result == ESP_OK && frame_status_visible) {
                    result = CODY::draw_status_overlay_rows(
                        s_state.status_overlay, navigation_opacity, scene_background,
                        strip, damage.top, damage.left
                    );
                }
                if (result == ESP_OK && frame_navigation_visible && !frame_technical) {
                    result = CODY::draw_rig_navigation_rows(
                        strip, damage.top, frame_debug, s_state.animation_paused,
                        damage.left, navigation_opacity, scene_background,
                        screenshot_control_state(frame_screenshot_state),
                        kExecutionTimingsEnabled ? &timings.raster_profile : nullptr
                    );
                }
                if (result == ESP_OK && frame_readout_visible) {
                    result = CODY::draw_debug_readout_rows(
                        debug_readout, strip, damage.top, damage.left, scene_background
                    );
                }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
                if (result == ESP_OK && frame_channel_list) {
                    result = CODY::draw_debug_channel_list_rows(s_debug_channel_list, strip, damage.top, damage.left);
                }
                if (result == ESP_OK && frame_overlay) {
                    result = CODY::draw_debug_view_back_rows(strip, damage.top, damage.left);
                }
#endif
            } else {
                // Boot assets invalidate the entire canvas, so their decoder
                // still receives full-width rows with no horizontal crop.
                result = CODY2D::decode_rows(boot_image, damage.top, strip);
            }
            if (result == ESP_OK) {
                result = draw_capture_overlay(frame_capture_visual,
                    capture_feedback_line.valid ? &capture_feedback_line : nullptr,
                    strip, damage.top, damage.left);
            }
            timings.raster_us += esp_timer_get_time() - raster_started_us;
            if (result == ESP_OK && screenshot_capture_active) {
                result = CODY::capture_screenshot_rows(
                    screenshot_capture, strip, damage.top, damage.left
                );
            }
            if (result == ESP_OK) {
                const int64_t transfer_started_us = esp_timer_get_time();
                result = LUNASHELL_display_frame_submit_rgb565(
                    damage.left, damage.top, damage.right, damage.bottom, strip.pixels);
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
            const TouchPollResult touch = capture_pending_touch(pending_touch);
            if (touch.status == TouchPollStatus::Failed) {
                if (optional_runtime_touch_failure(touch)) {
                    pending_touch.count = 0;
                    interrupt_touch_gestures(touch.error);
                } else {
                    result = fail_consumer("Touch report during frame", touch.error, true);
                    break;
                }
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
    const DrawResult completed = frame.finish({result, parked_during_frame, completed_revision});
    timings.frame_end_completed_us = esp_timer_get_time();
    // The last queued strip must retire inside the measured frame, not after it.
    timings.transfer_us += timings.frame_end_completed_us - timings.frame_end_started_us;
    if (screenshot_capture_active) {
        const esp_err_t screenshot_finish_error = CODY::finish_screenshot_capture(
            screenshot_capture, completed.error
        );
        if (screenshot_finish_error != ESP_OK && completed.error == ESP_OK) {
            // Screenshot persistence is an explicit optional operation. Its own
            // control remains red while the successfully completed display frame lives on.
            ESP_LOGE(kTag, "Completed frame screenshot submission failed (%s)",
                     esp_err_to_name(screenshot_finish_error));
        }
    }
    s_resources.previous_frame_valid = completed.error == ESP_OK;
    if (completed.error == ESP_OK) {
        // Commit geometry only after every panel write and frame retirement.
        // Panel-order raster bytes live only in the borrowed DMA tile, never in this reference.
        if (frame_was_rig) {
            *s_resources.previous_rig = *s_resources.rig;
        }
        const CODY::PersonalityFrame previous_personality = s_state.retired_personality;
        s_state.retired_personality = frame_was_rig ? s_resources.rig->personality : CODY::PersonalityFrame{};
        s_state.retired_battery_notice = s_state.battery_notice_line.valid ?
            s_state.battery_notice.active : CODY::BatteryNotice::None;
        notify_completed_personality(previous_personality, s_state.retired_personality,
                                     main_companion_view_active(frame_was_rig));
        notify_completed_idle_scene(main_companion_view_active(frame_was_rig));
        s_cache_was_rig = frame_was_rig;
        s_cached_rig_debug = frame_debug;
        s_cached_selected_point = s_state.selected_point;
        s_cached_readout = debug_readout;
        s_cached_readout_visible = frame_readout_visible;
        s_cached_navigation_visible = frame_navigation_visible;
        s_cached_navigation_opacity = navigation_opacity;
        s_cached_status_overlay = s_state.status_overlay;
        s_cached_status_visible = frame_status_visible;
        s_cached_screenshot_state = frame_screenshot_state;
        s_cached_capture_visual = frame_capture_visual;
        s_cached_scene = s_state.scene;
        s_cached_scene_visible = frame_scene_visible;
        s_cached_companion_panel = frame_technical ? s_state.companion_panel : CODY::CompanionPanel::Home;
        s_cached_panel_revision = s_state.technical_panel.revision;
        const esp_err_t audio_error = (frame_overlay || frame_technical) ? ESP_OK :
            notify_completed_typewriter(s_cached_scene, presentation);
        if (audio_error != ESP_OK) {
            // The audio owner already retains/publishes its real failure. This
            // optional cue cannot invalidate a successfully retired panel frame.
            log_optional_typewriter_result(audio_error);
        }
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
    if (completed.error != ESP_OK) {
        // Failure timings distinguish geometry/raster scheduling from DMA waits;
        // log only after both frame retirement and the outer panel lock release.
        ESP_LOGE(kTag, "Failed frame timing: error=%s total_us=%lld geometry_us=%lld raster_us=%lld "
            "transfer_us=%lld rectangles=%u tiles=%u touch_polls=%u",
            esp_err_to_name(completed.error),
            static_cast<long long>(timings.frame_end_completed_us - timings.frame_begin_completed_us),
            static_cast<long long>(timings.geometry_us), static_cast<long long>(timings.raster_us),
            static_cast<long long>(timings.transfer_us), static_cast<unsigned>(timings.dirty_rectangles),
            static_cast<unsigned>(timings.rasterized_tiles), static_cast<unsigned>(timings.touch_polls));
    }
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
    if (requested_phase == BootPhase::Ready && s_state.branding_complete && !s_state.deep_sleep_wake &&
        s_state.onboarding.force_first_boot) {
        s_greeting_due_us = esp_timer_get_time() + CODY::SceneData::boot_delay_ms * 1000LL;
        s_greeting_scheduled.store(true, std::memory_order_release);
        ESP_LOGI(kTag, "First-boot greeting scheduled %lu ms after completed ready frame",
            static_cast<unsigned long>(CODY::SceneData::boot_delay_ms));
    }
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
    ReturnToIdle,
    ToggleOverlay,
    TogglePause,
    SelectPoint,
};

// point: Real logical touch coordinate already mapped by the display owner.
TouchAction classify_rig_touch(const LUNASHELL_touch_point_t &point)
{
    if constexpr (!kRenderDiagnosticsEnabled) {
        return TouchAction::None;
    }
    if (!CODY::lifecycle_is_resting(s_state.lifecycle) || s_state.resume_scene_pending) {
        return TouchAction::None;
    }
    if (s_state.navigation_visibility.opacity(esp_timer_get_time()) != 255) {
        return s_state.rig_debug ? TouchAction::SelectPoint : TouchAction::None;
    }
    switch (CODY::rig_control_at({static_cast<float>(point.x), static_cast<float>(point.y)})) {
    case CODY::RigControl::Back:
        return TouchAction::ReturnToIdle;
    case CODY::RigControl::Overlay:
        return TouchAction::ToggleOverlay;
    case CODY::RigControl::Pause:
        return TouchAction::TogglePause;
    case CODY::RigControl::Screenshot:
        return TouchAction::None;
    case CODY::RigControl::None:
        break;
    }
    return s_state.rig_debug ? TouchAction::SelectPoint : TouchAction::None;
}

// No parameters: touch and automatic demo share the same clocks, buffers and model.
// Caller owns the display/state lock; entering does not draw while another frame is active.
// Exception: Returns the actual transient scene-policy error before changing the model.
esp_err_t enter_animation_demo()
{
    const esp_err_t policy_error = apply_scene_standby_policy({});
    if (policy_error != ESP_OK) {
        return policy_error;
    }
    s_state.scene = {};
    s_state.resume_scene_pending = false;
    s_state.rig_visible = true;
    // Start with the character artwork; the lateral overlay control still
    // exposes the complete rig when the user wants to inspect its geometry.
    s_state.rig_debug = false;
    s_state.animation_paused = false;
    s_state.animation_elapsed_us = 0;
    s_state.contour = CODY::make_contour_state();
    s_state.face = CODY::Data::ready;
    s_state.response = CODY::make_response_state(CODY::Data::response);
    s_state.response.idle_random_state = esp_random() | 1U;
    s_state.response_sources_logged = false;
    s_state.animation_sampled_us = 0;
    s_state.last_timing_log_us = 0;
    s_state.rig_frame_count = 0;
    s_state.frame_rate = {};
    s_benchmark.reset();
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Animation demo entered: %s; authored targets and real snapshot stimuli", CODY::Data::character_name);
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// screen: Validated reversible overlay mode; Normal restores the retained route.
// scroll: Validated absolute channel row, zero for modes without a list.
void apply_debug_view(LUNASHELL::CodexDebugScreen screen, uint8_t scroll)
{
    s_state.debug_screen = screen;
    s_state.debug_channel_scroll = scroll;
    s_state.debug_scroll_contact = false;
    s_state.debug_placement_dirty = true;
    s_state.body_drag_active = false;
    s_state.setup_hold_candidate = false;
    s_state.setup_hold_eligible = false;
    // Consume an already held contact through its acquired release after restoration.
    s_state.navigation_touch_active = s_state.touch_pressed;
    CODY::cancel_onboarding_touch();
    cancel_transient_affection();
    CODY::clear_personality_presentation(s_state.personality_lag);
    s_resources.previous_frame_valid = false;
    s_state.onboarding_sampled_us = 0;
    s_state.frame_dirty.store(true, std::memory_order_release);
}
#endif

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
// optional_rejection: True only after a real rejection was acknowledged and canonically published.
// Exception: Returns the original application, lock or completion error; absence is not a failure.
esp_err_t apply_debug_screen_requests(bool &optional_rejection)
{
    optional_rejection = false;
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    LUNASHELL::CodexDebugScreenRequest request;
    const esp_err_t take_error = LUNASHELL::takeCodexDebugScreen(request);
    if (take_error == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (take_error != ESP_OK) {
        return take_error;
    }
    esp_err_t application_error = ESP_OK;
    if (request.screen != LUNASHELL::CodexDebugScreen::Normal &&
        request.screen != LUNASHELL::CodexDebugScreen::FaceChannels &&
        request.screen != LUNASHELL::CodexDebugScreen::Character) {
        application_error = ESP_ERR_INVALID_ARG;
    } else if ((request.screen == LUNASHELL::CodexDebugScreen::FaceChannels &&
                request.scroll >= CODY::kFaceChannelCount) ||
               (request.screen != LUNASHELL::CodexDebugScreen::FaceChannels && request.scroll != 0)) {
        application_error = ESP_ERR_INVALID_ARG;
    } else if (s_state.boot_phase != BootPhase::Ready || !s_state.branding_complete ||
               !s_state.rig_visible || s_resources.rig == nullptr || !s_resources.rig->valid ||
               !CODY::lifecycle_is_resting(s_state.lifecycle) || s_state.resume_scene_pending ||
               s_state.raster_benchmark_active || s_capture_sequence_active.load(std::memory_order_acquire) ||
               s_capture_request_pending.load(std::memory_order_acquire) || capture_visual() != CaptureVisual::None ||
               !LUNASHELL_display_runtime_writes_allowed()) {
        application_error = ESP_ERR_INVALID_STATE;
    } else {
        const CODY::Transform placement = request.screen == LUNASHELL::CodexDebugScreen::Normal
            ? s_state.response_frame.transform : CODY::Transform{233, 205, 130};
        application_error = CODY::validate_rig_pose(s_state.response_frame.face, placement);
        if (application_error == ESP_OK && request.screen == LUNASHELL::CodexDebugScreen::FaceChannels) {
            // Preflight in the stable owner: TextLine borrows strings and cannot be copied.
            application_error = CODY::prepare_debug_channel_list(s_state.response_frame.face,
                request.scroll, CODY::SceneData::body_font, s_debug_channel_list);
        }
    }
    if (application_error == ESP_OK) {
        apply_debug_view(request.screen, request.scroll);
    } else {
        ESP_LOGE(kTag, "Debug screen request rejected: mode=%u scroll=%u cause=%s",
            static_cast<unsigned>(request.screen), request.scroll, esp_err_to_name(application_error));
    }
    const esp_err_t completion_error = LUNASHELL::completeCodexDebugScreen(
        request, application_error, CODY::kFaceChannelCount);
    if (completion_error != ESP_OK) {
        ESP_LOGE(kTag, "Debug screen completion failed: %s", esp_err_to_name(completion_error));
        return completion_error;
    }
    // The transport owner publishes rejected optional requests without failing the display stage.
    optional_rejection = application_error != ESP_OK;
    return application_error;
}
#endif

// No parameters: the existing worker owns both startup intents and the scene transition.
// Exception: Returns the real lock, onboarding, policy or scene preparation failure.
esp_err_t apply_scene_requests()
{
    {
        const esp_err_t route_lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
        if (route_lock_error != ESP_OK) {
            return route_lock_error;
        }
        const DisplayUnlockOnExit route_lock;
        if (s_state.boot_phase != BootPhase::Ready || !s_state.branding_complete ||
            debug_view_active() ||
            !CODY::lifecycle_is_resting(s_state.lifecycle) ||
            !LUNASHELL_display_runtime_writes_allowed()) {
            // Keep both the current route and any action-worker result intact
            // until Wake finishes. No hidden scene consumes or loses a mailbox.
            s_state.onboarding_sampled_us = 0;
            return ESP_OK;
        }
        if constexpr (kRasterBenchmarkEnabled) {
            if (s_state.boot_phase == BootPhase::Ready && !s_state.raster_benchmark_attempted) {
                return begin_raster_benchmark();
            }
            if (s_state.raster_benchmark_active) {
                if (s_capture_request_pending.load(std::memory_order_acquire) ||
                    s_capture_sequence_active.load(std::memory_order_acquire) ||
                    capture_visual() != CaptureVisual::None) {
                    return stop_raster_benchmark(false, "power-capture");
                }
                if (s_lifecycle_request.load(std::memory_order_acquire) != s_state.lifecycle_request) {
                    return stop_raster_benchmark(false, "power-lifecycle");
                }
                if (LUNASHELL_display_runtime_content_revision() != s_state.raster_benchmark_revision) {
                    return stop_raster_benchmark(false, "display-revision");
                }
                if (s_resources.raster_benchmark->due(esp_timer_get_time())) {
                    const esp_err_t result = s_resources.raster_benchmark->finish(esp_timer_get_time());
                    if (result != ESP_OK) {
                        return result;
                    }
                    return stop_raster_benchmark(true, "window-complete");
                }
                return ESP_OK;
            }
        }
        // Inactivity is awake time, independent of the Debug animation pause.
        // Reset sampling while hidden so a long standby never expires a route.
        const int64_t onboarding_now_us = esp_timer_get_time();
        if (s_state.onboarding_sampled_us > 0 &&
            onboarding_now_us >= s_state.onboarding_sampled_us) {
            const int64_t delta_us = onboarding_now_us - s_state.onboarding_sampled_us;
            const int64_t remaining_us = std::numeric_limits<int64_t>::max() -
                s_state.onboarding_elapsed_us;
            s_state.onboarding_elapsed_us += std::min(delta_us, remaining_us);
        }
        s_state.onboarding_sampled_us = onboarding_now_us;
        if (kRenderDiagnosticsEnabled || !s_state.onboarding.effective_first_boot) {
            const int64_t now_us = esp_timer_get_time();
            // No startup priming: only real touch reveals shared status/debug chrome.
            s_state.navigation_visibility.update(now_us);
            if (s_state.navigation_visibility.opacity(now_us) != s_cached_navigation_opacity) {
                s_state.frame_dirty.store(true, std::memory_order_release);
            }
        }
        if (s_state.capture_feedback_visible &&
            capture_visual() != s_cached_capture_visual) {
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
        bool route_changed = false;
        const esp_err_t route_error = CODY::update_onboarding(
            s_state.scene, s_cached_scene, s_state.animation_elapsed_us,
            s_state.onboarding_elapsed_us, route_changed);
        if (route_error != ESP_OK) {
            return route_error;
        }
        const esp_err_t policy_error = apply_scene_standby_policy(s_state.scene);
        if (policy_error != ESP_OK) {
            return policy_error;
        }
        if (s_state.resume_scene_pending) {
            // Reconcile the real backend before exposing the retained scene.
            // A session closed by standby may now require a truthful new route.
            const esp_err_t paused_model_error = restore_paused_scene_model();
            if (paused_model_error != ESP_OK) {
                return paused_model_error;
            }
            s_state.resume_scene_pending = false;
            s_state.animation_paused = s_state.animation_paused_before_standby;
            s_state.rig_debug = s_state.rig_debug_before_standby;
            s_state.animation_sampled_us = 0;
            route_changed = true;
            ESP_LOGI(kTag, "Wake resumed scene: %s; preserved active text/entrance clocks",
                     s_state.scene.valid ? s_state.scene.definition->name : "idle animation");
        }
        if (route_changed) {
            s_state.routine_scene = false;
            s_state.routine_greeting_pending = false;
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
    }
    const bool greeting_due = s_greeting_scheduled.load(std::memory_order_acquire) &&
        esp_timer_get_time() >= s_greeting_due_us;
    const bool demo_requested = s_demo_requested.exchange(false, std::memory_order_acq_rel);
    if (!greeting_due && !demo_requested) {
        return ESP_OK;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    if (s_state.boot_phase != BootPhase::Ready) {
        ESP_LOGE(kTag, "Scene entry rejected before boot completion (%s)",
            esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (greeting_due) {
        if (!CODY::lifecycle_is_resting(s_state.lifecycle)) {
            return ESP_OK;
        }
        const CODY::AtlasScreenDefinition *screen = nullptr;
        esp_err_t error = CODY::find_atlas_screen(CODY::AtlasScreenId::Greeting04, screen);
        if (error != ESP_OK) {
            return error;
        }
        if (!screen->scene.has_value()) {
            ESP_LOGE(kTag, "Atlas 04 has no implemented scene (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        const CODY::SceneDefinition *definition = nullptr;
        error = CODY::find_scene(*screen->scene, definition);
        CODY::SceneFrame prepared{};
        if (error == ESP_OK) {
            error = CODY::begin_scene(*definition, prepared);
        }
        if (error != ESP_OK) {
            return error;
        }
        // Startup already has the animated Sleeping/Waking/Ready sequence. Keep the
        // optional first-boot text, without replacing it with a static bitmap.
        prepared.intro_phase = CODY::SceneIntroPhase::Complete;
        prepared.intro_elapsed_us = 0;
        if (!s_state.rig_visible) {
            error = enter_animation_demo();
            if (error != ESP_OK) {
                return error;
            }
        }
        error = apply_scene_standby_policy(prepared);
        if (error != ESP_OK) {
            return error;
        }
        // Text and placement share the scene; facial lifecycle keeps its own owner.
        s_state.scene = prepared;
        CODY::begin_onboarding();
        s_state.greeting_entered = true;
        s_state.routine_greeting_pending = false;
        s_state.routine_scene = false;
        // Start measurement windows with screen 04, excluding completed boot frames.
        s_state.frame_rate = {};
        s_state.rig_frame_count = 0;
        s_state.last_timing_log_us = 0;
        s_benchmark.reset();
        s_state.frame_dirty.store(true, std::memory_order_release);
        s_greeting_scheduled.store(false, std::memory_order_release);
        ESP_LOGI(kTag, "Scene entered: %s; text over the continuous expression/breath channels",
            definition->name);
        return ESP_OK;
    }
    if (!s_state.rig_visible) {
        return enter_animation_demo();
    }
    return ESP_OK;
}

// point: One acquired press, move or release; its original timestamp is retained.
// pressed_edge: Whether this acquired report starts a new gesture.
// Exception: Returns the original body/caress geometry or snapshot publication failure.
esp_err_t publish_body_gesture(const LUNASHELL_touch_point_t &point, bool pressed_edge)
{
    if (pressed_edge) {
        s_state.body_drag_active = false;
        const bool interactive_scene = !s_state.scene.valid ||
            s_state.scene.intro_phase == CODY::SceneIntroPhase::Complete;
        const TouchAction action = s_state.rig_visible ? classify_rig_touch(point) : TouchAction::None;
        if (s_state.rig_visible && interactive_scene && !s_state.animation_paused &&
            s_resources.previous_frame_valid && s_cache_was_rig &&
            (action == TouchAction::None || action == TouchAction::SelectPoint)) {
            bool hit = false;
            const esp_err_t hit_error = CODY::hit_test_body(*s_resources.rig,
                {static_cast<float>(point.x), static_cast<float>(point.y)}, hit);
            if (hit_error != ESP_OK) {
                return hit_error;
            }
            if (hit) {
                s_state.body_drag_active = true;
                s_state.body_drag = {};
                s_state.body_drag.origin_x = point.x;
                s_state.body_drag.origin_y = point.y;
                s_state.body_drag.gesture_started_at_us = point.sample_timestamp_us;
                s_state.body_drag.rendering_rotation_degrees = s_state.rendered_rotation_degrees;
            }
        }
    }
    if (!s_state.body_drag_active) {
        if (pressed_edge) {
            ESP_LOGI(kTag, "Caress contact excluded: drag=0 x=%u y=%u", point.x, point.y);
        }
        CODY::cancel_petting(s_state.petting);
        return ESP_OK;
    }
    s_state.body_drag.pressed = point.pressed;
    s_state.body_drag.x = point.x;
    s_state.body_drag.y = point.y;
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().reportCharacterTouch(
        s_state.body_drag, point.sample_timestamp_us);
    if (error != ESP_OK) {
        return error;
    }
    const bool petting_allowed = main_companion_view_active(true) &&
        personality_presentation(true) == CODY::PersonalityPresentation::Main &&
        !s_state.animation_paused && !s_state.rig_debug && !s_state.resume_scene_pending;
    if (pressed_edge) {
        ESP_LOGI(kTag, "Caress contact start: allowed=%u presentation=%u paused=%u debug=%u x=%u y=%u",
            static_cast<unsigned>(petting_allowed), static_cast<unsigned>(personality_presentation(true)),
            static_cast<unsigned>(s_state.animation_paused), static_cast<unsigned>(s_state.rig_debug),
            point.x, point.y);
    }
    if (petting_allowed) {
        if (!point.pressed && s_state.petting.contact_active) {
            // Observe before the recognizer clears the released contact. These
            // are actual acquired timestamps/coordinates, not synthetic samples.
            const auto &contact = s_state.petting;
            ESP_LOGI(kTag, "Caress contact end: zone=%u triggered=%u rejected=%u cause=%s duration_us=%lld path_px=%.1f",
                static_cast<unsigned>(contact.zone), static_cast<unsigned>(contact.triggered),
                static_cast<unsigned>(contact.rejected),
                CODY::petting_rejection_name(contact.rejection),
                static_cast<long long>(point.sample_timestamp_us - contact.started_at_us),
                static_cast<double>(contact.path_pixels));
        }
        CODY::PettingZone zone{};
        const bool previously_rejected = !pressed_edge && s_state.petting.rejected;
        const esp_err_t petting_error = CODY::observe_petting(*s_resources.rig,
            {static_cast<float>(point.x), static_cast<float>(point.y)}, point.pressed,
            pressed_edge, point.sample_timestamp_us, s_state.petting, zone);
        if (petting_error != ESP_OK) {
            return petting_error;
        }
        if (s_state.petting.rejected && !previously_rejected) {
            const auto &contact = s_state.petting;
            ESP_LOGI(kTag, "Caress rejected: cause=%s zone=%u next_zone=%u body=%u at=%lld gap_us=%lld",
                CODY::petting_rejection_name(contact.rejection), static_cast<unsigned>(contact.zone),
                static_cast<unsigned>(contact.rejected_zone), static_cast<unsigned>(contact.rejected_body_hit),
                static_cast<long long>(contact.rejected_at_us),
                static_cast<long long>(contact.rejected_sample_gap_us));
            char speed[24] = "N/A";
            if (contact.rejected_speed_valid) {
                std::snprintf(speed, sizeof(speed), "%.1f",
                    static_cast<double>(contact.rejected_speed_pixels_per_second));
            }
            ESP_LOGI(kTag, "Caress rejection geometry: x=%.1f y=%.1f scale=%.1f speed=%s path_px=%.1f",
                static_cast<double>(contact.rejected_point.x), static_cast<double>(contact.rejected_point.y),
                static_cast<double>(contact.contact_scale), speed, static_cast<double>(contact.path_pixels));
        }
        if (zone != CODY::PettingZone::None) {
            // Mood belongs to the recognized interaction, even if another symbol suppresses the heart.
            const esp_err_t clock_error = advance_happiness_clock_locked(esp_timer_get_time(), true);
            if (clock_error != ESP_OK) {
                return clock_error;
            }
            const esp_err_t happiness_error = CODY::apply_happiness_event(
                CODY::PersonalityData::happiness, CODY::PettingConfig::event, s_state.happiness);
            if (happiness_error != ESP_OK) {
                return happiness_error;
            }
            s_state.last_caress_at_us = point.sample_timestamp_us;
            ESP_LOGI(kTag, "Mood caress: happiness=%u sadness=%u",
                static_cast<unsigned>(s_state.happiness.happiness),
                static_cast<unsigned>(CODY::PersonalityData::happiness.maximum - s_state.happiness.happiness));
            // Canonical admission and authored priorities decide on the next frame.
            // A normal symbol must not silently discard a recognized caress.
            s_state.affection_remaining_us = CODY::PettingConfig::affection_us;
            s_state.affection_owns_channel = true;
            ESP_LOGI(kTag, "Caress zone=%u accepted: transient Affection", static_cast<unsigned>(zone));
        }
    } else {
        const bool visible_sleep_handoff = personality_presentation(true) ==
            CODY::PersonalityPresentation::Sleep && !s_state.animation_paused;
        cancel_transient_affection(visible_sleep_handoff);
    }
    if (!point.pressed) {
        s_state.body_drag_active = false;
    }
    s_state.frame_dirty.store(true, std::memory_order_release);
    return ESP_OK;
}

// No parameters: only the existing worker may change the lifecycle and its clocks.
// Exception: Returns the real display-lock, standby-policy or expression failure.
esp_err_t apply_lifecycle_requests()
{
    const uint32_t request = s_lifecycle_request.load(std::memory_order_acquire);
    if (request == 0 || request == s_state.lifecycle_request) {
        return ESP_OK;
    }
    const esp_err_t error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (error != ESP_OK) {
        return error;
    }
    const DisplayUnlockOnExit lock;
    const esp_err_t policy_error = apply_scene_standby_policy({});
    if (policy_error != ESP_OK) {
        return policy_error;
    }
    const esp_err_t benchmark_error = stop_raster_benchmark(false, "power-lifecycle");
    if (benchmark_error != ESP_OK) {
        return benchmark_error;
    }
    const bool sleeping = (request & 1U) != 0;
    const esp_err_t audio_error = set_companion_typewriter_suspended(sleeping);
    if (audio_error != ESP_OK) {
        return audio_error;
    }
    s_state.lifecycle_request = request;
    CODY::cancel_companion_tap(s_state.companion_tap);
    CODY::suspend_companion_shake(s_state.companion_shake, esp_timer_get_time());
    // A retained contact must start a fresh visible hold after the wake handoff.
    s_state.setup_hold_candidate = false;
    s_state.setup_hold_eligible = false;
    cancel_transient_affection(sleeping);
    if (sleeping && !s_state.resume_scene_pending) {
        s_state.animation_paused_before_standby = s_state.animation_paused;
        s_state.rig_debug_before_standby = s_state.rig_debug;
        s_paused_scene_model.valid = s_state.animation_paused;
        if (s_paused_scene_model.valid) {
            // Wake has its own live artistic clocks. A paused scene retains its
            // exact expression, root displacement and contour until it resumes.
            s_paused_scene_model.face = s_state.face;
            s_paused_scene_model.response = s_state.response;
            s_paused_scene_model.response_frame = s_state.response_frame;
            s_paused_scene_model.contour = s_state.contour;
            s_paused_scene_model.definition = s_state.scene.valid ? s_state.scene.definition : nullptr;
        }
    }
    s_state.resume_scene_pending = true;
    s_state.lifecycle = {sleeping ? CODY::LifecyclePhase::Sleep : CODY::LifecyclePhase::BootSleeping, 0};
    if (!sleeping) {
        // The first retired dark/closed-eye frame acknowledges restored display
        // ownership. The panel owns the 800-ms wake fade; never multiply it by
        // another two-second character fade in the same worker.
        s_state.lifecycle.startup_presentation = CODY::StartupPresentation::PanelFade;
        const esp_err_t face_error = CODY::expression_channels(CODY::ExpressionId::BootSleeping, s_state.face);
        if (face_error != ESP_OK) {
            return face_error;
        }
    }
    s_state.rig_visible = true;
    s_state.rig_debug = false;
    s_state.animation_paused = false;
    s_state.animation_sampled_us = 0;
    s_state.next_frame_us = 0;
    s_state.frame_dirty.store(true, std::memory_order_release);
    s_demo_requested.store(false, std::memory_order_release);
    CODY::rebase_response_motion(s_state.response);
    // The existing face is the starting point for morphing, including closed
    // sleep eyes. Breathing/blink phases keep their owner across the transition.
    ESP_LOGI(kTag, "Atlas lifecycle: %s", sleeping ? "35A / Sleep" :
        "01 Sleeping (800ms panel fade) -> 02 Waking (1.5s) -> 03 Ready (2s); resume retained scene");
    return ESP_OK;
}

// sleeping: Power-owner intent; completion means an actual retired sleep/wake frame.
esp_err_t prepare_display_lifecycle(bool sleeping)
{
    if (s_start_state.load(std::memory_order_acquire) != StartState::Running) {
        const esp_err_t error = s_start_state.load(std::memory_order_acquire) == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Lifecycle request before a running renderer (%s)", esp_err_to_name(error));
        return error;
    }
    const uint32_t generation = s_lifecycle_generation.fetch_add(1, std::memory_order_acq_rel) + 1U;
    if (generation > UINT32_MAX / 2U) {
        ESP_LOGE(kTag, "Lifecycle generation exhausted (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t request = generation * 2U + (sleeping ? 1U : 0U);
    s_lifecycle_request.store(request, std::memory_order_release);
    const TickType_t started_at = xTaskGetTickCount();
    while (s_lifecycle_acknowledged.load(std::memory_order_acquire) != request) {
        if (s_start_state.load(std::memory_order_acquire) == StartState::Failed) {
            return s_terminal_error.load(std::memory_order_acquire);
        }
        if (xTaskGetTickCount() - started_at >= pdMS_TO_TICKS(kLifecyclePreparationTimeoutMs)) {
            ESP_LOGE(kTag, "Atlas %s frame preparation timed out (%s)",
                sleeping ? "35A / Sleep" : "35B / Wake", esp_err_to_name(ESP_ERR_TIMEOUT));
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

// point: Original acquired contact with its hardware-owner timestamp.
// pressed_edge: Actual beginning of the contact; wake eligibility uses fresh reports.
// eligible: True only while the visible character owns a resting, unobstructed route.
// consumed: True after the menu opens and until this same contact actually releases.
// Exception: Returns the original body hit-test, menu, policy or release-publication error.
esp_err_t handle_setup_hold(const LUNASHELL_touch_point_t &point, bool pressed_edge,
                            bool eligible, bool &consumed)
{
    consumed = false;
    if (s_state.setup_hold_opened) {
        consumed = true;
        CODY::cancel_petting(s_state.petting);
        if (!point.pressed) {
            s_state.setup_hold_opened = false;
            s_state.setup_hold_candidate = false;
            if (s_state.body_drag_active) {
                return publish_body_gesture(point, false); // Publish only the real release.
            }
        }
        return ESP_OK;
    }
    const bool menu_eligible = eligible && CODY::can_open_setup_menu();
    const bool start_visible_contact = point.pressed && menu_eligible &&
        (pressed_edge || !s_state.setup_hold_eligible);
    s_state.setup_hold_eligible = menu_eligible && point.pressed;
    if (!menu_eligible || !point.pressed) {
        s_state.setup_hold_candidate = false;
        return ESP_OK;
    }
    if (start_visible_contact) {
        // A wake contact may begin while the retained scene is hidden. Start
        // its 1500-ms clock only at the first acquired, visible eligible sample.
        bool hit = false;
        const esp_err_t error = CODY::hit_test_body(*s_resources.rig,
            {static_cast<float>(point.x), static_cast<float>(point.y)}, hit);
        if (error != ESP_OK) {
            return error;
        }
        s_state.setup_hold_candidate = hit;
        s_state.setup_hold_origin = point;
        s_state.setup_hold_last_sample_us = point.sample_timestamp_us;
        s_state.setup_hold_rotation_degrees = s_state.rendered_rotation_degrees;
    }
    if (!s_state.setup_hold_candidate) {
        return ESP_OK;
    }
    const int dx = static_cast<int>(point.x) - s_state.setup_hold_origin.x;
    const int dy = static_cast<int>(point.y) - s_state.setup_hold_origin.y;
    const int64_t gap_us = point.sample_timestamp_us - s_state.setup_hold_last_sample_us;
    if (dx * dx + dy * dy > 12 * 12 || gap_us < 0 || gap_us > 250000 ||
        s_state.setup_hold_rotation_degrees != s_state.rendered_rotation_degrees) {
        // Moving caresses/drags retain their original gesture. A discontinuity,
        // standby or orientation change cannot accumulate a hidden long press.
        s_state.setup_hold_candidate = false;
        return ESP_OK;
    }
    s_state.setup_hold_last_sample_us = point.sample_timestamp_us;
    if (point.sample_timestamp_us - s_state.setup_hold_origin.sample_timestamp_us < 1500000) {
        return ESP_OK;
    }
    esp_err_t error = CODY::open_setup_menu(s_state.scene);
    if (error == ESP_OK) {
        error = apply_scene_standby_policy(s_state.scene);
    }
    if (error != ESP_OK) {
        return error;
    }
    s_state.setup_hold_opened = true;
    s_state.setup_hold_candidate = false;
    s_state.routine_scene = false;
    s_state.routine_greeting_pending = false;
    cancel_transient_affection();
    s_state.frame_dirty.store(true, std::memory_order_release);
    consumed = true;
    ESP_LOGI(kTag, "Setup menu opened by stationary character hold (1500 ms)");
    return ESP_OK;
}

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// point: Acquired logical touch in the same orientation as the completed diagnostic view.
// pressed_edge: True for the first acquired press of this contact.
void handle_debug_view_touch(const LUNASHELL_touch_point_t &point, bool pressed_edge)
{
    s_state.touch_pressed = point.pressed;
    if (pressed_edge && CODY::debug_view_back_contains(
            {static_cast<int16_t>(point.x), static_cast<int16_t>(point.y)})) {
        apply_debug_view(LUNASHELL::CodexDebugScreen::Normal, 0);
        return;
    }
    if (!point.pressed || s_state.debug_scroll_rotation_degrees != s_state.rendered_rotation_degrees) {
        s_state.debug_scroll_contact = false;
    }
    if (pressed_edge && debug_channel_view_active() && point.x >= 73 && point.x < 393 &&
        point.y >= 90 && point.y < 375) {
        s_state.debug_scroll_contact = true;
        s_state.debug_scroll_origin_y = static_cast<int16_t>(point.y);
        s_state.debug_scroll_origin_row = s_state.debug_channel_scroll;
        s_state.debug_scroll_rotation_degrees = s_state.rendered_rotation_degrees;
    }
    if (point.pressed && s_state.debug_scroll_contact) {
        const int offset = (s_state.debug_scroll_origin_y - static_cast<int>(point.y)) / 34;
        // Only gesture travel is bounded; authored face values remain exact and unmodified.
        const int row = std::clamp<int>(s_state.debug_scroll_origin_row + offset, 0, CODY::kFaceChannelCount - 1);
        if (row != s_state.debug_channel_scroll) {
            s_state.debug_channel_scroll = static_cast<uint8_t>(row);
            s_resources.previous_frame_valid = false;
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
    }
}
#endif

// point: Successfully acquired valid hardware sample, handled under the display lock.
// Exception: Returns the real route, gesture, snapshot or rendering-state error.
esp_err_t handle_touch_sample(const LUNASHELL_touch_point_t &point)
{
    if (s_touch_contact_interrupted) {
        // A damaged report cannot imply release. Require an actual acquired
        // release before admitting a fresh press/hold/swipe on any route.
        if (!point.pressed) {
            s_touch_contact_interrupted = false;
            s_state.touch_pressed = false;
            s_state.setup_hold_opened = false;
        }
        return ESP_OK;
    }
    const bool pressed_edge = point.pressed && !s_state.touch_pressed;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (s_voice_view_visible) {
        s_state.touch_pressed = point.pressed;
        CODY::cancel_companion_tap(s_state.companion_tap);
        return ESP_OK;
    }
#endif
    const bool panel_open = technical_panel_view_active();
    const bool companion_eligible = LUNASHELL_display_runtime_writes_allowed() &&
        (companion_home_route_active() || panel_open);
    const bool passive_tap = CODY::observe_companion_tap(s_state.companion_tap,
        static_cast<int16_t>(point.x), static_cast<int16_t>(point.y), point.pressed,
        pressed_edge, point.sample_timestamp_us, s_state.rendered_rotation_degrees,
        companion_eligible, (!pressed_edge && s_state.petting.triggered) || s_state.setup_hold_opened);
    if (panel_open) {
        s_state.touch_pressed = point.pressed;
        if (passive_tap) {
            return advance_local_companion_panel();
        }
        // A contact on a technical panel never reaches a hidden home control.
        return ESP_OK;
    }
    if (pressed_edge && s_typewriter_audio_prepared && s_state.boot_phase == BootPhase::Ready &&
        LUNASHELL_display_runtime_writes_allowed()) {
        // A held finger and its motion samples produce no additional clicks.
        const esp_err_t feedback_error = LUNASHELL_audio_notify_touch();
        if (feedback_error != ESP_OK) {
            log_optional_typewriter_result(feedback_error);
        }
    }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    if (debug_view_active()) {
        handle_debug_view_touch(point, pressed_edge);
        return ESP_OK;
    }
#endif
    if (s_state.raster_benchmark_active && point.pressed) {
        s_state.touch_pressed = point.pressed;
        return stop_raster_benchmark(false, "touch");
    }
    if (!s_state.resume_scene_pending && s_state.boot_phase == BootPhase::Ready &&
        LUNASHELL_display_runtime_writes_allowed() && CODY::lifecycle_is_resting(s_state.lifecycle)) {
        const esp_err_t activity_error = CODY::observe_onboarding_touch_activity(
            {static_cast<int16_t>(point.x), static_cast<int16_t>(point.y)},
            point.pressed, pressed_edge, s_state.onboarding_elapsed_us);
        if (activity_error != ESP_OK) {
            return activity_error;
        }
    }
    bool reveal_only = false;
    const bool chrome_eligible = s_state.rig_visible &&
        CODY::lifecycle_is_resting(s_state.lifecycle) &&
        (kRenderDiagnosticsEnabled || !s_state.onboarding.effective_first_boot);
    if constexpr (kRenderDiagnosticsEnabled) {
        const uint8_t previous_opacity = s_state.navigation_visibility.opacity(point.sample_timestamp_us);
        reveal_only = chrome_eligible && pressed_edge && previous_opacity != 255 &&
            CODY::rig_control_at({static_cast<float>(point.x), static_cast<float>(point.y)}) != CODY::RigControl::None;
        if (pressed_edge) {
            s_state.navigation_touch_active = chrome_eligible &&
                CODY::rig_control_at({static_cast<float>(point.x), static_cast<float>(point.y)}) != CODY::RigControl::None;
        }
    }
    if (chrome_eligible && (point.pressed || s_state.touch_pressed)) {
        s_state.navigation_visibility.interact(point.sample_timestamp_us);
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    s_state.touch_pressed = point.pressed;
    const bool debug_control_contact = s_state.navigation_touch_active;
    if (!point.pressed) {
        s_state.navigation_touch_active = false;
    }
    if (debug_control_contact && !pressed_edge) {
        CODY::cancel_companion_tap(s_state.companion_tap);
        CODY::cancel_petting(s_state.petting);
        return ESP_OK; // A Debug-control contact never becomes an onboarding swipe.
    }
    if (reveal_only) {
        CODY::cancel_companion_tap(s_state.companion_tap);
        s_state.body_drag_active = false;
        CODY::cancel_petting(s_state.petting);
        return ESP_OK;
    }
    bool setup_consumed = false;
    const bool setup_eligible = !debug_control_contact && !s_state.resume_scene_pending &&
        s_state.rig_visible && s_resources.previous_frame_valid && s_cache_was_rig &&
        s_state.boot_phase == BootPhase::Ready && LUNASHELL_display_runtime_writes_allowed() &&
        CODY::lifecycle_is_resting(s_state.lifecycle) &&
        (!s_state.scene.valid || s_state.scene.intro_phase == CODY::SceneIntroPhase::Complete) &&
        classify_rig_touch(point) == TouchAction::None;
    const esp_err_t setup_error = handle_setup_hold(point, pressed_edge, setup_eligible, setup_consumed);
    if (setup_error != ESP_OK || setup_consumed) {
        CODY::cancel_companion_tap(s_state.companion_tap);
        return setup_error;
    }
    if (!debug_control_contact && !s_state.resume_scene_pending && s_state.boot_phase == BootPhase::Ready && LUNASHELL_display_runtime_writes_allowed() &&
        CODY::lifecycle_is_resting(s_state.lifecycle)) {
        bool consumed = false;
        const esp_err_t route_error = CODY::handle_onboarding_touch(
            s_state.scene, {static_cast<int16_t>(point.x), static_cast<int16_t>(point.y)},
            point.pressed, pressed_edge,
            s_state.rendered_rotation_degrees, consumed);
        if (route_error != ESP_OK) {
            return route_error;
        }
        const esp_err_t policy_error = apply_scene_standby_policy(s_state.scene);
        if (policy_error != ESP_OK) {
            return policy_error;
        }
        if (consumed) {
            CODY::cancel_companion_tap(s_state.companion_tap);
            cancel_transient_affection();
            if (!point.pressed && s_state.body_drag_active) {
                const esp_err_t release_error = publish_body_gesture(point, false);
                if (release_error != ESP_OK) {
                    return release_error;
                }
            }
            s_state.body_drag_active = false;
            s_state.frame_dirty.store(true, std::memory_order_release);
            return ESP_OK;
        }
    }
    if (pressed_edge && s_state.boot_phase == BootPhase::Ready && LUNASHELL_display_runtime_writes_allowed()) {
        if (!s_state.rig_visible) {
            if (!s_greeting_scheduled.load(std::memory_order_acquire) &&
                point.x >= 73 && point.x < 393 && point.y >= 73 && point.y < 393) {
                const esp_err_t demo_error = enter_animation_demo();
                if (demo_error != ESP_OK) {
                    return demo_error;
                }
            }
        } else {
            const TouchAction action = classify_rig_touch(point);
            if (action != TouchAction::None) {
                CODY::cancel_companion_tap(s_state.companion_tap);
            }
            if (action == TouchAction::ReturnToIdle) {
                const esp_err_t policy_error = apply_scene_standby_policy({});
                if (policy_error != ESP_OK) {
                    return policy_error;
                }
                s_state.rig_visible = true;
                s_state.scene = {};
                s_state.resume_scene_pending = false;
                s_state.lifecycle = {CODY::LifecyclePhase::Idle, 0};
                s_state.routine_scene = false;
                s_state.routine_greeting_pending = false;
                s_state.requested_expression = CODY::ExpressionId::Calm;
                s_state.expression_override = false;
                s_state.selected_point = CODY::kNoPoint;
                ESP_LOGI(kTag, "Rig Back -> 16 / Idle (Calm)");
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
            const bool interactive_scene = !s_state.scene.valid ||
                s_state.scene.intro_phase == CODY::SceneIntroPhase::Complete;
            if (interactive_scene && !s_state.animation_paused && s_state.rig_frame_count != 0 &&
                action != TouchAction::ReturnToIdle && action != TouchAction::ToggleOverlay &&
                action != TouchAction::TogglePause) {
                CODY::EyeId eye = CODY::EyeId::None;
                const esp_err_t hit_error = CODY::hit_test_eye(*s_resources.rig,
                    {static_cast<float>(point.x), static_cast<float>(point.y)},
                    CODY::Data::response.touch.eye_hit_radius, eye);
                if (hit_error != ESP_OK) {
                    return hit_error;
                }
                if (eye != CODY::EyeId::None) {
                    CODY::cancel_companion_tap(s_state.companion_tap);
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
    const esp_err_t body_error = publish_body_gesture(point, pressed_edge);
    if (body_error == ESP_OK && passive_tap && !s_state.petting.triggered) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        bool on_character = false;
        const auto &tap = s_state.companion_tap;
        const esp_err_t hit_error = CODY::hit_test_body(*s_resources.rig,
            {static_cast<float>(tap.origin_x), static_cast<float>(tap.origin_y)}, on_character);
        if (hit_error != ESP_OK) return hit_error;
        if (on_character) {
            const int dx = tap.origin_x - s_voice_tap_x;
            const int dy = tap.origin_y - s_voice_tap_y;
            if (s_voice_tap_due_us != 0 && point.sample_timestamp_us <= s_voice_tap_due_us &&
                s_voice_tap_rotation == s_state.rendered_rotation_degrees && dx * dx + dy * dy <= 1600) {
                s_voice_tap_due_us = 0;
                return LUNASHELL::requestVoiceInput(LUNASHELL::VoiceInputTrigger::DoubleTap);
            }
            s_voice_tap_due_us = point.sample_timestamp_us + 420000;
            s_voice_tap_rotation = s_state.rendered_rotation_degrees;
            s_voice_tap_x = tap.origin_x;
            s_voice_tap_y = tap.origin_y;
            s_state.frame_dirty.store(true, std::memory_order_release);
            return ESP_OK;
        }
#endif
        return advance_local_companion_panel();
    }
    return body_error;
}

// point: Receives a real report, retaining its original timestamp and error.
// Exception: Returns lock/acquisition failure distinctly from an absent report.
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
                sample_error != ESP_OK ? sample_error : ESP_ERR_INVALID_RESPONSE, true};
    }
    return {TouchPollStatus::Reported, ESP_OK};
}

// reports: Frame-local reports to dispatch after complete frame retirement.
// Exception: Returns the original acquisition or capacity error as a failed result.
TouchPollResult capture_pending_touch(PendingTouchReports &reports)
{
    LUNASHELL_touch_point_t point{};
    const TouchPollResult sample = read_touch_report(point);
    if (sample.status == TouchPollStatus::NoReport) {
        return sample; // No read took place; no measurement was published.
    }
    if (sample.status == TouchPollStatus::Failed) {
        return sample;
    }
    if (reports.count >= sizeof(reports.samples) / sizeof(reports.samples[0])) {
        ESP_LOGE(kTag, "Frame touch report capacity exhausted (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return {TouchPollStatus::Failed, ESP_ERR_INVALID_SIZE};
    }
    reports.samples[reports.count++] = point;
    return sample;
}

// sample: Structured result whose provenance distinguishes acquisition from routing.
bool optional_runtime_touch_failure(const TouchPollResult &sample)
{
    // Only an acquired INVALID_RESPONSE is optional after startup. This code
    // can also mean an I2C NACK; do not infer its electrical/protocol origin.
    // Other acquisition errors, locks, routing and startup retain terminal policy.
    return sample.status == TouchPollStatus::Failed && sample.acquisition_failed &&
        sample.error == ESP_ERR_INVALID_RESPONSE &&
        s_start_state.load(std::memory_order_acquire) == StartState::Running;
}

// error: Real rejected report error, preserved by ui.touch and character_touch.
void interrupt_touch_gestures(esp_err_t error)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    s_voice_tap_due_us = 0;
#endif
    CODY::cancel_companion_tap(s_state.companion_tap);
    s_touch_contact_interrupted = true;
    s_state.setup_hold_candidate = false;
    s_state.setup_hold_eligible = false;
    s_state.navigation_touch_active = false;
    s_state.body_drag_active = false;
    CODY::cancel_petting(s_state.petting);
    CODY::cancel_onboarding_touch();
    ESP_LOGW(kTag, "Optional runtime touch report rejected; gestures interrupted, renderer continues (%s)",
        esp_err_to_name(error));
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

// No parameters: poll the sole hardware owner once and dispatch only valid reports.
// Exception: Returns acquisition and route failures with distinct provenance.
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

// minimum_free_bytes: Smallest measured headroom of this PSRAM-backed worker.
// sampled_at_us: Last diagnostic sampling time; scanning is bounded to 0.5 Hz.
void report_worker_stack_headroom(uint32_t &minimum_free_bytes, int64_t &sampled_at_us)
{
    if constexpr (!kRenderDiagnosticsEnabled) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (sampled_at_us != 0 && now_us - sampled_at_us < kWorkerStackSamplePeriodUs) {
        return;
    }
    sampled_at_us = now_us;
    // The supported ESP-IDF Xtensa port returns bytes, not generic FreeRTOS words.
    // This runs only in the worker after a complete draw, never on startup's stack.
    const uint32_t free_bytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    if (free_bytes < minimum_free_bytes) {
        minimum_free_bytes = free_bytes;
        ESP_LOGI(kTag, "STACK worker=CODY_companion budget=%luB min_free=%luB peak_used=%luB",
            static_cast<unsigned long>(kWorkerStackBytes),
            static_cast<unsigned long>(minimum_free_bytes),
            static_cast<unsigned long>(kWorkerStackBytes - minimum_free_bytes));
    }
}

// context: Unused; this single worker owns the boot/debug route and animation clock.
void companion_worker(void *context)
{
    (void)context;
    bool parked = false;
    uint32_t minimum_stack_free_bytes = kWorkerStackBytes;
    int64_t stack_sampled_at_us = 0;
    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        FrameTimings::WorkerLoopTrace worker_trace{};
        worker_trace.valid = true;
        worker_trace.loop_started_us = now_us;
        // The same worker advances boot, expression and body animation clocks.
        const int64_t remaining_us = s_state.next_frame_us - now_us;
        uint32_t wait_ms = kSamplePeriodMs;
        if (parked) {
            wait_ms = kStandbySamplePeriodMs;
        } else if (s_state.animation_paused && !kRenderDiagnosticsEnabled && !companion_home_route_active()) {
            wait_ms = kSamplePeriodMs;
        } else if (s_state.rig_visible) {
            wait_ms = remaining_us > 0 ?
                static_cast<uint32_t>((remaining_us + 999) / 1000) : 1;
        }
        if (s_capture_sequence_active.load(std::memory_order_acquire)) {
            wait_ms = std::min<uint32_t>(wait_ms, 100U);
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
            if (optional_runtime_touch_failure(touch)) {
                interrupt_touch_gestures(touch.error);
            } else {
                fail_consumer("Touch sample", touch.error, true);
                break;
            }
        }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
        // Poll even when animation or display frames are parked; mailbox expiry is independent.
        bool optional_debug_rejection = false;
        const esp_err_t debug_error = apply_debug_screen_requests(optional_debug_rejection);
        if (debug_error != ESP_OK && !optional_debug_rejection) {
            fail_consumer("Debug screen request", debug_error, false);
            break;
        }
#endif
        const esp_err_t scene_error = apply_scene_requests();
        if (scene_error != ESP_OK) {
            fail_consumer("Scene request", scene_error, false);
            break;
        }
        const esp_err_t lifecycle_error = apply_lifecycle_requests();
        if (lifecycle_error != ESP_OK) {
            fail_consumer("Atlas lifecycle request", lifecycle_error, false);
            break;
        }
        const esp_err_t capture_timeline_error = advance_capture_timeline();
        if (capture_timeline_error != ESP_OK) {
            fail_consumer("PWR capture timeline", capture_timeline_error, false);
            break;
        }
        // Gate closure and its healthy parked classification belong to one
        // locked observation; a fast failed Pair must not terminate the worker.
        const esp_err_t admission_lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
        if (admission_lock_error != ESP_OK) {
            fail_consumer("Display admission lock", admission_lock_error, false);
            break;
        }
        const bool display_available = LUNASHELL_display_runtime_writes_allowed();
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        // Recognition can preempt a static non-character route as well as Home.
        const esp_err_t voice_error = LUNASHELL::DeviceSnapshotService::instance().readVoiceObservation(s_voice_input_view);
        if (voice_error != ESP_OK) {
            bsp_display_unlock();
            fail_consumer("Voice input observation", voice_error, false);
            break;
        }
        const auto input_phase = s_voice_input_view.input.phase;
        const bool recent_terminal = (input_phase == LUNASHELL::VoiceInputPhase::Delivered ||
            input_phase == LUNASHELL::VoiceInputPhase::Cancelled || input_phase == LUNASHELL::VoiceInputPhase::Unconfirmed ||
            input_phase == LUNASHELL::VoiceInputPhase::Failed) && s_voice_input_view.input.changed_at_us > 0 &&
            esp_timer_get_time() - s_voice_input_view.input.changed_at_us < 3000000;
        const bool voice_attention = input_phase == LUNASHELL::VoiceInputPhase::Listening ||
            input_phase == LUNASHELL::VoiceInputPhase::Sending || input_phase == LUNASHELL::VoiceInputPhase::Cancelling ||
            input_phase == LUNASHELL::VoiceInputPhase::Received || s_voice_view_visible || recent_terminal;
        if ((voice_attention && (input_phase != s_voice_view_phase ||
            s_voice_view_started_at_us != s_voice_input_view.input.started_at_us ||
            esp_timer_get_time() >= s_state.next_frame_us)) ||
            (s_voice_tap_due_us != 0 && esp_timer_get_time() >= s_voice_tap_due_us)) {
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
#endif
        const bool display_parked = !display_available && renderer_is_parked();
        // The same admission snapshot owns the independent mood clock. A brief
        // radio DMA drain must rebase it without replaying a parked caress later.
        const esp_err_t happiness_error = advance_happiness_clock_locked(esp_timer_get_time(),
            display_available && s_state.lifecycle.phase != CODY::LifecyclePhase::Sleep);
        if (happiness_error != ESP_OK) {
            bsp_display_unlock();
            fail_consumer("Happiness owner update", happiness_error, false);
            break;
        }
        if (display_parked) {
            suspend_optional_personality_audio(true);
            cancel_transient_affection();
        }
        const esp_err_t audio_error = display_parked
            ? set_companion_typewriter_suspended(true) : ESP_OK;
        bsp_display_unlock();
        if (!display_available) {
            if (!display_parked) {
                fail_consumer("Display readiness", ESP_ERR_INVALID_STATE, false);
                break;
            }
            parked = true;
            if (audio_error != ESP_OK) {
                fail_consumer("Parked typewriter audio", audio_error, false);
                break;
            }
            continue;
        }
        // A pending boot sound can observe trusted time even on a static/paused route.
        // The cue owner bounds this canonical-only check; terminal requests do no work.
        CODY_boot_cues_observe_pending();
        const uint32_t revision = LUNASHELL_display_runtime_content_revision();
        const bool paused_release_frame = s_state.animation_paused && !kRenderDiagnosticsEnabled &&
            !companion_home_route_active();
        if (!parked && s_state.rig_visible &&
            (paused_release_frame || esp_timer_get_time() < s_state.next_frame_us) &&
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
        const bool clearing_countdown = s_state.capture_clear_pending;
        const bool capturing_frame = CODY::screenshot_state() == CODY::ScreenshotState::Capturing;
        const DrawResult draw = draw_companion_frame(parked, nullptr, &worker_trace);
        if (draw.parked) {
            ESP_LOGW(kTag, "Redraw parked during power transition: %s",
                     esp_err_to_name(draw.error));
            parked = true;
            const esp_err_t happiness_rebase_error = update_worker_happiness(true);
            if (happiness_rebase_error != ESP_OK) {
                fail_consumer("Parked happiness clock", happiness_rebase_error, false);
                break;
            }
            continue;
        }
        if (draw.error != ESP_OK) {
            fail_consumer("Companion frame redraw", draw.error, false);
            break;
        }
        report_worker_stack_headroom(minimum_stack_free_bytes, stack_sampled_at_us);
        s_rendered_revision.store(draw.completed_revision, std::memory_order_release);
        if (s_state.capture_digit != 0U &&
            s_state.capture_digit_shown_at_us == 0 &&
            s_cached_capture_visual == capture_visual()) {
            s_state.capture_digit_shown_at_us = esp_timer_get_time();
        }
        if (clearing_countdown) {
            // The first no-overlay frame has now retired; its successor may be captured.
            s_state.capture_clear_pending = false;
            const esp_err_t request_error = CODY::request_screenshot();
            if (request_error != ESP_OK) {
                s_state.capture_request_error = request_error;
                s_state.capture_feedback_visible = true;
                ESP_LOGE(kTag, "PWR screenshot admission failed (%s)",
                         esp_err_to_name(request_error));
            }
            s_state.frame_dirty.store(true, std::memory_order_release);
        }
        if (capturing_frame) {
            s_state.capture_feedback_visible = true;
            s_state.frame_dirty.store(true, std::memory_order_release);
            const auto screenshot = CODY::screenshot_state();
            if (screenshot == CODY::ScreenshotState::Saving ||
                screenshot == CODY::ScreenshotState::Saved) {
                const esp_err_t sound_error = CODY::notify_capture_sound();
                if (sound_error != ESP_OK) {
                    ESP_LOGE(kTag, "Captured frame shutter cue unavailable (%s)",
                             esp_err_to_name(sound_error));
                }
            }
        }
        if (s_state.capture_feedback_visible &&
            (s_cached_capture_visual == CaptureVisual::Saved ||
             s_cached_capture_visual == CaptureVisual::Failed) &&
            s_state.capture_feedback_shown_at_us == 0) {
            s_state.capture_feedback_shown_at_us = esp_timer_get_time();
            s_capture_sequence_active.store(false, std::memory_order_release);
        }
        // An animation request is complete only after the final panel transfer,
        // never after geometry evaluation or a partial strip update.
        if (s_state.lifecycle_request != 0 &&
            ((s_state.lifecycle_request & 1U) == 0 || CODY::lifecycle_sleep_ready(s_state.lifecycle))) {
            s_lifecycle_acknowledged.store(s_state.lifecycle_request, std::memory_order_release);
        }
        parked = false;
    }
    const esp_err_t cleanup_error = release_worker_resources();
    if (cleanup_error != ESP_OK) {
        fail_consumer("Strip cleanup", cleanup_error, false);
    }
    vTaskDeleteWithCaps(nullptr);
}

}  // namespace

// cue: Single narrative symbol; real device facts use the separate snapshot-gated selector.
// Exception: Returns invalid cue/category or the real state/lock error.
esp_err_t CODY::set_personality_cue(PersonalityCue cue)
{
    if (!personality_cue_valid(cue) || personality_cue_is_device(cue)) {
        ESP_LOGE(kTag, "Narrative cue=%u rejected (%s)", static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return set_personality_request(cue);
}

// cue: None or a device-state symbol whose visibility follows the canonical snapshot.
// Exception: Returns invalid cue/category or the real state/lock error.
esp_err_t CODY::set_personality_device_cue(PersonalityCue cue)
{
    if (!personality_cue_valid(cue) || (cue != PersonalityCue::None && !personality_cue_is_device(cue))) {
        ESP_LOGE(kTag, "Device cue=%u rejected (%s)", static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return set_personality_request(cue);
}

// cue: Receives the current request on success only, independent of displayed-frame retirement.
// Exception: Returns the real display lock or consumer-state failure.
esp_err_t CODY::get_personality_cue(PersonalityCue &cue)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Personality read lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Personality read state rejected (%s)", esp_err_to_name(error));
        return error;
    }
    cue = s_state.requested_personality;
    return ESP_OK;
}

// frame: Receives only the last fully retired presentation, never a prepared or partial frame.
// Exception: Returns the real display-lock, consumer-state or invalidated-frame error.
esp_err_t CODY::get_personality_frame(PersonalityFrame &frame)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Personality frame read lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running || !s_resources.previous_frame_valid) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Personality retired frame unavailable (%s)", esp_err_to_name(error));
        return error;
    }
    frame = s_state.retired_personality;
    return ESP_OK;
}

// snapshot: Receives session happiness, derived sadness and retained decay phase under one lock.
// Exception: Returns the actual display-lock or consumer-state error without replacing output.
esp_err_t CODY::get_happiness(HappinessSnapshot &snapshot)
{
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Happiness read lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Happiness read state rejected (%s)", esp_err_to_name(error));
        return error;
    }
    snapshot.happiness = s_state.happiness.happiness;
    snapshot.sadness = CODY::PersonalityData::happiness.maximum - snapshot.happiness;
    snapshot.decay_remainder_us = s_state.happiness.decay_remainder_us;
    return ESP_OK;
}

// No parameters: start the single companion owner on the dedicated render core.
// Exception: Returns the original startup failure and publishes the failed stage.
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
    const esp_err_t happiness_error = CODY::initialize_happiness(
        CODY::PersonalityData::happiness, s_state.happiness);
    if (happiness_error != ESP_OK) {
        return fail_consumer("Happiness configuration", happiness_error, false);
    }
    s_state.happiness_sampled_at_us = 0;
    const esp_err_t branding_error = VIBEWARE_boot_validate();
    if (branding_error != ESP_OK) {
        return fail_consumer("VibeWare boot resource", branding_error, false);
    }
    const esp_err_t branding_layout_error = CODY::BootBranding::prepare(
        CODY::SceneData::title_font, CODY::SceneData::body_font, s_state.branding_presentation);
    if (branding_layout_error != ESP_OK) {
        return fail_consumer("Cody boot branding layout", branding_layout_error, false);
    }
    const esp_err_t asset_error = validate_boot_asset(
        CODY_asset::sleeping_pearl, "Sleeping boot asset"
    );
    if (asset_error != ESP_OK) {
        return fail_consumer("Boot asset validation", asset_error, false);
    }
    const esp_err_t waking_asset_error = validate_boot_asset(
        CODY_asset::waking_pearl, "Waking boot asset"
    );
    if (waking_asset_error != ESP_OK) {
        return fail_consumer("Waking boot asset validation", waking_asset_error, false);
    }
    const esp_err_t ready_asset_error = validate_boot_asset(
        CODY_asset::ready_pearl, "Ready boot asset"
    );
    if (ready_asset_error != ESP_OK) {
        return fail_consumer("Ready boot asset validation", ready_asset_error, false);
    }
    const esp_err_t response_config_error = CODY::validate_response_config(CODY::Data::response);
    if (response_config_error != ESP_OK) {
        return fail_consumer("Response configuration", response_config_error, false);
    }
    const esp_err_t settings_error = CODY::load_onboarding_settings(s_state.onboarding);
    if (settings_error != ESP_OK) {
        return fail_consumer("Onboarding settings", settings_error, false);
    }
    const esp_err_t preferences_error = CODY::load_companion_preferences();
    if (preferences_error != ESP_OK) {
        return fail_consumer("Companion preferences", preferences_error, false);
    }
#if CODY_ENABLE_PEARL_JELLY
    const esp_err_t look_error = CODY::load_persisted_jelly_skin();
    if (look_error != ESP_OK) {
        return fail_consumer("Cody look setting", look_error, false);
    }
    ESP_LOGI(kTag, "Jelly raster kernel: %s", CODY::jelly_raster_kernel_name(
        CODY::selected_jelly_raster_kernel()));
#endif
    for (const auto &definition : CODY::SceneData::definitions) {
        CODY::SceneFrame preflight_scene{};
        const esp_err_t scene_error = CODY::begin_scene(definition, preflight_scene);
        if (scene_error != ESP_OK) {
            return fail_consumer("Scene configuration", scene_error, false);
        }
    }
    const esp_err_t caption_font_error = CODY2D::validate_mono_font(CODY::SceneData::body_font);
    if (caption_font_error != ESP_OK) {
        return fail_consumer("Caption font configuration", caption_font_error, false);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    const esp_err_t typewriter_audio_error = CODY::prepare_typewriter_audio();
    if (typewriter_audio_error != ESP_OK) {
        return fail_consumer("Typewriter audio preparation", typewriter_audio_error, false);
    }
    s_typewriter_audio_prepared = true;
    const esp_err_t personality_audio_error = CODY::prepare_personality_audio();
    if (personality_audio_error == ESP_OK) {
        s_personality_audio_prepared = true;
        suspend_optional_personality_audio(true);
    } else {
        // Decorative SFX are optional. Their owner retains/publishes the original failure.
        log_optional_personality_error(personality_audio_error);
    }
#endif
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    const esp_err_t capture_sound_error = CODY::prepare_capture_sound();
    if (capture_sound_error != ESP_OK) {
        return fail_consumer("Shutter audio preparation", capture_sound_error, false);
    }
#endif
    const esp_err_t action_worker_error = CODY::onboarding_service_init();
    if (action_worker_error != ESP_OK) {
        return fail_consumer("Onboarding action worker", action_worker_error, false);
    }
    if (!s_state.onboarding.force_first_boot || esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        CODY::request_saved_setup_startup();
        s_state.routine_greeting_pending = false;
    }
    s_state.response = CODY::make_response_state(CODY::Data::response);
    s_state.response.idle_random_state = esp_random() | 1U;
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
    if constexpr (kRasterBenchmarkEnabled) {
        void *benchmark_storage = heap_caps_malloc(
            sizeof(CODY::RasterBenchmarkRun), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (benchmark_storage == nullptr) {
            release_companion_resources();
            return fail_consumer("Raster benchmark allocation", ESP_ERR_NO_MEM, false);
        }
        s_resources.raster_benchmark = new (benchmark_storage) CODY::RasterBenchmarkRun{};
    }
    s_state.rig_visible = true;
    s_state.deep_sleep_wake = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    // Deep-sleep Wake keeps its dark-panel lifecycle; branding belongs to cold boot/reset only.
    s_state.branding_complete = s_state.deep_sleep_wake;
    s_state.lifecycle = s_state.deep_sleep_wake
        ? CODY::LifecycleState{CODY::LifecyclePhase::Wake, 0} : CODY::LifecycleState{};
    s_state.contour = CODY::make_contour_state();
    const esp_err_t initial_expression_error = CODY::expression_channels(
        s_state.deep_sleep_wake ? CODY::ExpressionId::Drowsy : CODY::ExpressionId::BootSleeping, s_state.face);
    if (initial_expression_error != ESP_OK) {
        release_companion_resources();
        return fail_consumer("Initial lifecycle expression", initial_expression_error, false);
    }
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
    const esp_err_t lifecycle_error = LUNASHELL_power_manager_register_display_lifecycle(prepare_display_lifecycle);
    if (lifecycle_error != ESP_OK) {
        release_companion_resources();
        return fail_consumer("Display lifecycle owner registration", lifecycle_error, false);
    }
    const esp_err_t power_capture_error =
        LUNASHELL_power_manager_register_short_press(handle_active_power_short_press);
    if (power_capture_error != ESP_OK) {
        release_companion_resources();
        return fail_consumer("PWR capture owner registration", power_capture_error, false);
    }
    s_start_state.store(StartState::Running, std::memory_order_release);
    TaskHandle_t worker = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(
            companion_worker, "CODY_companion", kWorkerStackBytes,
            nullptr, 2, &worker, kRenderCoreId,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        release_companion_resources();
        return fail_consumer("Worker creation", ESP_ERR_NO_MEM, false);
    }
    ESP_LOGI(kTag, "Animated %s and real touch ready; frame-owned internal raster on core %ld, active=%lu ms standby=%lu ms",
             s_state.deep_sleep_wake ? "35B / Wake -> 16 / Idle" : "VibeWare floppy (3.5s cycles) -> 01 Sleeping (2s fade) -> 02 Waking (1.5s) -> 03 Ready (2s)",
             static_cast<long>(kRenderCoreId),
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
// Exception: Returns the real display-lock or state error.
extern "C" esp_err_t CODY_companion_demo_start(void)
{
    if (kRasterBenchmarkEnabled || !CODY::RuntimeData::autostart) {
        // The worker enters the benchmark once Idle is actually reached, even with a visible boot rig.
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
    if (s_state.onboarding.effective_first_boot) {
        // Ordinary demo requests retain the first-run routing priority.
        return ESP_OK;
    }
    s_demo_requested.store(true, std::memory_order_release);
    return ESP_OK;
}

// enabled: Session preference for the existing measured layer; the worker owns rendering.
extern "C" esp_err_t CODY_companion_set_debug_readout_enabled(bool enabled)
{
    if constexpr (!kRenderDiagnosticsEnabled) {
        if (enabled) {
            ESP_LOGE(kTag, "Readout enable rejected: diagnostics excluded in Release (%s)",
                esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Readout preference lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Readout preference state rejected (%s)", esp_err_to_name(error));
        return error;
    }
    if (s_state.debug_readout_enabled != enabled) {
        s_state.debug_readout_enabled = enabled;
        s_state.frame_dirty.store(true, std::memory_order_release);
    }
    ESP_LOGI(kTag, "Measured FPS/CPU readout requested: %s (session preference)", enabled ? "ON" : "OFF");
    return ESP_OK;
}

// expression_key: Exact implemented catalog key, resolved once into a typed identity.
// Exception: Returns lookup/state/lock errors or failure to end an interrupted benchmark.
extern "C" esp_err_t CODY_companion_set_expression(const char *expression_key)
{
    const CODY::ExpressionDefinition *definition = nullptr;
    const esp_err_t lookup_error = CODY::find_expression(expression_key, definition);
    if (lookup_error != ESP_OK) {
        return lookup_error;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Expression selection lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running ||
        s_state.boot_phase != BootPhase::Ready || !CODY::lifecycle_is_resting(s_state.lifecycle) ||
        !LUNASHELL_display_runtime_writes_allowed()) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Expression %s rejected during lifecycle/power transition (%s)",
            definition->key, esp_err_to_name(error));
        return error;
    }
    const esp_err_t benchmark_error = stop_raster_benchmark(false, "expression-selection");
    if (benchmark_error != ESP_OK) {
        return benchmark_error;
    }
    s_state.requested_expression = definition->id;
    s_state.expression_override = true;
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Expression selected: %s; rhythmic channels remain continuous", definition->key);
    return ESP_OK;
}

// enabled: Non-null output populated only after the consumer state has been accepted.
extern "C" esp_err_t CODY_companion_get_debug_readout_enabled(bool *enabled)
{
    if (enabled == nullptr) {
        ESP_LOGE(kTag, "Readout preference output rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Readout preference read lock failed (%s)", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Readout preference read state rejected (%s)", esp_err_to_name(error));
        return error;
    }
    *enabled = s_state.debug_readout_enabled;
    return ESP_OK;
}

// enabled: Session flag for active PWR countdowns; an admitted capture completes.
// Exception: Returns ESP_ERR_NOT_SUPPORTED in Release or the real renderer state error.
extern "C" esp_err_t CODY_companion_set_power_screenshot_enabled(bool enabled)
{
    if constexpr (!LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
        ESP_LOGE(kTag, "PWR screenshot flag=%u rejected: Debug only (%s)",
                 static_cast<unsigned>(enabled), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "PWR screenshot flag change rejected (%s)", esp_err_to_name(error));
        return error;
    }
    s_power_screenshot_enabled.store(enabled, std::memory_order_release);
    ESP_LOGI(kTag, "PWR screenshot flag=%u (session preference)", static_cast<unsigned>(enabled));
    return ESP_OK;
}

// enabled: Receives the current session flag on success only.
// Exception: Returns invalid argument or the real renderer state error.
extern "C" esp_err_t CODY_companion_get_power_screenshot_enabled(bool *enabled)
{
    if (enabled == nullptr) {
        ESP_LOGE(kTag, "PWR screenshot flag output rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "PWR screenshot flag read rejected (%s)", esp_err_to_name(error));
        return error;
    }
    *enabled = s_power_screenshot_enabled.load(std::memory_order_acquire);
    return ESP_OK;
}

// enabled: Runtime preference applied by the worker under the shared display/state lock.
// Exception: Returns state/lock/feature errors or failure to end an interrupted benchmark.
extern "C" esp_err_t CODY_companion_set_face_deformation(bool enabled)
{
#if CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Face deformation lock failed: %s", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Face deformation state rejected: %s", esp_err_to_name(error));
        return error;
    }
    const esp_err_t benchmark_error = stop_raster_benchmark(false, "face-deformation");
    if (benchmark_error != ESP_OK) {
        return benchmark_error;
    }
    s_state.face_follows_body = enabled;
    s_state.frame_dirty.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Face deformation requested: %s (session preference)", enabled ? "FOLLOW BODY" : "FIXED");
    return ESP_OK;
#else
    ESP_LOGE(kTag, "Face deformation=%u rejected: pearl renderer excluded (%s)",
             static_cast<unsigned>(enabled), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// enabled: Non-null output populated only after checking the current consumer state.
extern "C" esp_err_t CODY_companion_get_face_deformation(bool *enabled)
{
    if (enabled == nullptr) {
        ESP_LOGE(kTag, "Face deformation output rejected: %s", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_LUNASHELL_CODY_RENDER_PEARL_JELLY
    const esp_err_t lock_error = bsp_display_lock(kDisplayLockTimeoutMs);
    if (lock_error != ESP_OK) {
        ESP_LOGE(kTag, "Face preference read lock failed: %s", esp_err_to_name(lock_error));
        return lock_error;
    }
    const DisplayUnlockOnExit lock;
    const StartState state = s_start_state.load(std::memory_order_acquire);
    if (state != StartState::Running) {
        const esp_err_t error = state == StartState::Failed
            ? s_terminal_error.load(std::memory_order_acquire) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Face preference read state rejected: %s", esp_err_to_name(error));
        return error;
    }
    *enabled = s_state.face_follows_body;
    return ESP_OK;
#else
    ESP_LOGE(kTag, "Face preference read rejected: pearl renderer excluded (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#else

// frame: Output remains untouched because this profile has no Cody consumer.
// Exception: Returns ESP_ERR_NOT_SUPPORTED and logs the rejected operation.
esp_err_t CODY::get_personality_frame(PersonalityFrame &frame)
{
    (void)frame;
    ESP_LOGE("CODY_companion", "Personality frame read rejected: consumer excluded (%s)",
        esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// cue: Requested typed symbol; the explicitly selected profile excludes the consumer.
// Exception: Returns ESP_ERR_NOT_SUPPORTED and logs the rejected operation.
esp_err_t CODY::set_personality_cue(PersonalityCue cue)
{
    ESP_LOGE("CODY_companion", "Narrative cue=%u rejected: consumer excluded (%s)",
        static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// cue: Requested device symbol; the explicitly selected profile excludes the consumer.
// Exception: Returns ESP_ERR_NOT_SUPPORTED and logs the rejected operation.
esp_err_t CODY::set_personality_device_cue(PersonalityCue cue)
{
    ESP_LOGE("CODY_companion", "Device cue=%u rejected: consumer excluded (%s)",
        static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// cue: Output remains untouched because this profile has no Cody consumer.
// Exception: Returns ESP_ERR_NOT_SUPPORTED and logs the rejected operation.
esp_err_t CODY::get_personality_cue(PersonalityCue &cue)
{
    (void)cue;
    ESP_LOGE("CODY_companion", "Personality read rejected: consumer excluded (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// snapshot: Output remains unchanged because this explicit profile excludes Cody.
// Exception: Returns ESP_ERR_NOT_SUPPORTED and logs the rejected operation.
esp_err_t CODY::get_happiness(HappinessSnapshot &snapshot)
{
    (void)snapshot;
    ESP_LOGE("CODY_companion", "Happiness read rejected: consumer excluded (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// expression_key: Requested catalog key; this profile excludes the Cody consumer.
extern "C" esp_err_t CODY_companion_set_expression(const char *expression_key)
{
    ESP_LOGE("CODY_companion", "Expression selection=%s rejected: consumer excluded (%s)",
        expression_key != nullptr ? expression_key : "<null>", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Requested session preference; this profile does not include the Cody consumer.
extern "C" esp_err_t CODY_companion_set_debug_readout_enabled(bool enabled)
{
    ESP_LOGE("CODY_companion", "Readout preference=%u rejected: consumer excluded (%s)",
        static_cast<unsigned>(enabled), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Non-null output is left unchanged when the consumer is excluded.
extern "C" esp_err_t CODY_companion_get_debug_readout_enabled(bool *enabled)
{
    const esp_err_t error = enabled == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
    ESP_LOGE("CODY_companion", "Readout preference read rejected: consumer excluded (%s)", esp_err_to_name(error));
    return error;
}

// enabled: Requested preference, unavailable in a profile without the Cody consumer.
extern "C" esp_err_t CODY_companion_set_face_deformation(bool enabled)
{
    ESP_LOGE("CODY_companion", "Face deformation=%u rejected: consumer excluded (%s)",
             static_cast<unsigned>(enabled), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Requested PWR preference; this profile excludes the Cody consumer.
extern "C" esp_err_t CODY_companion_set_power_screenshot_enabled(bool enabled)
{
    ESP_LOGE("CODY_companion", "PWR screenshot flag=%u rejected: consumer excluded (%s)",
             static_cast<unsigned>(enabled), esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
}

// enabled: Output remains untouched because this profile has no Cody consumer.
extern "C" esp_err_t CODY_companion_get_power_screenshot_enabled(bool *enabled)
{
    const esp_err_t error = enabled == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
    ESP_LOGE("CODY_companion", "PWR screenshot flag read rejected (%s)", esp_err_to_name(error));
    return error;
}

// enabled: Output stays untouched because this profile cannot own a face preference.
extern "C" esp_err_t CODY_companion_get_face_deformation(bool *enabled)
{
    const esp_err_t error = enabled == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_NOT_SUPPORTED;
    ESP_LOGE("CODY_companion", "Face preference read rejected: %s", esp_err_to_name(error));
    return error;
}

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
