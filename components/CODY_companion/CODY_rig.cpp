#include "CODY_rig.hpp"

#include <cmath>
#include <algorithm>
#include <utility>
#include <iterator>
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_rig";
constexpr float kTau = 6.2831853071795864769f;
static_assert(std::size(Data::points) <= kMaxPoints);
static_assert(std::size(Data::paths) <= kMaxPaths);
static_assert(std::size(Data::curves) * kSubdivisions + std::size(Data::paths) <= kMaxVertices);
static_assert(std::size(Data::contour_lobes) > 0 && std::size(Data::contour_lobes) <= 21);
static_assert(std::size(Data::point_contour_lobe) == std::size(Data::points));
static_assert(std::size(Data::contour_min) == kContourControlCount);
static_assert(std::size(Data::contour_max) == kContourControlCount);
static_assert(Data::contour_transition_seconds >= .05f && Data::contour_transition_seconds <= 5.f);
static_assert(Data::face_transition_seconds >= .05f && Data::face_transition_seconds <= 5.f);
static_assert(std::size(Data::point_face_transforms) == std::size(Data::points));
static_assert(std::size(Data::channel_min) == std::size(Channels{}.values));
static_assert(std::size(Data::channel_max) == std::size(Channels{}.values));

// channels: Independent facial values, including shape and cheek opacity controls.
// operation: Identifies the caller that rejected a channel without clamping it.
esp_err_t validate_face_channels(const Channels &channels, const char *operation)
{
    for (uint16_t index = 0; index < std::size(channels.values); ++index) {
        const float value = channels.values[index];
        if (!std::isfinite(value) || value < Data::channel_min[index] || value > Data::channel_max[index]) {
            ESP_LOGE(kTag, "%s: channel %s rejected: %f (%s)", operation, Data::channel_names[index], value,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

// controls: Independent contour controls to check without clamping.
// operation: Identifies which caller rejected the value in the component log.
esp_err_t validate_contour_controls(const ContourControls &controls, const char *operation)
{
    for (uint16_t index = 0; index < std::size(controls.values); ++index) {
        const float value = controls.values[index];
        if (!std::isfinite(value) || value < Data::contour_min[index] || value > Data::contour_max[index]) {
            ESP_LOGE(kTag, "%s: contour %s rejected: %f (%s)", operation, Data::contour_names[index],
                     value, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

// state: Shape controls and integrated phase that must be finite and in range.
// operation: Identifies the failing evaluation or animation operation.
esp_err_t validate_contour_state(const ContourState &state, const char *operation)
{
    const esp_err_t controls_error = validate_contour_controls(state.controls, operation);
    if (controls_error != ESP_OK) {
        return controls_error;
    }
    if (!std::isfinite(state.phase) || state.phase < 0 || state.phase >= kTau) {
        ESP_LOGE(kTag, "%s: contour phase rejected: %f (%s)", operation, state.phase,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// control: Four already evaluated control points.
// t: Cubic parameter between zero and one.
CODY2D::Vec2 cubic(const CODY2D::Vec2 (&control)[4], float t)
{
    const float remaining = 1 - t;
    const float start_weight = remaining * remaining * remaining;
    const float outgoing_weight = 3 * remaining * remaining * t;
    const float incoming_weight = 3 * remaining * t * t;
    const float end_weight = t * t * t;
    const auto &start = control[to_index(CurvePoint::Start)];
    const auto &outgoing = control[to_index(CurvePoint::OutgoingHandle)];
    const auto &incoming = control[to_index(CurvePoint::IncomingHandle)];
    const auto &end = control[to_index(CurvePoint::End)];
    // Four ordinary weights, so the curve formula stays easy to inspect.
    return {
        start_weight * start.x + outgoing_weight * outgoing.x + incoming_weight * incoming.x + end_weight * end.x,
        start_weight * start.y + outgoing_weight * outgoing.y + incoming_weight * incoming.y + end_weight * end.y
    };
}

// scene: Completed geometry shared with the artwork renderer.
// strip: Destination tile receiving skeleton links and point markers.
// first_row: Logical tile origin before the display owner's rotation.
// selected: Stable point ID to highlight, or kNoPoint.
// profile: Optional frame-wide accumulator using the caller's execution clock.
// stage_started_us: Current substage start; advanced after links and tangents.
esp_err_t draw_debug_rows(const RigScene &scene, CODY2D::Surface strip,
                          uint16_t first_row, PointId selected, RigRasterProfile *profile,
                          int64_t &stage_started_us)
{
    esp_err_t result = ESP_OK;
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const auto &point = Data::points[index];
        if (point.role == PointRole::Anchor && point.parent != PointId::None) {
            result = CODY2D::stroke_rows(strip, first_row, scene.world[to_index(point.parent)], scene.world[index], Data::debug_link_width, Data::palette[to_index(Data::debug_link)]);
            if (result != ESP_OK) {
                return result;
            }
        }
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->skeleton_links_us += now_us - stage_started_us;
        stage_started_us = now_us;
    }
    for (const auto &curve : Data::curves) {
        // Each tangent joins its own anchor and handle. No mysterious pair offsets.
        for (const auto &pair : {
                std::pair{CurvePoint::Start, CurvePoint::OutgoingHandle},
                std::pair{CurvePoint::End, CurvePoint::IncomingHandle}}) {
            const PointId anchor = curve.points[to_index(pair.first)];
            const PointId handle = curve.points[to_index(pair.second)];
            result = CODY2D::stroke_rows(
                strip, first_row, scene.world[to_index(anchor)], scene.world[to_index(handle)],
                Data::debug_handle_width, Data::palette[to_index(Data::debug_handle)]);
            if (result != ESP_OK) {
                return result;
            }
        }
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->skeleton_tangents_us += now_us - stage_started_us;
        stage_started_us = now_us;
    }
    // The caller's existing skeleton-end tick also closes the marker stage.
    // Only two extra clock reads per tile are needed for this breakdown.
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const auto &point = Data::points[index];
        const bool is_selected = point.id == selected;
        const bool is_handle = point.role == PointRole::Handle;
        ColorId marker_color = Data::debug_knot;
        if (is_selected) {
            marker_color = Data::debug_selected;
        } else if (is_handle) {
            marker_color = Data::debug_handle;
        } else if (point.role == PointRole::Anchor) {
            marker_color = Data::debug_anchor;
        }
        float diameter = Data::debug_anchor_diameter;
        if (is_selected) {
            diameter = Data::debug_selected_diameter;
        } else if (is_handle) {
            diameter = Data::debug_handle_diameter;
        }
        result = CODY2D::stroke_rows(strip, first_row, scene.world[index], scene.world[index],
                                    diameter, Data::palette[to_index(marker_color)]);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

struct NavigationControl {
    RigControl identity;
    CODY2D::Vec2 center;
};

// One geometry table owns both the drawn circles and their full touch targets.
constexpr NavigationControl kNavigationControls[] = {
    {RigControl::Back, {40, 233}},
    {RigControl::Overlay, {426, 233}},
    {RigControl::Pause, {233, 40}},
};
constexpr float kControlRadius = 30;

// strip: Destination tile receiving the controls outside the central safe square.
// first_row: Logical tile origin in the canonical circular canvas.
// debug: Current overlay state shown by the toggle icon.
// paused: Show Play while geometry is frozen, otherwise show Pause.
esp_err_t draw_navigation_rows(CODY2D::Surface strip, uint16_t first_row, bool debug, bool paused)
{
    esp_err_t result = ESP_OK;
    for (const auto &control : kNavigationControls) {
        const int first = std::max(0, static_cast<int>(control.center.y - kControlRadius) - first_row);
        const int last = std::min(static_cast<int>(strip.height),
            static_cast<int>(control.center.y + kControlRadius) - first_row);
        for (int row = first; row < last; ++row) {
            const float dy = first_row + row + .5f - control.center.y;
            // Xtensa software division belongs outside the covered-pixel loop.
            const int first_color = to_index(Data::back_top);
            const int color_span = to_index(Data::back_bottom) - first_color;
            const int slot = first_color + static_cast<int>((dy + kControlRadius) * color_span / 60);
            const uint16_t color = Data::palette[slot];
            for (int x = static_cast<int>(control.center.x - kControlRadius);
                 x < control.center.x + kControlRadius && x < strip.width; ++x) {
                const float dx = x + .5f - control.center.x;
                if (dx * dx + dy * dy <= kControlRadius * kControlRadius) {
                    strip.pixels[row * strip.stride + x] = color;
                }
            }
        }
    }
    // Two bars while running; the paused triangle is the available Resume action.
    const CODY2D::Vec2 pause_segments[][2] = {{{226, 29}, {226, 51}}, {{240, 29}, {240, 51}}};
    const CODY2D::Vec2 play_segments[][2] = {
        {{227, 29}, {243, 40}}, {{243, 40}, {227, 51}}, {{227, 51}, {227, 29}},
    };
    const auto *segments = paused ? play_segments : pause_segments;
    const size_t segment_count = paused ? std::size(play_segments) : std::size(pause_segments);
    for (size_t index = 0; index < segment_count; ++index) {
        result = CODY2D::stroke_rows(strip, first_row, segments[index][0], segments[index][1],
            Data::back_stroke, Data::palette[to_index(Data::back_ink)]);
        if (result != ESP_OK) {
            return result;
        }
    }
    result = CODY2D::stroke_rows(strip, first_row, {44, 222}, {33, 233}, Data::back_stroke, Data::palette[to_index(Data::back_ink)]);
    if (result != ESP_OK) {
        return result;
    }
    result = CODY2D::stroke_rows(strip, first_row, {33, 233}, {44, 244}, Data::back_stroke, Data::palette[to_index(Data::back_ink)]);
    if (result != ESP_OK) {
        return result;
    }
    const CODY2D::Vec2 nodes[] = {{416, 242}, {426, 222}, {436, 242}};
    for (uint16_t index = 0; index < 3; ++index) {
        result = CODY2D::stroke_rows(strip, first_row, nodes[index], nodes[(index + 1) % 3], Data::overlay_stroke, Data::palette[to_index(Data::back_ink)]);
        if (result != ESP_OK) {
            return result;
        }
        result = CODY2D::stroke_rows(strip, first_row, nodes[index], nodes[index], Data::overlay_marker_diameter,
                                   Data::palette[to_index(debug ? Data::debug_selected : Data::back_ink)]);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}
}  // namespace

// point: Logical display coordinate using exactly the rendered controls' geometry.
RigControl rig_control_at(CODY2D::Vec2 point)
{
    for (const auto &control : kNavigationControls) {
        const float dx = point.x - control.center.x;
        const float dy = point.y - control.center.y;
        if (dx * dx + dy * dy <= kControlRadius * kControlRadius) {
            return control.identity;
        }
    }
    return RigControl::None;
}

// Return immutable YAML-authored contour defaults.
ContourControls contour_defaults()
{
    return Data::contour_defaults;
}

// Each explicit route entry starts its own contour clock.
ContourState make_contour_state()
{
    return {Data::contour_defaults, 0};
}

// state: Previous independent shape state, never modified before successful completion.
// target: Requested outline controls, unrelated to facial channels.
// delta_ms: Active elapsed time; zero freezes phase and all shape transitions.
// next: Receives the new state atomically after checks; may alias state.
esp_err_t advance_contour(const ContourState &state, const ContourControls &target,
                          float delta_ms, ContourState &next)
{
    esp_err_t result = validate_contour_state(state, "Contour advance state");
    if (result != ESP_OK) {
        return result;
    }
    result = validate_contour_controls(target, "Contour advance target");
    if (result != ESP_OK) {
        return result;
    }
    if (!std::isfinite(delta_ms) || delta_ms < 0) {
        ESP_LOGE(kTag, "Contour advance time rejected: %fms (%s)", delta_ms,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (delta_ms == 0) {
        next = state;
        return ESP_OK;
    }
    const float seconds = delta_ms / 1000.f;
    const float time_constant = Data::contour_transition_seconds;
    const float ratio = seconds / time_constant;
    const float weight = -std::expm1(-ratio);
    ContourState candidate = state;
    for (uint16_t index = 0; index < std::size(target.values); ++index) {
        if (state.controls.values[index] != target.values[index]) {
            candidate.controls.values[index] =
                state.controls.values[index] * (1 - weight) + target.values[index] * weight;
        }
    }
    // Integrate the smoothed frequency analytically; the series keeps tiny dt stable.
    const float ramp_time = ratio < .001f ?
        seconds * ratio * (.5f - ratio/6 + ratio*ratio/24 - ratio*ratio*ratio/120) :
        seconds - time_constant * weight;
    const float turns = state.controls[ContourControl::Speed] * (seconds - ramp_time) + target[ContourControl::Speed] * ramp_time;
    candidate.phase = std::fmod(state.phase + kTau * turns, kTau);
    // Preserve the canonical periodic boundary after float rounding.
    if (candidate.phase >= kTau) {
        candidate.phase = 0;
    }
    result = validate_contour_state(candidate, "Contour advance result");
    if (result != ESP_OK) {
        return result;
    }
    next = candidate;
    return ESP_OK;
}

// state: Previous unanimated facial channels; caller ownership remains unchanged on failure.
// target: Requested independent face controls, with authored finite bounds.
// delta_ms: Real active elapsed time; zero freezes state without accepting invalid values.
// next: Receives a complete smoothed set only after successful validation; may alias state.
esp_err_t advance_face(const Channels &state, const Channels &target, float delta_ms, Channels &next)
{
    esp_err_t result = validate_face_channels(state, "Face advance state");
    if (result != ESP_OK) {
        return result;
    }
    result = validate_face_channels(target, "Face advance target");
    if (result != ESP_OK) {
        return result;
    }
    if (!std::isfinite(delta_ms) || delta_ms < 0) {
        ESP_LOGE(kTag, "Face advance time rejected: %fms (%s)", delta_ms,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (delta_ms == 0) {
        next = state;
        return ESP_OK;
    }
    const float weight = -std::expm1(-(delta_ms / 1000.f) / Data::face_transition_seconds);
    Channels candidate = state;
    for (uint16_t index = 0; index < std::size(target.values); ++index) {
        if (state.values[index] != target.values[index]) {
            candidate.values[index] = state.values[index] * (1 - weight) + target.values[index] * weight;
        }
    }
    result = validate_face_channels(candidate, "Face advance result");
    if (result != ESP_OK) {
        return result;
    }
    next = candidate;
    return ESP_OK;
}

// base: Smoothed independent controls, validated when the scene is evaluated.
// elapsed_ms: Real monotonic animation time, never a sensor measurement.
Channels animate_rig(const Channels &base, uint32_t elapsed_ms)
{
    Channels result = base;
    const float breath = std::sin(kTau * static_cast<float>(elapsed_ms % 4000U) / 4000.f);
    const uint32_t blink_ms = elapsed_ms % 4800U;
    const float blink = blink_ms < 280U ?
        .5f - .5f * std::cos(kTau * static_cast<float>(blink_ms) / 280.f) : 0;
    result[FaceChannel::EyeLeft] = base[FaceChannel::EyeLeft] * (1 - blink);
    result[FaceChannel::EyeRight] = base[FaceChannel::EyeRight] * (1 - blink);
    result[FaceChannel::MouthOpen] = base[FaceChannel::MouthOpen] + (1 - base[FaceChannel::MouthOpen]) * .08f * (.5f + .5f * breath);
    result[FaceChannel::Breath] = base[FaceChannel::Breath] + (1 - std::abs(base[FaceChannel::Breath])) * breath;
    return result;
}

// channels: Authored independent facial controls; invalid values stop evaluation.
// transform: Logical translation and scale, shared by every feature and debug marker.
// scene: Caller-owned geometry buffer, never retained as valid after failure.
// contour: Optional outline controls; parented handles inherit their own lobe's translation.
esp_err_t evaluate_rig(const Channels &channels, Transform transform, RigScene &scene,
                       const ContourState *contour)
{
    scene.valid = false;
    if (contour != nullptr) {
        const esp_err_t contour_error = validate_contour_state(*contour, "Rig evaluation");
        if (contour_error != ESP_OK) {
            return contour_error;
        }
    }
    const esp_err_t face_error = validate_face_channels(channels, "Rig evaluation");
    if (face_error != ESP_OK) {
        return face_error;
    }
    if (!std::isfinite(transform.x) || !std::isfinite(transform.y) || !std::isfinite(transform.scale) ||
        transform.x < 0 || transform.x >= 466 || transform.y < 0 || transform.y >= 466 ||
        transform.scale < 20 || transform.scale > 200) {
        ESP_LOGE(kTag, "Transform rejected: %f,%f scale=%f (%s)", transform.x, transform.y,
                 transform.scale, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // One breathing scale carries every feature; no wandering eyes.
    scene.scale = transform.scale * (1 + .018f * channels[FaceChannel::Breath]);
    std::array<CODY2D::Vec2, std::size(Data::contour_lobes)> contour_offsets{};
    if (contour != nullptr) {
        for (uint16_t index = 0; index < std::size(Data::contour_lobes); ++index) {
            const auto &lobe = Data::contour_lobes[index];
            const auto &controls = contour->controls;
            const float wave = std::sin(contour->phase - kTau * controls[ContourControl::PhaseSpread] * lobe.phase_turns);
            const float distance = controls[ContourControl::Fullness] * lobe.fullness + controls[ContourControl::Amplitude] * lobe.wave * wave;
            contour_offsets[index] = {lobe.direction_x * distance, lobe.direction_y * distance};
        }
    }
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const auto &definition = Data::points[index];
        const float value = definition.channel == FaceChannel::None ? 0 : channels[definition.channel];
        CODY2D::Vec2 local{definition.x + definition.dx * value, definition.y + definition.dy * value};
        const auto &face_transform = Data::point_face_transforms[index];
        if (face_transform.x_channel != FaceChannel::None) {
            local.x *= channels[face_transform.x_channel];
        }
        if (face_transform.y_channel != FaceChannel::None) {
            local.y *= channels[face_transform.y_channel];
        }
        const ContourLobeId lobe = Data::point_contour_lobe[index];
        if (lobe != ContourLobeId::None) {
            local.x += contour_offsets[to_index(lobe)].x;
            local.y += contour_offsets[to_index(lobe)].y;
        }
        if (definition.parent != PointId::None) {
            local.x += scene.local[to_index(definition.parent)].x;
            local.y += scene.local[to_index(definition.parent)].y;
        }
        scene.local[index] = local;
        scene.world[index] = {transform.x + local.x * scene.scale, transform.y + local.y * scene.scale};
    }
    // Sample each named path once; all display strips reuse this geometry.
    uint16_t vertex = 0;
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        auto &path = scene.paths[index];
        path.first_vertex = vertex;
        path.visible = definition.visible_channel == FaceChannel::None || channels[definition.visible_channel] >= .25f;
        path.opacity = definition.opacity_channel == FaceChannel::None ? 255 : static_cast<uint8_t>(
            std::floor(channels[definition.opacity_channel] * 255.f + .5f));
        for (uint16_t curve_index = 0; curve_index < definition.curve_count; ++curve_index) {
            const auto &curve = Data::curves[to_index(definition.first_curve) + curve_index];
            CODY2D::Vec2 control[4];
            for (uint16_t point_index = 0; point_index < 4; ++point_index) {
                control[point_index] = scene.world[to_index(curve.points[point_index])];
            }
            if (curve_index == 0) {
                scene.vertices[vertex++] = control[to_index(CurvePoint::Start)];
            }
            for (uint16_t step = 1; step <= kSubdivisions; ++step) {
                scene.vertices[vertex++] = cubic(control, static_cast<float>(step) / kSubdivisions);
            }
        }
        path.count = vertex - path.first_vertex;
        const float stroke_width = definition.stroke * scene.scale;
        if (!std::isfinite(stroke_width) || stroke_width < 0 ||
            stroke_width > CODY2D::kMaxStrokeWidth ||
            (definition.stroke == 0 && path.count > CODY2D::kMaxPathVertices)) {
            ESP_LOGE(kTag, "Evaluated path %s rejected: width=%f vertices=%u (%s)",
                     definition.name, stroke_width, path.count, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        const float stroke_margin = stroke_width * .5f;
        path.first_scan_y = scene.vertices[path.first_vertex].y - stroke_margin;
        path.last_scan_y = scene.vertices[path.first_vertex].y + stroke_margin;
        for (uint16_t path_vertex = path.first_vertex; path_vertex < vertex; ++path_vertex) {
            const auto position = scene.vertices[path_vertex];
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                std::abs(position.x) > 2048 || std::abs(position.y) > 2048) {
                ESP_LOGE(kTag, "Evaluated path %s has invalid geometry (%s)", definition.name,
                         esp_err_to_name(ESP_ERR_INVALID_ARG));
                return ESP_ERR_INVALID_ARG;
            }
            path.first_scan_y = std::min(path.first_scan_y, position.y - stroke_margin);
            path.last_scan_y = std::max(path.last_scan_y, position.y + stroke_margin);
        }
    }
    scene.valid = true;
    return ESP_OK;
}

// scene: One completed evaluation used unchanged throughout the frame's strips.
// strip: RGB565 destination owned by the companion worker.
// first_row: Tile y origin in the unrotated logical canvas.
// debug: Enables bones, Bézier tangents and evaluated point markers.
// selected: Point emphasized by the latest real touch selection.
// profile: Optional caller-owned collector with a monotonic execution clock.
// paused: Draw the Resume action without changing caller-owned geometry.
esp_err_t draw_rig_rows(const RigScene &scene, CODY2D::Surface strip,
                        uint16_t first_row, bool debug, PointId selected, RigRasterProfile *profile, bool paused)
{
    if (profile != nullptr && profile->now_us == nullptr) {
        ESP_LOGE(kTag, "Raster profiling clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!scene.valid || (selected != kNoPoint && to_index(selected) >= std::size(Data::points))) {
        ESP_LOGE(kTag, "Draw rejected: valid=%u selected=%u (%s)", scene.valid, static_cast<unsigned>(to_index(selected)),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    int64_t stage_started_us = profile != nullptr ? profile->now_us() : 0;
    esp_err_t result = CODY2D::clear(strip, Data::palette[to_index(Data::background_color)]);
    if (result != ESP_OK) {
        return result;
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->clear_us += now_us - stage_started_us;
        stage_started_us = now_us;
    }
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        const auto &path = scene.paths[index];
        // Bounds belong to the evaluated frame, not to whichever strip draws first.
        const float strip_first_scan_y = first_row + .5f;
        const float strip_last_scan_y = first_row + strip.height - .5f;
        if (!path.visible || (definition.stroke == 0 && path.opacity == 0) ||
            strip_last_scan_y < path.first_scan_y || strip_first_scan_y > path.last_scan_y) {
            continue;
        }
        const auto *vertices = scene.vertices.data() + path.first_vertex;
        if (definition.stroke == 0) {
            result = CODY2D::fill_path_rows(strip, first_row, vertices, path.count,
                                          Data::palette, static_cast<uint8_t>(to_index(definition.first_color)),
                                          static_cast<uint8_t>(to_index(definition.last_color)), path.opacity);
        } else {
            for (uint16_t segment = 1; segment < path.count; ++segment) {
                result = CODY2D::stroke_rows(strip, first_row, vertices[segment-1], vertices[segment],
                                             definition.stroke * scene.scale, Data::palette[to_index(definition.first_color)]);
                if (result != ESP_OK) {
                    return result;
                }
            }
        }
        if (result != ESP_OK) {
            return result;
        }
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->artwork_us += now_us - stage_started_us;
        stage_started_us = now_us;
    }
    const int64_t skeleton_started_us = stage_started_us;
    if (debug) {
        result = draw_debug_rows(scene, strip, first_row, selected, profile, stage_started_us);
        if (result != ESP_OK) {
            return result;
        }
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->skeleton_us += now_us - skeleton_started_us;
        if (debug) {
            profile->skeleton_markers_us += now_us - stage_started_us;
        }
        stage_started_us = now_us;
    }
    result = draw_navigation_rows(strip, first_row, debug, paused);
    if (profile != nullptr) {
        profile->navigation_us += profile->now_us() - stage_started_us;
    }
    return result;
}

// scene: Successfully evaluated geometry, shared unchanged with the renderer.
// debug: Include every bone, tangent and selected marker when the overlay is visible.
// bounds: Receives conservative artwork coverage clipped to the 466-pixel canvas.
esp_err_t rig_damage_bounds(const RigScene &scene, bool debug, DamageRect &bounds)
{
    bounds = {};
    if (!scene.valid) {
        ESP_LOGE(kTag, "Artwork bounds rejected: invalid scene (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    float left = 466.f;
    float top = 466.f;
    float right = 0.f;
    float bottom = 0.f;
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        const auto &path = scene.paths[index];
        if (!path.visible || (definition.stroke == 0 && path.opacity == 0)) {
            continue;
        }
        const float margin = definition.stroke * scene.scale * .5f + 1.f;
        for (uint16_t vertex = 0; vertex < path.count; ++vertex) {
            const auto &point = scene.vertices[path.first_vertex + vertex];
            left = std::min(left, point.x - margin);
            right = std::max(right, point.x + margin);
            top = std::min(top, point.y - margin);
            bottom = std::max(bottom, point.y + margin);
        }
    }
    if (debug) {
        // Straight links and tangents lie between their endpoints. The largest
        // authored marker/line radius also covers selection changes.
        const float margin = std::max({Data::debug_anchor_diameter, Data::debug_handle_diameter,
            Data::debug_selected_diameter, Data::debug_link_width, Data::debug_handle_width}) * .5f + 1.f;
        for (uint16_t index = 0; index < std::size(Data::points); ++index) {
            const auto &point = scene.world[index];
            left = std::min(left, point.x - margin);
            right = std::max(right, point.x + margin);
            top = std::min(top, point.y - margin);
            bottom = std::max(bottom, point.y + margin);
        }
    }
    // Clip before integer conversion. Artwork bounds include raster margins,
    // but static navigation and diagnostic text have separate owners.
    if (left < right && top < bottom && right > 0 && bottom > 0 && left < 466 && top < 466) {
        bounds = {static_cast<uint16_t>(std::floor(std::max(0.f, left))),
            static_cast<uint16_t>(std::floor(std::max(0.f, top))),
            static_cast<uint16_t>(std::ceil(std::min(466.f, right))),
            static_cast<uint16_t>(std::ceil(std::min(466.f, bottom)))};
    }
    return ESP_OK;
}

// No parameters: fixed navigation occupies its own lateral control band.
DamageRect rig_navigation_damage_bounds()
{
    return {0, 10, 466, 263};
}

// scene: Valid evaluated geometry from the latest completed frame.
// point: Current logical input coordinate.
// radius: Positive finite search radius; no selection is returned beyond it.
// selected: Output distinguishes an empty hit from a rejected input via esp_err_t.
esp_err_t nearest_rig_point(const RigScene &scene, CODY2D::Vec2 point, float radius, PointId &selected)
{
    selected = kNoPoint;
    if (!scene.valid || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(radius) || radius <= 0) {
        ESP_LOGE(kTag, "Point selection rejected: valid=%u x=%f y=%f radius=%f", scene.valid, point.x, point.y, radius);
        return ESP_ERR_INVALID_ARG;
    }
    float distance = radius*radius;
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const float dx = point.x - scene.world[index].x;
        const float dy = point.y - scene.world[index].y;
        const float candidate = dx*dx + dy*dy;
        if (candidate <= distance) {
            distance = candidate;
            selected = Data::points[index].id;
        }
    }
    return ESP_OK;
}

// identity: Stable point ID from selection or geometry traversal.
const char *rig_point_name(PointId identity)
{
    const auto storage_index = to_index(identity);
    return storage_index < std::size(Data::points) ? Data::points[storage_index].name : nullptr;
}
}  // namespace CODY
