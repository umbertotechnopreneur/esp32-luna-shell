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

// Host transport only: geometry, animation and RGB565 rasterization stay in firmware C++.
#include "CODY_rig.hpp"
#include "CODY_connection_personality.hpp"
#include "CODY_jelly.hpp"
#include "CODY_scene.hpp"
#include "CODY_activity_description.hpp"
#include "CODY_activity_identity.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_observation.hpp"
#include "CODY_technical_panels.hpp"
#include "CODY_home_dashboard.hpp"
#include "CODY_home_identity.hpp"
#include "CODY_model_cost_notice.hpp"
#include "CODY_status_overlay.hpp"
#include "home_dashboard_fonts.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_companion_mode.hpp"
#include "LUNASHELL_snapshot_aging.hpp"
#include "CODY_response.hpp"
#include "CODY2D_controls.hpp"
#include "CODY_debug_views.hpp"
#include "cloud_rig_data.hpp"
#include "scene_data.hpp"
#include "esp_log.h"
#include <cstring>
#include <iterator>
#include <new>
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct HostScene {
    CODY::RigScene rig{};
    CODY::RigDebugStrokeCache debug{};
    CODY::ActivityOverlayFrame activity_overlay{};
    CODY::ActivityDescriptionState activity_description{};
    CODY::ActivityIdentityState activity_identity{};
    CODY::PanelPresentation companion_panel{};
    LUNASHELL::CodexTechnicalObservation panel_observation{};
    const CODY::HomeDashboardPresentation *home_dashboard = nullptr;
    const CODY::HomeIdentityPresentation *home_identity = nullptr;
    const CODY::CommandHintsState *home_commands = nullptr;
    CODY::SceneFrame home_frame{};
    int64_t home_observation_time_us = 0;
    char home_title[65]{};
};
struct HostState {
    CODY::HomeDashboardPresentation home_dashboard{};
    CODY::HomeIdentityPresentation home_identity{};
    CODY::CommandHintsState home_commands{};
    CODY::HomeDashboardRotationState home_rotation{};
    CODY::ModelCostNoticeState model_cost_notice{};
    CODY::PersonalityLag personality{};
    CODY::ConnectionPersonalityState connection{};
    CODY::ResponseState response = CODY::make_response_state(CODY::Data::response);
    CODY::ActivityObservationState activity_observation{};
    CODY::CompanionTapState companion_tap{};
    CODY::CompanionShakeState companion_shake{};
    bool away = false;
};
// Wire data contains PC-only observations. It never enters DeviceSnapshotService.
struct HostResponseInputs {
    int32_t health[6];
    int32_t errors[6];
    int64_t timestamps[6];
    int64_t last_interaction;
    int64_t gesture_started;
    float acceleration[3];
    float power;
    float drag[2];
    uint32_t microphone_mean[2];
    uint32_t microphone_peak[2];
    uint32_t playback_mean;
    uint32_t playback_cue;
    uint16_t rotation;
    uint16_t touch_rotation;
    uint8_t microphone_count;
    uint8_t charging;
    uint8_t playback_active;
    uint8_t pressed;
    uint8_t idle_allowed;
    uint8_t eye;
};
// The PC transfers original source durations; the canonical C++ policy ages them once.
struct HostPanelObservation {
    const char *project;
    const char *branch;
    const char *description;
    const char *chat_id;
    const char *technical_chat_id;
    const char *model;
    const char *effort;
    uint8_t phase;
    uint8_t compaction;
    uint8_t cost_band;
    bool connected;
    bool link_valid;
    bool link_stale;
    bool activity_valid;
    bool activity_stale;
    bool technical_valid;
    bool model_valid;
    bool effort_valid;
    bool compaction_valid;
    bool compaction_age_valid;
    bool activity_age_valid;
    bool turn_elapsed_valid;
    uint64_t model_age_us;
    uint64_t compaction_age_us;
    uint64_t activity_age_us;
    uint64_t turn_elapsed_us;
    uint64_t receipt_age_us;
    uint64_t cost_age_us;
    uint64_t cost_valid_for_us;
    bool used_valid;
    bool capacity_valid;
    bool cumulative_valid;
    bool input_valid;
    bool output_valid;
    bool threshold_valid;
    uint64_t used;
    uint64_t capacity;
    uint64_t cumulative;
    uint64_t input;
    uint64_t output;
    uint64_t threshold;
    uint64_t usage_age_us;
    const char *device_ip;
    bool device_ip_valid;
    bool reconnecting;
    bool simulated_motion_valid;
    uint8_t simulated_motion;
    uint64_t simulated_motion_age_us;
    const char *chat_title;
    uint8_t chat_title_state;
    uint64_t chat_title_age_us;
    int32_t chat_title_error;
    uint8_t simulated_stillness;
    bool simulated_stillness_valid;
    int32_t simulated_stillness_error;
    uint64_t simulated_stillness_age_us;
    uint8_t simulated_motion_rejection;
    int32_t simulated_motion_error;
};

// source: Borrowed NUL-terminated printable ASCII producer string.
// destination: Fixed owner buffer, including terminator space.
// Exception: Rejects original overlong or non-ASCII metadata without repairing it.
template <size_t Size>
esp_err_t copy_panel_source(const char *source, char (&destination)[Size])
{
    if (!source) { return ESP_ERR_INVALID_ARG; }
    for (size_t index = 0; index < Size; ++index) {
        if (source[index] == '\0') {
            std::memcpy(destination, source, index + 1U);
            return ESP_OK;
        }
        if (source[index] < 32 || source[index] > 126) { return ESP_ERR_INVALID_ARG; }
    }
    return ESP_ERR_INVALID_SIZE;
}

// operation: Actual host boundary operation that failed.
// error: Original error returned by portable firmware code.
esp_err_t host_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE("CodyHostGraphics", "%s failed: %s (0x%x)", operation, esp_err_to_name(error), error);
    }
    return error;
}

// values: Borrowed contiguous facial channels from the typed host catalog.
// count: Number of provided values; must match the compiled rig exactly.
// channels: Receives channels only after validating the boundary shape.
// Exception: Returns invalid arguments for null or mismatched input.
esp_err_t read_channels(const float *values, uint32_t count, CODY::Channels &channels)
{
    if (!values || count != CODY::kFaceChannelCount) {
        return host_result("read_channels", ESP_ERR_INVALID_ARG);
    }
    std::copy(values, values + count, channels.values);
    return ESP_OK;
}
}

// handle: Host frame owning exactly the currently displayed native rig.
// scene: Receives a borrowed immutable model pointer after validation.
// Exception: Returns invalid argument/state without exposing an invalid frame.
esp_err_t cody_host_scene_model(const void *handle, const CODY::RigScene *&scene)
{
    scene = nullptr;
    if (!handle) { return host_result("scene_model", ESP_ERR_INVALID_ARG); }
    const auto &model = static_cast<const HostScene *>(handle)->rig;
    if (!model.valid) { return host_result("scene_model", ESP_ERR_INVALID_STATE); }
    scene = &model;
    return ESP_OK;
}

// No parameters: versioned boundary rejects outdated libraries explicitly.
CODY_HOST_EXPORT uint32_t cody_host_abi() { return 2; }
// No parameters: exact compiled semantic facial channel count.
CODY_HOST_EXPORT uint32_t cody_host_channel_count() { return CODY::kFaceChannelCount; }
// index: Storage traversal offset; returned name retains domain identity.
CODY_HOST_EXPORT const char *cody_host_channel_name(uint32_t index)
{
    return index < std::size(CODY::Data::channel_names) ? CODY::Data::channel_names[index] : nullptr;
}
// No parameters: number of authored points in the compiled shared rig.
CODY_HOST_EXPORT uint32_t cody_host_point_count() { return static_cast<uint32_t>(std::size(CODY::Data::points)); }
// index: Storage traversal offset whose authored stable name is returned.
CODY_HOST_EXPORT const char *cody_host_point_name(uint32_t index)
{
    return index < std::size(CODY::Data::points) ? CODY::Data::points[index].name : nullptr;
}
// No parameters: number of authored paths in the compiled shared rig.
CODY_HOST_EXPORT uint32_t cody_host_path_count() { return static_cast<uint32_t>(std::size(CODY::Data::paths)); }
// index: Storage traversal offset whose authored stable name is returned.
CODY_HOST_EXPORT const char *cody_host_path_name(uint32_t index)
{
    return index < std::size(CODY::Data::paths) ? CODY::Data::paths[index].name : nullptr;
}
// No parameters: allocate an independently owned frame, never shared with pending redraws.
// Exception: Returns nullptr on allocation failure; no exception crosses the C ABI.
CODY_HOST_EXPORT void *cody_host_scene_create()
{
    auto *scene = new (std::nothrow) HostScene;
    if (!scene) { host_result("scene_allocate", ESP_ERR_NO_MEM); }
    return scene;
}
// handle: Frame allocated by this library, or nullptr.
CODY_HOST_EXPORT void cody_host_scene_destroy(void *handle) { delete static_cast<HostScene *>(handle); }
// No parameters: allocate stateful native personality and connection owners.
// Exception: Returns nullptr on allocation failure.
CODY_HOST_EXPORT void *cody_host_state_create()
{
    auto *state = new (std::nothrow) HostState;
    if (!state) { host_result("state_allocate", ESP_ERR_NO_MEM); }
    return state;
}
// handle: State allocated by this library, or nullptr.
CODY_HOST_EXPORT void cody_host_state_destroy(void *handle) { delete static_cast<HostState *>(handle); }

// handle: Independently owned output frame; invalidated by evaluate_rig on entry.
// values: Finite bounded facial controls in checked semantic order.
// count: Exact compiled channel count.
// transform: Three floats holding logical x, y and scale.
// contour: Optional five floats holding four contour controls then integrated phase.
// deform_face: Explicit face/body warp policy for the real pearl renderer.
// Exception: Returns original pose or allocation-boundary error.
CODY_HOST_EXPORT int cody_host_evaluate(void *handle, const float *values, uint32_t count,
    const float *transform, const float *contour, bool deform_face)
{
    if (handle) { static_cast<HostScene *>(handle)->rig.valid = false; }
    if (!handle || !transform) { return host_result("evaluate", ESP_ERR_INVALID_ARG); }
    CODY::Channels channels{};
    const esp_err_t error = read_channels(values, count, channels);
    if (error != ESP_OK) { return error; }
    CODY::ContourState shape{};
    if (contour) {
        std::copy(contour, contour + CODY::kContourControlCount, shape.controls.values);
        shape.phase = contour[CODY::kContourControlCount];
    }
    return host_result("evaluate_rig", CODY::evaluate_rig(channels,
        {transform[0], transform[1], transform[2]}, static_cast<HostScene *>(handle)->rig,
        contour ? &shape : nullptr, deform_face));
}

// handle: Successfully evaluated independent scene.
// coordinates: Receives local then world x/y for each authored point.
// coordinate_count: Exact destination float count, four per authored point.
// Exception: Returns original invalid-state or boundary-size error.
CODY_HOST_EXPORT int cody_host_points(void *handle, float *coordinates, uint32_t coordinate_count)
{
    if (!handle || !coordinates || coordinate_count != std::size(CODY::Data::points) * 4) {
        return host_result("points", ESP_ERR_INVALID_ARG);
    }
    const auto &scene = static_cast<HostScene *>(handle)->rig;
    if (!scene.valid) { return host_result("points", ESP_ERR_INVALID_STATE); }
    for (size_t index = 0; index < std::size(CODY::Data::points); ++index) {
        const auto identity = CODY::to_index(CODY::Data::points[index].id);
        coordinates[index * 4] = scene.local[identity].x;
        coordinates[index * 4 + 1] = scene.local[identity].y;
        coordinates[index * 4 + 2] = scene.world[identity].x;
        coordinates[index * 4 + 3] = scene.world[identity].y;
    }
    return ESP_OK;
}

// handle: Successfully evaluated independent scene.
// index: Checked path storage traversal offset.
// metadata: Receives count, visibility and quantized opacity.
// vertices: Receives sampled x/y from the actual native cubic evaluator.
// capacity: Available float slots in the destination.
// Exception: Returns invalid state, identity or destination capacity.
CODY_HOST_EXPORT int cody_host_path(void *handle, uint32_t index, uint32_t *metadata,
    float *vertices, uint32_t capacity)
{
    if (!handle || !metadata || !vertices || index >= std::size(CODY::Data::paths)) {
        return host_result("path", ESP_ERR_INVALID_ARG);
    }
    const auto &scene = static_cast<HostScene *>(handle)->rig;
    const auto &path = scene.paths[CODY::to_index(CODY::Data::paths[index].id)];
    if (!scene.valid || capacity < path.count * 2U) {
        return host_result("path", !scene.valid ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE);
    }
    metadata[0] = path.count;
    metadata[1] = path.visible;
    metadata[2] = path.opacity;
    for (uint16_t vertex = 0; vertex < path.count; ++vertex) {
        const auto &point = scene.vertices[path.first_vertex + vertex];
        vertices[vertex * 2] = point.x;
        vertices[vertex * 2 + 1] = point.y;
    }
    return ESP_OK;
}

// handle: Successfully evaluated scene owning its native scale.
// scale: Receives the real evaluated scale only after successful validation.
// Exception: Returns invalid argument/state; never substitutes zero for a failed frame.
CODY_HOST_EXPORT int cody_host_scale(void *handle, float *scale)
{
    if (!handle || !scale) { return host_result("scale", ESP_ERR_INVALID_ARG); }
    const auto &scene = static_cast<HostScene *>(handle)->rig;
    if (!scene.valid) { return host_result("scale", ESP_ERR_INVALID_STATE); }
    *scale = scene.scale;
    return ESP_OK;
}

// handle: Successfully evaluated immutable frame.
// pixels: Borrowed full logical RGB565 framebuffer owned by Python.
// capacity: Exact pixel count, 466 squared.
// debug: Draw the firmware skeleton and handles.
// selected_name: Optional stable authored point name; ambiguous/missing names are rejected.
// paused: Native toolbar Play/Pause state.
// navigation_visible: Active scene's navigation permission.
// navigation_opacity: Explicit wall-clock toolbar opacity.
// background: Authored RGB565 clear color.
// Exception: Returns original native preparation or draw error.
CODY_HOST_EXPORT int cody_host_draw(void *handle, uint16_t *pixels, uint32_t capacity,
    bool debug, const char *selected_name, bool paused, bool navigation_visible,
    uint8_t navigation_opacity, uint16_t background)
{
    if (!handle || !pixels || capacity != 466U * 466U) { return host_result("draw", ESP_ERR_INVALID_ARG); }
    auto &frame = *static_cast<HostScene *>(handle);
    CODY::PointId selected = CODY::kNoPoint;
    if (selected_name) {
        uint16_t matches = 0;
        for (const auto &point : CODY::Data::points) {
            if (std::strcmp(point.name, selected_name) == 0) { selected = point.id; ++matches; }
        }
        if (matches != 1) { return host_result("selected_point", ESP_ERR_NOT_FOUND); }
    }
    if (debug) {
        const esp_err_t error = CODY::prepare_rig_debug_strokes(frame.rig, selected, frame.debug);
        if (error != ESP_OK) { return host_result("prepare_debug", error); }
    }
    // Use firmware-sized strips: identical clipping/raster code writes PC-owned pixels.
    for (uint16_t first = 0; first < 466; first += 16) {
        const uint16_t height = std::min<uint16_t>(16, 466 - first);
        CODY2D::Surface strip{pixels + first * 466U, 466, height, 466, uint32_t(height) * 466U};
        if (frame.companion_panel.valid) {
            // Details retain the same live native character beneath the transparent text layer.
            esp_err_t panel_error = CODY::draw_rig_rows(frame.rig, strip, first, false,
                CODY::kNoPoint, nullptr, paused, 0, nullptr, false, 0, background,
                CODY::RigControlState::Idle, nullptr);
            if (panel_error == ESP_OK) {
                panel_error = CODY::draw_companion_panel_rows(frame.companion_panel, strip, first, 0);
            }
            if (panel_error != ESP_OK) { return host_result("draw_companion_panel", panel_error); }
            continue;
        }
        esp_err_t error = CODY::draw_rig_rows(frame.rig, strip, first, debug, selected,
            nullptr, paused, 0, debug ? &frame.debug : nullptr,
            navigation_visible, navigation_opacity, background,
            CODY::RigControlState::Idle, frame.home_dashboard == nullptr ? &frame.activity_overlay : nullptr);
        if (error != ESP_OK) { return host_result("draw_rig_rows", error); }
        if (frame.home_identity != nullptr) {
            error = CODY2D::draw_text_rows(frame.home_frame.title, frame.home_frame.visible_characters, strip, first, 0);
            if (error == ESP_OK) { error = CODY::draw_home_identity_rows(*frame.home_identity, strip, first, 0); }
            if (error != ESP_OK) { return host_result("draw_home_identity", error); }
        } else if (frame.activity_description.length != 0) {
            error = CODY::draw_activity_description(frame.activity_description, strip, first, 0);
            if (error != ESP_OK) { return host_result("draw_activity_description", error); }
        }
        error = frame.home_identity == nullptr ? CODY::draw_activity_identity(frame.activity_identity, strip, first, 0) : ESP_OK;
        if (error != ESP_OK) { return host_result("draw_activity_identity", error); }
        if (frame.home_dashboard != nullptr) {
            error = CODY::draw_home_dashboard_rows(*frame.home_dashboard, strip, first, 0);
            if (error != ESP_OK) { return host_result("draw_home_dashboard", error); }
        }
        if (frame.home_commands != nullptr) {
            error = CODY::draw_command_hints(*frame.home_commands, strip, first, 0);
            if (error != ESP_OK) { return host_result("draw_home_commands", error); }
        }
    }
    return ESP_OK;
}

// handle: Native rig owner whose absolute layer is retained through rasterization.
// state: Shared typed activity identity derived from real or visibly SIMULATED activity.
// elapsed_us: Active presentation clock supplied by the host route owner.
// Exception: Returns invalid handle or unknown activity identity without changing state.
CODY_HOST_EXPORT int cody_host_activity_overlay(void *handle, uint8_t state, uint32_t elapsed_us)
{
    if (!handle) {
        return host_result("activity_overlay", ESP_ERR_INVALID_ARG);
    }
    const auto identity = static_cast<CODY::ActivityOverlayState>(state);
    switch (identity) {
    case CODY::ActivityOverlayState::Hidden:
        elapsed_us = 0;
        break;
    case CODY::ActivityOverlayState::Working:
    case CODY::ActivityOverlayState::Waiting:
    case CODY::ActivityOverlayState::Compacting:
        elapsed_us %= CODY::kActivityOverlayCycleUs;
        break;
    case CODY::ActivityOverlayState::ResponseReady:
        elapsed_us = std::min(elapsed_us, CODY::kActivityOverlayResponseUs);
        break;
    default:
        return host_result("activity_overlay", ESP_ERR_INVALID_ARG);
    }
    static_cast<HostScene *>(handle)->activity_overlay = {identity, elapsed_us};
    return ESP_OK;
}

// current: Typed Home or technical identity supplied by the passive navigation owner.
// next: Receives the shared native next-page identity.
// Exception: Returns invalid pointer or the real native identity rejection.
CODY_HOST_EXPORT int cody_host_panel_next(uint8_t current, uint8_t *next)
{
    if (!next) { return host_result("panel_next", ESP_ERR_INVALID_ARG); }
    CODY::CompanionPanel selected = CODY::CompanionPanel::Home;
    const esp_err_t error = CODY::next_home_dashboard_panel(static_cast<CODY::CompanionPanel>(current), selected);
    if (error == ESP_OK) { *next = static_cast<uint8_t>(selected); }
    return host_result("panel_next", error);
}

// transform: Receives the shared passive-panel logical x/y/scale, in that order.
// Exception: Returns invalid argument for a missing destination.
CODY_HOST_EXPORT int cody_host_panel_transform(float *transform)
{
    if (!transform) { return host_result("panel_transform", ESP_ERR_INVALID_ARG); }
    const auto &placement = CODY::kCompanionPanelCharacterTransform;
    transform[0] = placement.x;
    transform[1] = placement.y;
    transform[2] = placement.scale;
    return ESP_OK;
}

// handle: Native scene retaining stable technical captions until raster retirement.
// panel: Explicit page identity; Home clears the technical layer.
// inputs: Borrowed typed PC projection, consumed during this call only.
// active_time_us: Nonnegative presentation clock for the indeterminate compaction accent.
// Exception: Returns real input, metric or shared native layout errors.
CODY_HOST_EXPORT int cody_host_panel_prepare(void *handle, uint8_t panel,
    const CODY::PanelInputs *inputs, int64_t active_time_us)
{
    if (!handle || !inputs || active_time_us < 0) { return host_result("panel_prepare", ESP_ERR_INVALID_ARG); }
    auto &owner = *static_cast<HostScene *>(handle);
    if (panel == static_cast<uint8_t>(CODY::CompanionPanel::Home)) {
        owner.companion_panel.valid = false;
        return ESP_OK;
    }
    CODY::SceneContent content{};
    return host_result("panel_prepare", CODY::prepare_companion_panel(
        static_cast<CODY::CompanionPanel>(panel), *inputs, CODY::SceneData::body_font,
        active_time_us, owner.companion_panel, content));
}

// source: Original PC-observed metadata and source-relative ages, without synthetic counters.
// observation: Component-owned host scratch; never published into DeviceSnapshotService.
// Exception: Returns original metadata/clock errors; the shared canonical owner ages the projection.
static esp_err_t project_host_panel_observation(const HostPanelObservation *source,
    LUNASHELL::CodexTechnicalObservation &observation)
{
    if (!source || source->phase > 9 || source->compaction > 5 || source->cost_band > 3 || source->chat_title_state > 3 ||
        source->receipt_age_us > static_cast<uint64_t>(INT64_MAX - 1) ||
        (source->capacity_valid && source->capacity == 0)) {
        return host_result("panel_observation", ESP_ERR_INVALID_ARG);
    }
    // Every supported field below is overwritten in place. Unsupported metric flags stay absent;
    // an aggregate reset here would create a large nested temporary on some compilers.
    auto &link = observation.link;
    auto &technical = observation.technical;
    esp_err_t error = copy_panel_source(source->project, link.activity.project);
    if (error == ESP_OK) { error = copy_panel_source(source->branch, link.activity.branch); }
    if (error == ESP_OK) { error = copy_panel_source(source->description, link.activity.description); }
    if (error == ESP_OK) { error = copy_panel_source(source->chat_id, link.activity.chat_id); }
    if (error == ESP_OK) { error = copy_panel_source(source->chat_title, link.activity.title); }
    if (error == ESP_OK) { error = copy_panel_source(source->technical_chat_id, technical.chat_id); }
    if (error == ESP_OK) { error = copy_panel_source(source->model, technical.model); }
    if (error == ESP_OK) { error = copy_panel_source(source->effort, technical.effort); }
    if (error != ESP_OK) { return host_result("panel_observation_source", error); }
    link.state = source->connected ? LUNASHELL::CodexLinkState::Connected :
        source->reconnecting ? LUNASHELL::CodexLinkState::Discovering : LUNASHELL::CodexLinkState::Idle;
    link.retained_link_present = source->reconnecting;
    link.data_valid = source->link_valid;
    link.data_stale = source->link_stale;
    link.activity.state = static_cast<LUNASHELL::CodexActivityState>(source->phase);
    link.activity.data_valid = source->activity_valid;
    link.activity.data_stale = source->activity_stale;
    technical.data_valid = source->technical_valid;
    // Validity, age and same-chat admission are independent from owner-operation failures.
    // This DTO carries no failed-operation error: disconnect or stale input cannot invent one.
    link.health = source->link_valid ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Missing;
    link.last_error = ESP_OK;
    link.activity.health = source->activity_valid ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Missing;
    link.activity.last_error = ESP_OK;
    technical.health = source->technical_valid ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Missing;
    technical.last_error = ESP_OK;
    // A positive arbitrary receipt origin plus a duration recreates the same aging semantics.
    // This scratch never enters DeviceSnapshotService or a physical firmware snapshot.
    technical.received_at_us = 1;
    technical.model_valid = source->model_valid;
    technical.model_source = source->model_valid ? LUNASHELL::CodexModelSource::HookMetadata : LUNASHELL::CodexModelSource::Unavailable;
    technical.model_age_at_receive_us = source->model_age_us;
    technical.cost_band = static_cast<LUNASHELL::CodexCostBand>(source->cost_band);
    technical.cost_source = source->cost_band != 0 ? LUNASHELL::CodexCostSource::CodexStandardCatalog :
        LUNASHELL::CodexCostSource::Unavailable;
    technical.cost_age_at_receive_us = source->cost_age_us;
    technical.cost_valid_for_us = source->cost_valid_for_us;
    technical.effort_valid = source->effort_valid;
    technical.compaction = static_cast<LUNASHELL::CodexCompactionState>(source->compaction);
    technical.compaction_valid = source->compaction_valid;
    technical.compaction_age_valid = source->compaction_age_valid;
    technical.compaction_age_at_receive_us = source->compaction_age_us;
    technical.activity_age_valid = source->activity_age_valid;
    technical.activity_age_at_receive_us = source->activity_age_us;
    technical.turn_elapsed_valid = source->turn_elapsed_valid;
    technical.turn_elapsed_at_receive_us = source->turn_elapsed_us;
    technical.used_valid = source->used_valid;
    technical.capacity_valid = source->capacity_valid;
    technical.context_valid = source->used_valid && source->capacity_valid;
    technical.cumulative_valid = source->cumulative_valid;
    technical.input_valid = source->input_valid;
    technical.output_valid = source->output_valid;
    technical.threshold_valid = source->threshold_valid;
    technical.used = source->used;
    technical.capacity = source->capacity;
    technical.cumulative = source->cumulative;
    technical.input = source->input;
    technical.output = source->output;
    technical.threshold = source->threshold;
    technical.usage_age_at_receive_us = source->usage_age_us;
    const int64_t observation_time = static_cast<int64_t>(source->receipt_age_us) + 1;
    // The host supplies the title's original source age plus its ACTIVITY receipt
    // age. Anchor that already-aged duration at this projection's current origin;
    // TECH receipt age must not be counted a second time for an independent field.
    link.activity.title_state = static_cast<LUNASHELL::CodexChatTitleState>(source->chat_title_state);
    link.activity.title_age_at_receive_us = source->chat_title_age_us;
    link.activity.title_error = source->chat_title_error;
    link.activity.last_attempt_at_us = observation_time;
    LUNASHELL::age_codex_technical(technical, observation_time);
    return ESP_OK;
}

// handle: Native frame owning the canonical-shaped PC-only scratch and prepared panel.
// panel: Explicit selected technical page; Home removes the technical layer.
// source: Original observed metadata and source-relative ages, without synthetic counters.
// active_time_us: Local presentation clock for numeric pages and accent, independent of receipt age.
// Exception: Returns actual metadata, clock or shared canonical preparation failure.
CODY_HOST_EXPORT int cody_host_panel_observation(void *handle, uint8_t panel,
    const HostPanelObservation *source, int64_t active_time_us)
{
    if (!handle || active_time_us < 0) { return host_result("panel_observation", ESP_ERR_INVALID_ARG); }
    auto &owner = *static_cast<HostScene *>(handle);
    auto &observation = owner.panel_observation;
    esp_err_t error = project_host_panel_observation(source, observation);
    if (error != ESP_OK) { return error; }
    if (panel == 0) { owner.companion_panel.valid = false; return ESP_OK; }
    const int64_t observation_time = static_cast<int64_t>(source->receipt_age_us) + 1;
    CODY::SceneContent content{};
    error = CODY::prepare_companion_panel_from_observation(static_cast<CODY::CompanionPanel>(panel),
        observation.link, observation.technical, CODY::SceneData::body_font, observation_time, active_time_us, owner.companion_panel, content);
    if (error == ESP_OK) {
        bool changed = false;
        error = CODY::advance_companion_panel(owner.companion_panel, active_time_us, changed);
    }
    return host_result("panel_observation", error);
}

// handle: Recreated native frame borrowing the persistent dashboard until raster retirement.
// state: Host owner retained across frames; prepared glyphs never borrow stack storage.
// panel: Explicit Home, Model or Session lower-panel identity.
// source: Original PC-only observation; existing canonical-shaped projection owns age admission.
// active_time_us: Pausable presentation clock, never a token source timestamp.
// Exception: Returns original projection, formatting and native layout errors.
CODY_HOST_EXPORT int cody_host_home_dashboard(void *handle, void *state, uint8_t panel,
    const HostPanelObservation *source, int64_t active_time_us)
{
    if (!handle || !state || !source || !source->device_ip || active_time_us < 0 ||
        source->simulated_motion > static_cast<uint8_t>(LUNASHELL::ActivityClass::Walking) ||
        source->simulated_motion_rejection > static_cast<uint8_t>(LUNASHELL::ActivityRejection::Suspended) ||
        source->simulated_stillness > static_cast<uint8_t>(LUNASHELL::ImuStillnessState::Still) ||
        source->simulated_motion_age_us >= static_cast<uint64_t>(INT64_MAX) ||
        source->simulated_stillness_age_us >= static_cast<uint64_t>(INT64_MAX)) {
        return host_result("home_dashboard", ESP_ERR_INVALID_ARG);
    }
    auto &frame = *static_cast<HostScene *>(handle);
    auto &owner = static_cast<HostState *>(state)->home_dashboard;
    esp_err_t error = project_host_panel_observation(source, frame.panel_observation);
    if (error != ESP_OK) { return error; }
    // Only explicit SIMULATED inputs populate these independent owner projections.
    // No stationary threshold, hold rule or classifier runs in the host driver.
    LUNASHELL::ActivitySnapshot motion{};
    const uint64_t origin = std::max(source->simulated_motion_age_us, source->simulated_stillness_age_us);
    if (source->receipt_age_us > static_cast<uint64_t>(INT64_MAX) - origin - 1U) {
        return host_result("home_dashboard_source_clock", ESP_ERR_INVALID_ARG);
    }
    const int64_t source_time = static_cast<int64_t>(source->receipt_age_us + origin) + 1;
    // Translate every timestamp used by this frame by the same origin. This keeps
    // original source ages positive and independent of an unrelated TECH receipt.
    frame.panel_observation.technical.received_at_us += static_cast<int64_t>(origin);
    frame.panel_observation.link.activity.last_attempt_at_us += static_cast<int64_t>(origin);
    motion.rejection = static_cast<LUNASHELL::ActivityRejection>(source->simulated_motion_rejection);
    motion.error = source->simulated_motion_error;
    if (source->simulated_motion_valid) {
        motion.activity = static_cast<LUNASHELL::ActivityClass>(source->simulated_motion);
        motion.scores_valid = true;
        motion.window_end_us = source_time - static_cast<int64_t>(source->simulated_motion_age_us);
    }
    motion.stillness.state = static_cast<LUNASHELL::ImuStillnessState>(source->simulated_stillness);
    motion.stillness.data_valid = source->simulated_stillness_valid;
    motion.stillness.error = source->simulated_stillness_error;
    if (motion.stillness.state != LUNASHELL::ImuStillnessState::Unavailable) {
        motion.stillness.sampled_at_us = source_time - static_cast<int64_t>(source->simulated_stillness_age_us);
    }
    CODY::SceneContent content{};
    error = CODY::prepare_home_dashboard(static_cast<CODY::CompanionPanel>(panel),
        frame.panel_observation.link, frame.panel_observation.technical,
        CODY::HomeFonts::value_font, CODY::SceneData::body_font,
        source_time, active_time_us, owner, content,
        &motion, &CODY::HomeFonts::small_font, source->device_ip,
        source->device_ip_valid ? CODY::PanelValueState::Current : CODY::PanelValueState::Unavailable,
        static_cast<HostState *>(state)->home_rotation.elapsed_us);
    if (error == ESP_OK) {
        frame.companion_panel.valid = false;
        frame.home_dashboard = &owner;
        frame.home_observation_time_us = source_time;
    }
    return host_result("home_dashboard", error);
}

// handle: Actual native scene whose complete framebuffer has reached the Studio canvas.
// state: Persistent shared policy owner; carousel and audio rejection never reset its latch.
// content_visible: Complete ordinary Home presentation, excluding paused or preempted frames.
// notify: Receives the single expensive-model audio claim, never a playback-success assertion.
// Exception: Returns original source projection or notice-policy errors.
CODY_HOST_EXPORT int cody_host_home_presented(void *handle, void *state, bool content_visible, bool *notify)
{
    if (!handle || !state || !notify) { return host_result("home_presented", ESP_ERR_INVALID_ARG); }
    *notify = false;
    const auto &frame = *static_cast<HostScene *>(handle);
    if (frame.home_dashboard == nullptr) { return ESP_OK; }
    CODY::PanelInputs inputs{};
    esp_err_t error = CODY::project_companion_panel_inputs(frame.panel_observation.link,
        frame.panel_observation.technical, frame.home_observation_time_us, "", inputs);
    if (error == ESP_OK) {
        const bool price_visible = content_visible && frame.home_dashboard->price_visible &&
            frame.home_dashboard->panel == CODY::CompanionPanel::ModelContext;
        error = CODY::advance_model_cost_notice(inputs, price_visible,
            static_cast<HostState *>(state)->model_cost_notice, *notify);
    }
    return host_result("home_presented", error);
}

// handle: Current native frame retaining its title bytes through raster retirement.
// state: Persistent model-marquee owner shared by all three Home pages.
// source: Canonical-shaped PC observation with original freshness and health.
// title: Existing authored Home title, never inferred from model metadata.
// visible_characters: Existing typewriter prefix, unchanged by the shared layout helper.
// active_time_us: Pausable presentation clock independent of source observation ages.
// Exception: Returns real input, source, font and layout failures without fallback text.
CODY_HOST_EXPORT int cody_host_home_identity(void *handle, void *state,
    const HostPanelObservation *source, const char *title, uint16_t visible_characters, int64_t active_time_us)
{
    if (!handle || !state || !source || !title || std::strlen(title) >= 65 ||
        visible_characters > std::strlen(title)) {
        return host_result("home_identity", ESP_ERR_INVALID_ARG);
    }
    auto &frame = *static_cast<HostScene *>(handle);
    auto &owner = static_cast<HostState *>(state)->home_identity;
    esp_err_t error = project_host_panel_observation(source, frame.panel_observation);
    if (error != ESP_OK) { return error; }
    std::strcpy(frame.home_title, title);
    frame.home_frame.title.text = {frame.home_title, static_cast<uint16_t>(std::strlen(title))};
    frame.home_frame.title.color = 0xFD20;
    frame.home_frame.visible_characters = visible_characters;
    CODY::SceneContent content{};
    error = CODY::prepare_home_identity(frame.home_frame, frame.panel_observation.link,
        frame.panel_observation.technical, static_cast<int64_t>(source->receipt_age_us) + 1,
        active_time_us, owner, content);
    if (error == ESP_OK) {
        auto &commands = static_cast<HostState *>(state)->home_commands;
        const auto &link = frame.panel_observation.link;
        const auto &technical = frame.panel_observation.technical;
        CODY::PanelInputs inputs{};
        error = CODY::project_companion_panel_inputs(link, technical,
            static_cast<int64_t>(source->receipt_age_us) + 1, "", inputs);
        if (error != ESP_OK) { return host_result("home_command_source", error); }
        const bool connected = link.data_valid && !link.data_stale &&
            link.state == LUNASHELL::CodexLinkState::Connected;
        const bool urgent = inputs.phase == LUNASHELL::CodexActivityState::Paused ||
            inputs.phase == LUNASHELL::CodexActivityState::Interrupted ||
            inputs.compaction == LUNASHELL::CodexCompactionState::Active;
        bool changed = false;
        error = CODY::update_command_hints(commands, CODY::SceneData::body_font, connected,
            !urgent, link.activity.chat_id, active_time_us, changed);
        if (error == ESP_OK) {
            frame.home_identity = &owner;
            frame.home_commands = &commands;
        }
    }
    return host_result("home_identity", error);
}

// transform: Receives the exact production Home character x/y/scale.
// Exception: Returns invalid argument for absent writable storage.
CODY_HOST_EXPORT int cody_host_home_transform(float *transform)
{
    if (!transform) { return host_result("home_transform", ESP_ERR_INVALID_ARG); }
    transform[0] = CODY::kHomeDashboardCharacterTransform.x;
    transform[1] = CODY::kHomeDashboardCharacterTransform.y;
    transform[2] = CODY::kHomeDashboardCharacterTransform.scale;
    return ESP_OK;
}

// state: Persistent host-only owner; no Python timer or firmware snapshot is involved.
// current: Stable selected Home panel identity.
// active_us: Pausable local display clock, independent of canonical source ages.
// visible: Actual ordinary Home visibility; hidden routes retain their remaining dwell.
// compacting: Current admitted compaction, whose Home arc has priority over rotation.
// next: Receives the shared policy's selected panel.
// Exception: Returns original identity, clock or owner errors without inventing a route.
CODY_HOST_EXPORT int cody_host_home_rotation(void *state, uint8_t current, int64_t active_us,
    bool visible, bool compacting, uint8_t *next)
{
    if (!state || !next) { return host_result("home_rotation", ESP_ERR_INVALID_ARG); }
    CODY::CompanionPanel selected = static_cast<CODY::CompanionPanel>(current);
    auto &owner = *static_cast<HostState *>(state);
    const int64_t dwell = owner.home_dashboard.valid && owner.home_dashboard.panel == selected ?
        owner.home_dashboard.minimum_dwell_us : CODY::kHomeDashboardRotationUs;
    const esp_err_t error = CODY::advance_home_dashboard_rotation(owner.home_rotation,
        selected, active_us, visible, compacting, selected, dwell);
    if (error == ESP_OK) { *next = static_cast<uint8_t>(selected); }
    return host_result("home_rotation", error);
}

// state: Persistent host display owner reset after an accepted manual page gesture.
// Exception: Returns invalid argument for a missing owner.
CODY_HOST_EXPORT int cody_host_home_rotation_reset(void *state)
{
    if (!state) { return host_result("home_rotation_reset", ESP_ERR_INVALID_ARG); }
    CODY::reset_home_dashboard_rotation(static_cast<HostState *>(state)->home_rotation);
    return ESP_OK;
}

// handle: Persistent host recognizer owner, independent of recreated drawing frames.
// x: Original logical acquired touch coordinate.
// y: Original logical acquired touch coordinate.
// pressed: Acquired contact state, including release.
// pressed_edge: Original contact-start edge.
// sampled_at_us: Original local acquisition time, strictly monotonic during contact.
// rotation_degrees: Current logical display orientation.
// eligible: Passive companion content was visible for the acquired contact.
// consumed: Explicit controls or another gesture own this sample.
// accepted: Receives the shared recognizer's accepted passive release.
// Exception: Returns invalid boundary state; never invents a release or navigation.
CODY_HOST_EXPORT int cody_host_panel_tap(void *handle, int16_t x, int16_t y, bool pressed,
    bool pressed_edge, int64_t sampled_at_us, uint16_t rotation_degrees, bool eligible,
    bool consumed, bool *accepted)
{
    if (!handle || !accepted || sampled_at_us <= 0 || rotation_degrees > 270 || rotation_degrees % 90 != 0) {
        return host_result("panel_tap", ESP_ERR_INVALID_ARG);
    }
    *accepted = CODY::observe_companion_tap(static_cast<HostState *>(handle)->companion_tap,
        x, y, pressed, pressed_edge, sampled_at_us, rotation_degrees, eligible, consumed);
    return ESP_OK;
}

// handle: Persistent host recognizer owner.
// Exception: Returns invalid owner; cancellation never fabricates an accepted release.
CODY_HOST_EXPORT int cody_host_panel_cancel_tap(void *handle)
{
    if (!handle) { return host_result("panel_cancel_tap", ESP_ERR_INVALID_ARG); }
    CODY::cancel_companion_tap(static_cast<HostState *>(handle)->companion_tap);
    return ESP_OK;
}

// handle: Persistent host motion recognizer, isolated from physical snapshots.
// x: Explicit SIMULATED acquired X acceleration in m/s2.
// y: Explicit SIMULATED acquired Y acceleration in m/s2.
// z: Explicit SIMULATED acquired Z acceleration in m/s2.
// sampled_at_us: Original local synthetic acquisition timestamp.
// now_us: Observation time used by the same canonical freshness rule.
// active: Whether a visible awake companion view admits the motion.
// accepted: Receives acceptance after shared calm, reversal and debounce checks.
// Exception: Rejects invalid owner, pointers, clocks or nonfinite samples.
CODY_HOST_EXPORT int cody_host_panel_shake(void *handle, float x, float y, float z,
    int64_t sampled_at_us, int64_t now_us, bool active, bool *accepted)
{
    if (!handle || !accepted || sampled_at_us <= 0 || now_us < sampled_at_us ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return host_result("panel_shake", ESP_ERR_INVALID_ARG);
    }
    CODY::MotionSample sample{};
    sample.stamp = {CODY::SampleHealth::Current, CODY::InputOrigin::Simulator, sampled_at_us, ESP_OK};
    sample.acceleration_mps2.values[0] = x;
    sample.acceleration_mps2.values[1] = y;
    sample.acceleration_mps2.values[2] = z;
    *accepted = CODY::observe_companion_shake(static_cast<HostState *>(handle)->companion_shake,
        sample, now_us, active);
    return ESP_OK;
}

// handle: Persistent host motion recognizer owner.
// now_us: Actual local presentation clock at wake or explicit suspension.
// Exception: Rejects invalid owner or clock without synthesizing motion.
CODY_HOST_EXPORT int cody_host_panel_suspend_shake(void *handle, int64_t now_us)
{
    if (!handle || now_us < 0) { return host_result("panel_suspend_shake", ESP_ERR_INVALID_ARG); }
    CODY::suspend_companion_shake(static_cast<HostState *>(handle)->companion_shake, now_us);
    return ESP_OK;
}

// handle: Native rig owner retaining a bounded description independently of Python text.
// text: NUL-terminated bounded ASCII with at least length plus one readable bytes.
// length: Explicit source byte count; zero removes the description layer.
// elapsed_us: Nonnegative active time since this description changed; host frames may be recreated.
// Exception: Returns real metadata, font or layout errors without truncating text.
CODY_HOST_EXPORT int cody_host_activity_description(void *handle, const char *text,
    uint32_t length, int64_t elapsed_us)
{
    if (!handle || !text || length > 192U || text[length] != '\0' ||
        std::memchr(text, '\0', length) != nullptr || elapsed_us < 0) {
        return host_result("activity_description", ESP_ERR_INVALID_ARG);
    }
    auto &state = static_cast<HostScene *>(handle)->activity_description;
    if (length == 0) {
        state = {};
        return ESP_OK;
    }
    esp_err_t error = ESP_OK;
    if (state.length != length || std::memcmp(state.text, text, length) != 0) {
        error = CODY::prepare_activity_description(state, CODY::SceneData::body_font, text, 0);
    }
    bool changed = false;
    if (error == ESP_OK) {
        error = CODY::advance_activity_description(state, CODY::SceneData::body_font, elapsed_us, changed);
    }
    return host_result("activity_description", error);
}

// handle: Native scene owner retaining prepared identity text through rasterization.
// project: NUL-terminated bounded ASCII project; empty clears its line.
// branch: NUL-terminated bounded ASCII selected branch; empty reports no branch metadata.
// source: Original coherent technical observation; native C++ owns model/catalog aging and admission.
// elapsed_us: Active time since this identity changed, independent of recreated host frames.
// attention: Genuine or explicitly SIMULATED attention priority retaining the project.
// operation_elapsed_us: Observed operation interval; negative explicitly means unavailable.
// compacting: Authoritative lifecycle switches the timer to genuine observed compaction elapsed.
// Exception: Returns genuine identity, clock, font or elapsed rendering errors.
CODY_HOST_EXPORT int cody_host_activity_identity(void *handle, const char *project,
    const char *branch, const HostPanelObservation *source, int64_t elapsed_us,
    bool attention, int64_t operation_elapsed_us, bool compacting)
{
    if (!handle || !project || !branch || elapsed_us < 0) {
        return host_result("activity_identity", ESP_ERR_INVALID_ARG);
    }
    auto &owner = *static_cast<HostScene *>(handle);
    auto &state = owner.activity_identity;
    esp_err_t error = project_host_panel_observation(source, owner.panel_observation);
    if (error != ESP_OK) { return error; }
    const auto model_cost = CODY::observed_model_cost(owner.panel_observation.link, owner.panel_observation.technical);
    error = CODY::prepare_activity_identity(state, CODY::SceneData::body_font, project, branch, 0);
    bool changed = false;
    if (error == ESP_OK) {
        error = CODY::set_activity_model_cost(state, CODY::SceneData::body_font,
            project[0] != '\0' ? model_cost.model : "",
            project[0] != '\0' ? model_cost.band : LUNASHELL::CodexCostBand::Unknown, 0, changed);
    }
    if (error == ESP_OK) {
        error = CODY::update_home_tokens(state, CODY::SceneData::body_font,
            owner.panel_observation.link, owner.panel_observation.technical, changed);
    }
    if (error == ESP_OK) {
        // Recreated host frames receive time since the caller's identity/attention transition.
        state.attention_active = attention;
        state.compaction_timer = compacting;
        error = CODY::advance_activity_identity(state, CODY::SceneData::body_font,
            elapsed_us, attention, operation_elapsed_us, changed);
    }
    return host_result("activity_identity", error);
}

// handle: Persistent native state surviving recreated host raster frames.
// phase: Genuine or explicitly SIMULATED typed Codex activity identity.
// chat: Exact bounded lowercase SHA-256 selected chat identity.
// current: Coherent activity freshness and connectivity admission.
// elapsed_us: Monotonic pausable active time, independent of repaint and tool description changes.
// display_phase: Receives the shared admitted subtype; attention/completion are immediate.
// operation_elapsed_us: Receives the genuine observed work interval, or negative unavailable state.
// Exception: Returns invalid boundary inputs or the original observation-policy error.
CODY_HOST_EXPORT int cody_host_activity_observe(void *handle, uint8_t phase, const char *chat,
    bool current, int64_t elapsed_us, uint8_t *display_phase, int64_t *operation_elapsed_us)
{
    if (!handle || !display_phase || !operation_elapsed_us ||
        phase > static_cast<uint8_t>(LUNASHELL::CodexActivityState::Unavailable)) {
        return host_result("activity_observe", ESP_ERR_INVALID_ARG);
    }
    auto &state = static_cast<HostState *>(handle)->activity_observation;
    const esp_err_t error = CODY::advance_activity_observation(state,
        static_cast<LUNASHELL::CodexActivityState>(phase), current, chat, elapsed_us);
    if (error == ESP_OK) {
        *display_phase = static_cast<uint8_t>(state.presented_phase);
        *operation_elapsed_us = state.operation_elapsed_us;
    }
    return host_result("activity_observe", error);
}

// handle: Persistent native activity observation paused by a genuine interaction screen.
// Exception: Returns invalid argument for a missing owner without changing observed timing.
CODY_HOST_EXPORT int cody_host_activity_pause(void *handle)
{
    if (!handle) {
        return host_result("activity_pause", ESP_ERR_INVALID_ARG);
    }
    auto &owner = *static_cast<HostState *>(handle);
    CODY::pause_activity_observation(owner.activity_observation);
    owner.home_commands.sampled_us = -1;
    return ESP_OK;
}

// state: Native transition owner retained across host frames.
// wifi_current: Synthetic validity of the Wi-Fi observation.
// wifi_searching: Synthetic Wi-Fi search state.
// pc_current: Synthetic validity of the PC link observation.
// pc_connected: Synthetic PC link state.
// pc_searching: Synthetic PC discovery state.
// delta_us: Actual active time; paused frames supply zero.
// wifi_connected: Explicit synthetic current LAN connection, independent of sample validity.
// search_view_visible: Explicit synthetic discovery-screen visibility.
// cue: Receives the shared model's cue identity on success.
// Exception: Returns the actual model validation error.
CODY_HOST_EXPORT int cody_host_connection(void *state, bool wifi_current, bool wifi_searching,
    bool pc_current, bool pc_connected, bool pc_searching, int64_t delta_us,
    bool wifi_connected, bool search_view_visible, uint8_t *cue)
{
    if (!state || !cue) { return host_result("connection", ESP_ERR_INVALID_ARG); }
    CODY::PersonalityCue result{};
    const esp_err_t error = CODY::advance_connection_personality(
        {wifi_current, wifi_searching, pc_current, pc_connected, pc_searching, wifi_connected, search_view_visible}, delta_us,
        static_cast<HostState *>(state)->connection, result);
    if (error == ESP_OK) { *cue = static_cast<uint8_t>(result); }
    return host_result("advance_connection_personality", error);
}

// state: Native owner retaining only explicitly synthetic connection observations.
// channels: Receives the shared authored search expression when the cycle is active.
// count: Exact host facial-channel capacity.
// active: Receives whether the visible search narrative currently owns expression selection.
// Exception: Returns the original pointer, capacity or expression contract failure.
CODY_HOST_EXPORT int cody_host_connection_face(void *state, float *channels, uint32_t count, bool *active)
{
    if (!state || !channels || !active || count != std::size(CODY::Channels{}.values)) {
        return host_result("connection_face", ESP_ERR_INVALID_ARG);
    }
    const auto &connection = static_cast<HostState *>(state)->connection;
    *active = connection.search_active;
    if (!*active) {
        return ESP_OK;
    }
    CODY::Channels face{};
    const esp_err_t error = CODY::expression_channels(
        CODY::connection_search_presentation(connection).expression, face);
    if (error == ESP_OK) {
        std::copy(std::begin(face.values), std::end(face.values), channels);
    }
    return host_result("connection_face", error);
}

// scene: Stable authored route ID rather than table index.
// connection: Checked native connectivity cue identity.
// output: Receives the real shared resolver outcome after boundary validation.
// Exception: Returns invalid identity or original scene catalog error.
CODY_HOST_EXPORT int cody_host_scene_cue(uint16_t scene, uint8_t connection, uint8_t *output)
{
    const auto cue = static_cast<CODY::PersonalityCue>(connection);
    if (!output || !CODY::personality_cue_valid(cue)) { return host_result("scene_cue", ESP_ERR_INVALID_ARG); }
    const CODY::SceneDefinition *definition = nullptr;
    const esp_err_t error = CODY::find_scene(static_cast<CODY::SceneId>(scene), definition);
    if (error != ESP_OK) { return host_result("scene_cue", error); }
    *output = static_cast<uint8_t>(CODY::resolve_scene_personality_cue(definition->id, cue));
    return ESP_OK;
}

// id: Exact authored pearl skin ID; activation is session-only on PC.
// Exception: Returns the original catalog selection error, without persistence.
CODY_HOST_EXPORT int cody_host_select_skin(const char *id)
{
    return host_result("select_jelly_skin", CODY::select_jelly_skin(id));
}

// state: Previous independent facial controls.
// target: New requested controls, both in compiled semantic order.
// count: Exact compiled channel count.
// delta_ms: Nonnegative active elapsed time.
// output: Receives the firmware transition result only on success.
// Exception: Returns the real firmware validation error.
CODY_HOST_EXPORT int cody_host_advance_face(const float *state, const float *target,
    uint32_t count, float delta_ms, float *output)
{
    CODY::Channels previous{}, requested{}, next{};
    if (!output) { return host_result("advance_face", ESP_ERR_INVALID_ARG); }
    esp_err_t error = read_channels(state, count, previous);
    if (error == ESP_OK) { error = read_channels(target, count, requested); }
    if (error == ESP_OK) { error = CODY::advance_face(previous, requested, delta_ms, next); }
    if (error == ESP_OK) { std::copy(next.values, next.values + count, output); }
    return host_result("advance_face", error);
}

// base: Validated facial controls in compiled semantic order.
// count: Exact compiled facial channel count.
// elapsed_ms: Bounded firmware animation clock.
// output: Receives real firmware breathing/blink controls.
// Exception: Returns the original input or animated-pose error.
CODY_HOST_EXPORT int cody_host_animate(const float *base, uint32_t count,
    uint32_t elapsed_ms, float *output)
{
    CODY::Channels channels{};
    if (!output) { return host_result("animate", ESP_ERR_INVALID_ARG); }
    esp_err_t error = read_channels(base, count, channels);
    if (error == ESP_OK) { error = CODY::validate_rig_pose(channels, {}); }
    if (error != ESP_OK) { return host_result("animate", error); }
    const auto animated = CODY::animate_rig(channels, elapsed_ms);
    error = CODY::validate_rig_pose(animated, {});
    if (error == ESP_OK) { std::copy(animated.values, animated.values + count, output); }
    return host_result("animate", error);
}

// state: Five floats containing four controls and integrated phase.
// target: Four requested contour controls in compiled semantic order.
// delta_ms: Nonnegative active elapsed time.
// output: Receives five floats only after a successful native transition.
// Exception: Returns the original contour validation error.
CODY_HOST_EXPORT int cody_host_advance_contour(const float *state, const float *target,
    float delta_ms, float *output)
{
    if (!state || !target || !output) { return host_result("advance_contour", ESP_ERR_INVALID_ARG); }
    CODY::ContourState previous{}, next{};
    CODY::ContourControls requested{};
    std::copy(state, state + CODY::kContourControlCount, previous.controls.values);
    previous.phase = state[CODY::kContourControlCount];
    std::copy(target, target + CODY::kContourControlCount, requested.values);
    const esp_err_t error = CODY::advance_contour(previous, requested, delta_ms, next);
    if (error == ESP_OK) {
        std::copy(next.controls.values, next.controls.values + CODY::kContourControlCount, output);
        output[CODY::kContourControlCount] = next.phase;
    }
    return host_result("advance_contour", error);
}

// name: Exact authored scene name, independent of declaration order.
// identity: Receives stable native SceneId only on a unique match.
// Exception: Returns invalid argument or missing/ambiguous identity.
CODY_HOST_EXPORT int cody_host_scene_id(const char *name, uint16_t *identity)
{
    if (!name || !identity) { return host_result("scene_id", ESP_ERR_INVALID_ARG); }
    uint16_t matches = 0;
    CODY::SceneId found{};
    for (const auto &definition : CODY::SceneData::definitions) {
        if (std::strcmp(name, definition.name) == 0) { found = definition.id; ++matches; }
    }
    if (matches != 1) { return host_result("scene_id", ESP_ERR_NOT_FOUND); }
    *identity = static_cast<uint16_t>(found);
    return ESP_OK;
}

// state: Native lag owner retained across rendered frames.
// handle: Evaluated native frame receiving one prepared manga sprite.
// cue: Checked semantic cue supplied by the shared route/connection resolver.
// nominal_scale: Authored character scale before breathing.
// route: Stable scene ID for geometry history ownership.
// rotation: Shared logical cardinal orientation.
// presentation: Native Disabled/Main/Sleep permission.
// delta_us: Active elapsed time; zero freezes the channel.
// paused: Explicit pause state.
// exclusions: Optional flattened half-open rectangles, four uint16 values each.
// exclusion_count: Number of exclusions, at most six including Home's automatically reserved status chrome.
// output: Receives cue, left/top/right/bottom, opacity and presentation status.
// Exception: Returns the actual native geometry/identity error.
CODY_HOST_EXPORT int cody_host_personality(void *state, void *handle, uint8_t cue,
    float nominal_scale, uint32_t route, uint16_t rotation, uint8_t presentation,
    int64_t delta_us, bool paused, const uint16_t *exclusions, uint8_t exclusion_count, uint16_t *output)
{
    if (!state || !handle || !output || exclusion_count > 6 || (exclusion_count && !exclusions)) {
        return host_result("personality", ESP_ERR_INVALID_ARG);
    }
    CODY::DamageRect bounds[6]{};
    for (uint8_t index = 0; index < exclusion_count; ++index) {
        bounds[index] = {exclusions[index * 4], exclusions[index * 4 + 1],
            exclusions[index * 4 + 2], exclusions[index * 4 + 3]};
    }
    const auto &host_scene = *static_cast<HostScene *>(handle);
    const bool dashboard = host_scene.home_dashboard != nullptr;
    if (dashboard) {
        const auto &status = CODY::kStatusOverlayBounds;
        bool status_reserved = false;
        for (uint8_t index = 0; index < exclusion_count; ++index) {
            const auto &bound = bounds[index];
            status_reserved = status_reserved || (bound.left <= status.left && bound.top <= status.top &&
                bound.right >= status.right && bound.bottom >= status.bottom);
        }
        if (!status_reserved) {
            if (exclusion_count == std::size(bounds)) {
                return host_result("personality_status_exclusion", ESP_ERR_INVALID_SIZE);
            }
            bounds[exclusion_count++] = status;
        }
    }
    CODY::CodySize size{};
    esp_err_t error = CODY::cody_size_for_scale(nominal_scale, size);
    if (error != ESP_OK) { return host_result("personality_size", error); }
    CODY::PersonalityContext context{};
    context.presentation = static_cast<CODY::PersonalityPresentation>(presentation);
    context.layout = dashboard ? CODY::PersonalityLayout::HomeDashboard : CODY::PersonalityLayout::Standard;
    context.nominal_scale = nominal_scale;
    context.route = route;
    context.rotation = rotation;
    auto &frame = static_cast<HostScene *>(handle)->rig;
    error = CODY::prepare_personality_frame(frame, static_cast<CODY::PersonalityCue>(cue), size,
        context, delta_us, paused, false, bounds, exclusion_count,
        static_cast<HostState *>(state)->personality, frame.personality);
    if (error == ESP_OK) {
        output[0] = static_cast<uint16_t>(frame.personality.cue);
        output[1] = frame.personality.bounds.left;
        output[2] = frame.personality.bounds.top;
        output[3] = frame.personality.bounds.right;
        output[4] = frame.personality.bounds.bottom;
        output[5] = frame.personality.opacity;
        output[6] = static_cast<uint16_t>(frame.personality.status);
    }
    return host_result("prepare_personality_frame", error);
}

// state: Native personality owner whose presentation is explicitly cancelled.
CODY_HOST_EXPORT void cody_host_personality_clear(void *state)
{
    if (state) { CODY::clear_personality_presentation(static_cast<HostState *>(state)->personality); }
}

// state: PC-only response owner; persisted or canonical physical settings are never modified.
// away: Explicit synthetic preview mode; false restores the authored Desk rhythm.
// Exception: Returns the original invalid-owner error.
CODY_HOST_EXPORT int cody_host_set_away(void *state, bool away)
{
    if (!state) { return host_result("set_away", ESP_ERR_INVALID_ARG); }
    static_cast<HostState *>(state)->away = away;
    return ESP_OK;
}

// state: Host-owned native response owner to initialize after explicit Studio reset.
// seed: Artistic nonzero idle sequence seed supplied by the PC.
// Exception: Returns invalid arguments without modifying the current native model.
CODY_HOST_EXPORT int cody_host_response_reset(void *state, uint32_t seed)
{
    if (!state || seed == 0) { return host_result("response_reset", ESP_ERR_INVALID_ARG); }
    auto response = CODY::make_response_state(CODY::Data::response);
    response.idle_random_state = seed;
    static_cast<HostState *>(state)->response = response;
    return ESP_OK;
}

// state: Host response owner crossing a route or pause boundary.
// Exception: Returns invalid state for a missing owner.
CODY_HOST_EXPORT int cody_host_response_rebase(void *state)
{
    if (!state) { return host_result("response_rebase", ESP_ERR_INVALID_ARG); }
    CODY::rebase_response_motion(static_cast<HostState *>(state)->response);
    return ESP_OK;
}

// state: Native response owner whose inspector data is copied, never computed again.
// output: Receives 21 artistic values followed by six health/error/timestamp triplets.
// capacity: Exactly 39 double slots in the versioned transport.
// Exception: Returns invalid arguments; model state remains untouched.
CODY_HOST_EXPORT int cody_host_response_readout(void *state, double *output, uint32_t capacity)
{
    if (!state || !output || capacity != 39) { return host_result("response_readout", ESP_ERR_INVALID_ARG); }
    const auto &value = static_cast<HostState *>(state)->response;
    const double fields[] = {value.stress, value.fatigue, value.fatigue_target, double(value.fatigue_known),
        value.attention, value.speaking, value.idle_seconds, double(value.idle_scene), value.idle_scene_weight,
        double(value.idle_scene_generation), value.annoyance, value.protective_eye_closure,
        double(value.consumed_eye_tap_at_us), value.breath_phase, value.breath_rate_hz,
        value.blink_elapsed_ms, value.yawn_seconds, value.yawn_cooldown_seconds, value.yawn_weight,
        double(value.yawning), double(value.idle_random_state)};
    std::copy(std::begin(fields), std::end(fields), output);
    const CODY::SampleStamp stamps[] = {value.motion_status, value.power_status, value.microphone_status,
        value.playback_status, value.eye_tap_status, value.body_touch_status};
    for (uint32_t index = 0; index < std::size(stamps); ++index) {
        output[21 + index * 3] = static_cast<uint8_t>(stamps[index].health);
        output[22 + index * 3] = stamps[index].error;
        output[23 + index * 3] = double(stamps[index].sampled_at_us);
    }
    return ESP_OK;
}

// state: Native response owner whose original input timestamps are copied exactly.
// output: Receives consumed-eye time then motion/power/microphone/playback/eye/body timestamps.
// capacity: Exact seven int64 transport slots.
// Exception: Returns invalid arguments without publishing substituted timestamps.
CODY_HOST_EXPORT int cody_host_response_timestamps(void *state, int64_t *output, uint32_t capacity)
{
    if (!state || !output || capacity != 7) { return host_result("response_timestamps", ESP_ERR_INVALID_ARG); }
    const auto &value = static_cast<HostState *>(state)->response;
    const int64_t timestamps[] = {value.consumed_eye_tap_at_us, value.motion_status.sampled_at_us,
        value.power_status.sampled_at_us, value.microphone_status.sampled_at_us, value.playback_status.sampled_at_us,
        value.eye_tap_status.sampled_at_us, value.body_touch_status.sampled_at_us};
    std::copy(std::begin(timestamps), std::end(timestamps), output);
    return ESP_OK;
}

// state: Native response dynamics owner retained between PC frames.
// wire: Explicitly synthetic driver data, never represented as acquired hardware.
// sensitivity: Explicit Studio override of the generated motion sensitivity.
// delta_ms: Nonnegative active elapsed time; paused frames supply zero.
// now_us: Actual host monotonic observation time for canonical freshness checks.
// animate: Advance firmware breathing/blink while the simulated route is active.
// Exception: Returns original input/config/model validation failures without committing partial state.
CODY_HOST_EXPORT int cody_host_response_advance(void *state, const HostResponseInputs *wire,
    float sensitivity, float delta_ms, int64_t now_us, bool animate)
{
    if (!state || !wire || wire->charging > 1 || wire->playback_active > 1 || wire->pressed > 1 ||
        wire->idle_allowed > 1 || wire->eye > 2) { return host_result("response_inputs", ESP_ERR_INVALID_ARG); }
    CODY::ResponseInputs inputs{};
    CODY::SampleStamp *stamps[] = {&inputs.motion.stamp, &inputs.power.stamp, &inputs.microphone.stamp,
        &inputs.playback.stamp, &inputs.eye_tap.stamp, &inputs.body_touch.stamp};
    for (uint32_t index = 0; index < std::size(stamps); ++index) {
        if (wire->health[index] < 0 || wire->health[index] > 3) {
            return host_result("response_sample_health", ESP_ERR_INVALID_ARG);
        }
        *stamps[index] = {static_cast<CODY::SampleHealth>(wire->health[index]), CODY::InputOrigin::Simulator,
            wire->timestamps[index], wire->errors[index]};
    }
    std::copy(std::begin(wire->acceleration), std::end(wire->acceleration), inputs.motion.acceleration_mps2.values);
    inputs.power.level_percent = wire->power;
    inputs.power.charging = wire->charging != 0;
    inputs.microphone.channel_count = wire->microphone_count;
    std::copy(std::begin(wire->microphone_mean), std::end(wire->microphone_mean), inputs.microphone.mean_level);
    std::copy(std::begin(wire->microphone_peak), std::end(wire->microphone_peak), inputs.microphone.peak_level);
    inputs.playback.active = wire->playback_active != 0;
    inputs.playback.mean_level = wire->playback_mean;
    inputs.playback.cue_id = wire->playback_cue;
    inputs.rendering_rotation_degrees = wire->rotation;
    inputs.last_interaction_at_us = wire->last_interaction;
    inputs.idle_scene_allowed = wire->idle_allowed != 0;
    inputs.eye_tap.eye = static_cast<CODY::EyeId>(wire->eye);
    inputs.body_touch.pressed = wire->pressed != 0;
    inputs.body_touch.displacement_px = {wire->drag[0], wire->drag[1]};
    inputs.body_touch.rendering_rotation_degrees = wire->touch_rotation;
    inputs.body_touch.gesture_started_at_us = wire->gesture_started;
    auto config = CODY::Data::response;
    config.motion.sensitivity = sensitivity;
    CODY::ContourControls unused_contour{};
    // PC call frames borrow original input facts; only this local presentation
    // projection receives the shared rhythm policy, once per response advance.
    CODY::apply_away_rhythm(static_cast<HostState *>(state)->away, config, unused_contour, inputs);
    auto &response = static_cast<HostState *>(state)->response;
    return host_result("advance_response", CODY::advance_response(config, response, inputs,
        delta_ms, now_us, animate, response));
}

// state: Already advanced native response owner.
// values: Independent authored facial controls.
// count: Exact native facial channel count.
// contour: Four independent authored contour targets.
// placement: Logical resting x/y/scale.
// rotation: Shared cardinal rendering and touch orientation.
// animate: Whether native breathing/blink contributions are enabled.
// output: Receives face, contour, transform, roots, spring velocity and depth.
// capacity: Exact output float count for this compiled channel catalog.
// Exception: Returns original native composition or boundary-capacity error.
CODY_HOST_EXPORT int cody_host_response_compose(void *state, const float *values, uint32_t count,
    const float *contour, const float *placement, uint16_t rotation, bool animate, float *output, uint32_t capacity)
{
    if (!state || !contour || !placement || !output || capacity != CODY::kFaceChannelCount + 14U) {
        return host_result("response_compose", ESP_ERR_INVALID_ARG);
    }
    CODY::Channels channels{};
    esp_err_t error = read_channels(values, count, channels);
    if (error != ESP_OK) { return error; }
    CODY::ContourControls targets{};
    std::copy(contour, contour + CODY::kContourControlCount, targets.values);
    auto config = CODY::Data::response;
    CODY::ResponseInputs unused_inputs{};
    // Compose from unmodified authored targets, applying the shared reduction
    // once rather than repeatedly scaling the previous displayed contour.
    CODY::apply_away_rhythm(static_cast<HostState *>(state)->away, config, targets, unused_inputs);
    CODY::ResponseFrame frame{};
    error = CODY::compose_response(config, static_cast<HostState *>(state)->response,
        channels, targets, {placement[0], placement[1], placement[2]}, rotation, animate, frame);
    if (error != ESP_OK) { return host_result("compose_response", error); }
    std::copy(frame.face.values, frame.face.values + count, output);
    std::copy(frame.contour.values, frame.contour.values + CODY::kContourControlCount, output + count);
    const float geometry[] = {frame.transform.x, frame.transform.y, frame.transform.scale,
        frame.resting_root.x, frame.resting_root.y, frame.current_root.x, frame.current_root.y,
        frame.spring_velocity_px_per_second.x, frame.spring_velocity_px_per_second.y, frame.depth_scale};
    std::copy(std::begin(geometry), std::end(geometry), output + count + CODY::kContourControlCount);
    return ESP_OK;
}

// handle: Exactly the displayed native geometry shared with rasterization.
// x: Logical touch x after display-driver rotation mapping.
// y: Logical touch y after display-driver rotation mapping.
// radius: Authored normalized eye hit radius.
// eye: Receives native semantic eye identity on success.
// body: Receives native polygon hit classification on success.
// Exception: Returns original native hit-test errors without recomputing another pose.
CODY_HOST_EXPORT int cody_host_hit(void *handle, float x, float y, float radius, uint8_t *eye, bool *body)
{
    if (!handle || !eye || !body) { return host_result("hit", ESP_ERR_INVALID_ARG); }
    const auto &scene = static_cast<HostScene *>(handle)->rig;
    CODY::EyeId identity{};
    esp_err_t error = CODY::hit_test_eye(scene, {x, y}, radius, identity);
    if (error == ESP_OK) { error = CODY::hit_test_body(scene, {x, y}, *body); }
    if (error == ESP_OK) { *eye = static_cast<uint8_t>(identity); }
    return host_result("hit_test", error);
}

// pixels: Existing logical RGB565 framebuffer owned by the PC driver.
// capacity: Exact logical pixel count.
// text: Borrowed bounded printable ASCII, retained until all rows finish.
// length: Explicit string extent; no unbounded walk is required.
// top: Logical first row of the centered line.
// color: Native RGB565 ink.
// body: Select the actual shared caption font instead of the title font.
// visible: Complete visible prefix count, bounded by length.
// center: Explicit logical horizontal center of the reserved line slot.
// max_width: Reserved line width, bounded by the circular canvas contract.
// Exception: Returns the original text preparation/draw error or rejected circle bounds.
CODY_HOST_EXPORT int cody_host_text(uint16_t *pixels, uint32_t capacity, const char *text,
    uint16_t length, uint16_t top, uint16_t color, bool body, uint16_t visible, uint16_t center, uint16_t max_width)
{
    if (!pixels || capacity != 466U * 466U || !text || visible > length || max_width == 0 || max_width > 320) {
        return host_result("text", ESP_ERR_INVALID_ARG);
    }
    if (length == 0) { return ESP_OK; }
    const auto &font = body ? CODY::SceneData::body_font : CODY::SceneData::title_font;
    const int32_t width = (length - 1) * font.advance + font.width;
    const int32_t left = center - (width + 1) / 2;
    if (width > max_width || left < 0 || left + width > 466 || top + font.height > 466) {
        return host_result("text_bounds", ESP_ERR_INVALID_SIZE);
    }
    for (int32_t x : {left, left + width - 1}) {
        for (int32_t y : {int32_t(top), int32_t(top + font.height - 1)}) {
            if ((x - 233) * (x - 233) + (y - 233) * (y - 233) > 233 * 233) {
                return host_result("text_chord", ESP_ERR_INVALID_SIZE);
            }
        }
    }
    CODY2D::TextLine line{};
    esp_err_t error = CODY2D::prepare_text_line(font, {text, length},
        {static_cast<int16_t>(left), static_cast<int16_t>(top)}, color, line);
    if (error != ESP_OK) { return host_result("prepare_text_line", error); }
    CODY2D::Surface canvas{pixels, 466, 466, 466, capacity};
    return host_result("draw_text_rows", CODY2D::draw_text_rows(line, visible, canvas, 0, 0));
}

// identity: Stable shared scene ID.
// elapsed_us: Nonnegative active scene time; no independent host typewriter exists.
// length: Counted content line length.
// preceding: Prior characters in the same sequential reveal.
// output: Receives x/y/scale, boot-visible, opacity, content-visible, title-prefix and content-prefix.
// Exception: Returns original native scene/timing validation errors.
CODY_HOST_EXPORT int cody_host_scene_timing(uint16_t identity, int64_t elapsed_us,
    uint16_t length, uint16_t preceding, float *output)
{
    if (!output || elapsed_us < 0) { return host_result("scene_timing", ESP_ERR_INVALID_ARG); }
    const CODY::SceneDefinition *definition = nullptr;
    esp_err_t error = CODY::find_scene(static_cast<CODY::SceneId>(identity), definition);
    CODY::SceneFrame frame{};
    if (error == ESP_OK) { error = CODY::begin_scene(*definition, frame); }
    if (error == ESP_OK) { error = CODY::advance_scene(frame, elapsed_us); }
    CODY::ScenePresentation presentation{};
    CODY::Transform transform{};
    if (error == ESP_OK) { error = CODY::prepare_scene_presentation(frame, presentation); }
    if (error == ESP_OK) { error = CODY::scene_cloud_transform(frame, transform); }
    if (error != ESP_OK) { return host_result("scene_timing", error); }
    const float values[] = {transform.x, transform.y, transform.scale, float(presentation.boot_bitmap_visible),
        float(presentation.opacity), float(presentation.content_visible), float(frame.visible_characters),
        float(CODY::scene_content_characters(frame, length, preceding))};
    std::copy(std::begin(values), std::end(values), output);
    return ESP_OK;
}

// pixels: Existing logical PC RGB565 framebuffer.
// capacity: Exact logical pixel count.
// bounds: Four uint16 driver values containing x/y/width/height.
// colors: Four uint16 driver values containing top/bottom/border/icon ink.
// icon: Checked native semantic action icon identity.
// icon_x: Driver-selected logical icon center x.
// icon_y: Driver-selected logical icon center y.
// radius: Authored rounded-card radius, or zero for a complete capsule.
// enabled: Explicit action admission state.
// Exception: Returns original shared control validation/raster error.
CODY_HOST_EXPORT int cody_host_capsule(uint16_t *pixels, uint32_t capacity, const uint16_t *bounds,
    const uint16_t *colors, uint8_t icon, int16_t icon_x, int16_t icon_y, uint16_t radius, bool enabled)
{
    if (!pixels || capacity != 466U * 466U || !bounds || !colors || icon > static_cast<uint8_t>(CODY2D::ActionIcon::Orientation)) {
        return host_result("capsule", ESP_ERR_INVALID_ARG);
    }
    CODY2D::CapsuleControl control{};
    control.bounds = {bounds[0], bounds[1], bounds[2], bounds[3]};
    control.top_color = colors[0];
    control.bottom_color = colors[1];
    control.border_color = colors[2];
    control.icon_color = colors[3];
    control.icon = static_cast<CODY2D::ActionIcon>(icon);
    control.icon_center = {icon_x, icon_y};
    control.corner_radius = radius;
    control.enabled = enabled;
    for (int32_t x : {int32_t(bounds[0]), int32_t(bounds[0]) + bounds[2] - 1}) {
        for (int32_t y : {int32_t(bounds[1]), int32_t(bounds[1]) + bounds[3] - 1}) {
            if ((x - 233) * (x - 233) + (y - 233) * (y - 233) > 233 * 233) {
                return host_result("capsule_chord", ESP_ERR_INVALID_SIZE);
            }
        }
    }
    CODY2D::Surface canvas{pixels, 466, 466, 466, capacity};
    return host_result("draw_capsule_rows", CODY2D::draw_capsule_rows(control, canvas, 0, 0));
}

// values: Actual composed native face values copied through the typed host boundary.
// count: Exact compiled channel catalog size, rejected rather than repaired.
// first_channel: Absolute first row in the current real catalog.
// pixels: Native RGB565 output or existing native character pixels for a Back-only pass.
// capacity: Exact writable 466x466 pixel count.
// list_visible: Draw the shared C++ channel list on a clean dark background when true.
// Exception: Returns original native layout/raster errors, never Python substitute geometry.
CODY_HOST_EXPORT int cody_host_debug_channels(const float *values, uint32_t count,
    uint8_t first_channel, uint16_t *pixels, uint32_t capacity, bool list_visible)
{
    if (!pixels || capacity != 466U * 466U) {
        return host_result("debug_channels", ESP_ERR_INVALID_ARG);
    }
    CODY::DebugChannelList list;
    CODY::Channels channels;
    esp_err_t error = ESP_OK;
    if (list_visible) {
        error = read_channels(values, count, channels);
        if (error == ESP_OK) {
            error = CODY::prepare_debug_channel_list(channels, first_channel, CODY::SceneData::body_font, list);
        }
    }
    for (uint16_t first = 0; error == ESP_OK && first < 466; first += 16) {
        const uint16_t height = std::min<uint16_t>(16, 466 - first);
        CODY2D::Surface strip{pixels + first * 466U, 466, height, 466, uint32_t(height) * 466U};
        if (list_visible) {
            error = CODY2D::clear(strip, 0);
            if (error == ESP_OK) {
                error = CODY::draw_debug_channel_list_rows(list, strip, first, 0);
            }
        }
        if (error == ESP_OK) {
            error = CODY::draw_debug_view_back_rows(strip, first, 0);
        }
    }
    return host_result("debug_channels", error);
}

// requested: Typed baseline manga cue after synthetic input classification.
// sleeping: Explicit semantic Sleep permission, never inferred from eyelid geometry.
// affection: Whether an accepted host caress still has active lifetime.
// eye_reaction: None or the current typed pointer-derived eye reaction.
// desired: Receives the actual firmware selector outcome on success.
// release_affection: Receives the actual selector's cancellation decision on success.
// Exception: Returns original cue/selection argument errors.
CODY_HOST_EXPORT int cody_host_select_target(uint8_t requested, bool sleeping, bool affection,
    uint8_t eye_reaction, uint8_t *desired, bool *release_affection)
{
    if (!desired || !release_affection) { return host_result("select_target", ESP_ERR_INVALID_ARG); }
    CODY::PersonalityCue result{};
    const esp_err_t error = CODY::select_personality_target(static_cast<CODY::PersonalityCue>(requested),
        sleeping, affection, result, *release_affection, static_cast<CODY::PersonalityCue>(eye_reaction));
    if (error == ESP_OK) { *desired = static_cast<uint8_t>(result); }
    return host_result("select_personality_target", error);
}

// pixels: Borrowed PC-owned logical RGB565 framebuffer.
// capacity: Exact full-canvas pixel count.
// opacity: Explicit scene intro opacity, shared with its native presentation state.
// Exception: Returns the actual shared surface/fade error.
CODY_HOST_EXPORT int cody_host_fade(uint16_t *pixels, uint32_t capacity, uint8_t opacity)
{
    if (!pixels || capacity != 466U * 466U) { return host_result("fade", ESP_ERR_INVALID_ARG); }
    CODY2D::Surface canvas{pixels, 466, 466, 466, capacity};
    return host_result("fade_to_black", CODY2D::fade_to_black(canvas, opacity));
}
