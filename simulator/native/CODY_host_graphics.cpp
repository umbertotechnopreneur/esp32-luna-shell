// Host transport only: geometry, animation and RGB565 rasterization stay in firmware C++.
#include "CODY_rig.hpp"
#include "CODY_connection_personality.hpp"
#include "CODY_jelly.hpp"
#include "CODY_scene.hpp"
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
};
struct HostState {
    CODY::PersonalityLag personality{};
    CODY::ConnectionPersonalityState connection{};
    CODY::ResponseState response = CODY::make_response_state(CODY::Data::response);
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
CODY_HOST_EXPORT uint32_t cody_host_abi() { return 1; }
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
        const esp_err_t error = CODY::draw_rig_rows(frame.rig, strip, first, debug, selected,
            nullptr, paused, 0, debug ? &frame.debug : nullptr,
            navigation_visible, navigation_opacity, background);
        if (error != ESP_OK) { return host_result("draw_rig_rows", error); }
    }
    return ESP_OK;
}

// state: Native transition owner retained across host frames.
// wifi_current: Synthetic validity of the Wi-Fi observation.
// wifi_searching: Synthetic Wi-Fi search state.
// pc_current: Synthetic validity of the PC link observation.
// pc_connected: Synthetic PC link state.
// pc_searching: Synthetic PC discovery state.
// delta_us: Actual active time; paused frames supply zero.
// cue: Receives the shared model's cue identity on success.
// Exception: Returns the actual model validation error.
CODY_HOST_EXPORT int cody_host_connection(void *state, bool wifi_current, bool wifi_searching,
    bool pc_current, bool pc_connected, bool pc_searching, int64_t delta_us, uint8_t *cue)
{
    if (!state || !cue) { return host_result("connection", ESP_ERR_INVALID_ARG); }
    CODY::PersonalityCue result{};
    const esp_err_t error = CODY::advance_connection_personality(
        {wifi_current, wifi_searching, pc_current, pc_connected, pc_searching}, delta_us,
        static_cast<HostState *>(state)->connection, result);
    if (error == ESP_OK) { *cue = static_cast<uint8_t>(result); }
    return host_result("advance_connection_personality", error);
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
// exclusion_count: Number of exclusions, at most six.
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
    CODY::CodySize size{};
    esp_err_t error = CODY::cody_size_for_scale(nominal_scale, size);
    if (error != ESP_OK) { return host_result("personality_size", error); }
    CODY::PersonalityContext context{};
    context.presentation = static_cast<CODY::PersonalityPresentation>(presentation);
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
    CODY::ResponseFrame frame{};
    error = CODY::compose_response(CODY::Data::response, static_cast<HostState *>(state)->response,
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
    if (!pixels || capacity != 466U * 466U || !bounds || !colors || icon > static_cast<uint8_t>(CODY2D::ActionIcon::Retry)) {
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
