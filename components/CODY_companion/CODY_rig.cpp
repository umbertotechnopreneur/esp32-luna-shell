#include "CODY_rig.hpp"

#include <cmath>
#include <algorithm>
#include <utility>
#include <iterator>
#include "cloud_rig_data.hpp"
#include "navigation_data.hpp"
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

// No parameters: count authored parent links without turning table position into identity.
constexpr size_t authored_debug_link_count()
{
    size_t count = 0;
    for (const auto &point : Data::points) {
        if (point.role == PointRole::Anchor && point.parent != PointId::None) {
            ++count;
        }
    }
    return count;
}

constexpr size_t kAuthoredDebugLinkCount = authored_debug_link_count();
constexpr size_t kAuthoredDebugTangentCount = std::size(Data::curves) * 2;
constexpr size_t kAuthoredDebugMarkerCount = std::size(Data::points);
static_assert(kAuthoredDebugLinkCount <= kRigDebugLinkCapacity,
              "Rig debug link cache is smaller than the authored rig");
static_assert(kAuthoredDebugTangentCount <= kRigDebugTangentCapacity,
              "Rig debug tangent cache is smaller than the authored rig");
static_assert(kAuthoredDebugMarkerCount <= kMaxPoints,
              "Rig debug marker cache is smaller than the authored rig");

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
// first_column: Logical x origin of the compact destination, never applied to geometry.
// debug_strokes: Optional immutable prepared links, tangents and markers for this scene and selection.
esp_err_t draw_debug_rows(const RigScene &scene, CODY2D::Surface strip,
                          uint16_t first_row, PointId selected, RigRasterProfile *profile,
                          int64_t &stage_started_us, uint16_t first_column,
                          const RigDebugStrokeView *debug_strokes)
{
    esp_err_t result = ESP_OK;
    if (debug_strokes != nullptr) {
        result = CODY2D::stroke_prepared_batch_rows(
            strip, first_row, debug_strokes->links,
            debug_strokes->link_count, first_column
        );
        if (result != ESP_OK) {
            return result;
        }
    } else {
        for (uint16_t index = 0; index < std::size(Data::points); ++index) {
            const auto &point = Data::points[index];
            if (point.role == PointRole::Anchor && point.parent != PointId::None) {
                result = CODY2D::stroke_rows(strip, first_row,
                    scene.world[to_index(point.parent)], scene.world[index],
                    Data::debug_link_width, Data::palette[to_index(Data::debug_link)], first_column);
                if (result != ESP_OK) {
                    return result;
                }
            }
        }
    }
    if (profile != nullptr) {
        const int64_t now_us = profile->now_us();
        profile->skeleton_links_us += now_us - stage_started_us;
        stage_started_us = now_us;
    }
    if (debug_strokes != nullptr) {
        result = CODY2D::stroke_prepared_batch_rows(
            strip, first_row, debug_strokes->tangents,
            debug_strokes->tangent_count, first_column
        );
        if (result != ESP_OK) {
            return result;
        }
    } else {
        for (const auto &curve : Data::curves) {
            // Each tangent joins its own anchor and handle. No mysterious pair offsets.
            for (const auto &pair : {
                    std::pair{CurvePoint::Start, CurvePoint::OutgoingHandle},
                    std::pair{CurvePoint::End, CurvePoint::IncomingHandle}}) {
                const PointId anchor = curve.points[to_index(pair.first)];
                const PointId handle = curve.points[to_index(pair.second)];
                result = CODY2D::stroke_rows(
                    strip, first_row, scene.world[to_index(anchor)], scene.world[to_index(handle)],
                    Data::debug_handle_width, Data::palette[to_index(Data::debug_handle)], first_column);
                if (result != ESP_OK) {
                    return result;
                }
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
    if (debug_strokes != nullptr) {
        result = CODY2D::stroke_prepared_batch_rows(
            strip, first_row, debug_strokes->markers,
            debug_strokes->marker_count, first_column
        );
        if (result != ESP_OK) {
            return result;
        }
    } else {
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
                                        diameter, Data::palette[to_index(marker_color)], first_column);
            if (result != ESP_OK) {
                return result;
            }
        }
    }
    return ESP_OK;
}

// control: Valid generated circle or annular upper-rim segment.
// point: Logical point in the same coordinate basis used by raster and rotation.
bool navigation_contains(const RigControlGeometry &control, CODY2D::Vec2 point)
{
    const float dx = point.x - control.center.x;
    const float dy = point.y - control.center.y;
    const float squared = dx * dx + dy * dy;
    if (control.side == 0) {
        return squared <= control.outer_radius * control.outer_radius;
    }
    const float along = dx * control.side;
    return dy < 0 && squared >= control.inner_radius * control.inner_radius &&
        squared <= control.outer_radius * control.outer_radius &&
        along >= -dy * control.slope_min && along <= -dy * control.slope_max;
}

// strip: Destination tile receiving the controls outside the central safe square.
// first_row: Logical tile origin in the canonical circular canvas.
// debug: Current overlay state shown by the toggle icon.
// paused: Show Play while geometry is frozen, otherwise show Pause.
// first_column: Logical x origin used to clip controls into the compact destination.
// opacity: Debug toolbar visibility; geometry and hit targets never move during fading.
esp_err_t draw_navigation_rows(CODY2D::Surface strip, uint16_t first_row,
                              bool debug, bool paused, uint16_t first_column, uint8_t opacity)
{
    esp_err_t result = ESP_OK;
    if (opacity == 0) {
        return ESP_OK;
    }
    for (const auto &control : NavigationData::controls) {
        if (!control.visible_by_default) {
            continue;
        }
        const int first = std::max<int>(0, control.bounds.top - static_cast<int>(first_row));
        const int last = std::min<int>(strip.height, control.bounds.bottom - static_cast<int>(first_row));
        for (int row = first; row < last; ++row) {
            const float dy = first_row + row + .5f - control.center.y;
            const float outer_squared = control.outer_radius * control.outer_radius - dy * dy;
            if (outer_squared < 0) {
                continue;
            }
            float left = control.center.x - std::sqrt(outer_squared);
            float right = control.center.x + std::sqrt(outer_squared);
            if (control.side != 0) {
                const float near = std::max(-dy * control.slope_min,
                    std::sqrt(std::max(0.f, control.inner_radius * control.inner_radius - dy * dy)));
                const float far = std::min(-dy * control.slope_max, std::sqrt(outer_squared));
                if (far < near) {
                    continue;
                }
                left = control.center.x + (control.side < 0 ? -far : near);
                right = control.center.x + (control.side < 0 ? -near : far);
            }
            const int first_x = std::max<int>(first_column, static_cast<int>(std::ceil(left - .5f)));
            const int last_x = std::min<int>(first_column + strip.width, static_cast<int>(std::floor(right - .5f)) + 1);
            if (last_x <= first_x) {
                continue;
            }
            const int first_color = to_index(Data::back_top);
            const int color_span = to_index(Data::back_bottom) - first_color;
            const int slot = first_color + (first_row + row - control.bounds.top) * color_span /
                (control.bounds.bottom - control.bounds.top);
            result = CODY2D::fill_rect(strip,
                {static_cast<uint16_t>(first_x - first_column), static_cast<uint16_t>(row),
                 static_cast<uint16_t>(last_x - first_x), 1},
                CODY2D::fade_rgb565_to_black(Data::palette[slot], opacity));
            if (result != ESP_OK) {
                return result;
            }
        }
        const auto center = control.icon_center;
        const uint16_t ink = CODY2D::fade_rgb565_to_black(Data::palette[to_index(Data::back_ink)], opacity);
        if (control.identity == RigControl::Overlay) {
            const CODY2D::Vec2 nodes[] = {{center.x-10, center.y+9}, {center.x, center.y-11}, {center.x+10, center.y+9}};
            for (uint16_t index = 0; index < 3; ++index) {
                result = CODY2D::stroke_rows(strip, first_row, nodes[index], nodes[(index+1)%3],
                    Data::overlay_stroke, ink, first_column);
                if (result == ESP_OK) {
                    const uint16_t marker = CODY2D::fade_rgb565_to_black(
                        Data::palette[to_index(debug ? Data::debug_selected : Data::back_ink)], opacity);
                    result = CODY2D::stroke_rows(strip, first_row, nodes[index], nodes[index],
                        Data::overlay_marker_diameter, marker, first_column);
                }
                if (result != ESP_OK) {
                    return result;
                }
            }
        } else {
            const CODY2D::Vec2 bars[][2] = {{{center.x-7,center.y-11},{center.x-7,center.y+11}},
                {{center.x+7,center.y-11},{center.x+7,center.y+11}}};
            const CODY2D::Vec2 triangle[][2] = {{{center.x-6,center.y-11},{center.x+10,center.y}},
                {{center.x+10,center.y},{center.x-6,center.y+11}},{{center.x-6,center.y+11},{center.x-6,center.y-11}}};
            const CODY2D::Vec2 chevron[][2] = {{{center.x+4,center.y-11},{center.x-7,center.y}},
                {{center.x-7,center.y},{center.x+4,center.y+11}}};
            const auto *segments = control.identity == RigControl::Back ? chevron : paused ? triangle : bars;
            const size_t count = control.identity != RigControl::Back && paused ? 3 : 2;
            for (size_t index = 0; index < count; ++index) {
                result = CODY2D::stroke_rows(strip, first_row, segments[index][0], segments[index][1],
                    Data::back_stroke, ink, first_column);
                if (result != ESP_OK) {
                    return result;
                }
            }
        }
    }
    return ESP_OK;
}
}  // namespace

// point: Logical display coordinate using exactly the rendered controls' geometry.
RigControl rig_control_at(CODY2D::Vec2 point)
{
    for (const auto &control : NavigationData::controls) {
        if (control.visible_by_default && navigation_contains(control, point)) {
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

// channels: Independent finite controls, each inside its authored range.
// transform: Finite logical position and scale accepted by the shared renderer.
esp_err_t validate_rig_pose(const Channels &channels, Transform transform)
{
    const esp_err_t face_error = validate_face_channels(channels, "Rig pose");
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
    return ESP_OK;
}

// channels: Authored independent facial controls; invalid values stop evaluation.
// transform: Logical translation and scale, shared by every feature and debug marker.
// scene: Caller-owned geometry and pearl row mappings, never valid after failure.
// contour: Optional outline controls; parented handles inherit their own lobe's translation.
// deform_face: Runtime preference for the same forward warp as the pearlescent body.
esp_err_t evaluate_rig(const Channels &channels, Transform transform, RigScene &scene,
                       const ContourState *contour, bool deform_face)
{
    scene.valid = false;
    scene.face_deformed = CODY_ENABLE_PEARL_JELLY && deform_face;
#if CODY_ENABLE_PEARL_JELLY
    scene.jelly.valid = false;
#endif
    if (contour != nullptr) {
        const esp_err_t contour_error = validate_contour_state(*contour, "Rig evaluation");
        if (contour_error != ESP_OK) {
            return contour_error;
        }
    }
    const esp_err_t pose_error = validate_rig_pose(channels, transform);
    if (pose_error != ESP_OK) {
        return pose_error;
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
        // Coverage includes a pixel even when its center is just outside the
        // polygon. Culling must consider the complete pixel footprint.
        const float coverage_margin = definition.antialiasing == CODY2D::FillAntialiasing::Coverage ? .5f : 0.f;
        const float stroke_margin = stroke_width * .5f + coverage_margin;
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
#if CODY_ENABLE_PEARL_JELLY
    const esp_err_t jelly_error = prepare_jelly_frame(scene, scene.jelly);
    if (jelly_error != ESP_OK) {
        return jelly_error;
    }
    if (deform_face) {
        const esp_err_t face_warp_error = deform_jelly_face(scene);
        if (face_warp_error != ESP_OK) {
            return face_warp_error;
        }
    }
#endif
    scene.valid = true;
    return ESP_OK;
}

// scene: Successfully evaluated geometry that remains unchanged while prepared strokes are used.
// selected: Stable point ID whose marker receives the selected style, or kNoPoint.
// cache: Separate caller-owned storage, invalid until every authored stroke has been prepared.
esp_err_t prepare_rig_debug_strokes(const RigScene &scene, PointId selected,
                                    RigDebugStrokeCache &cache)
{
    cache.valid_ = false;
    cache.source_scene_ = nullptr;
    cache.link_count_ = 0;
    cache.tangent_count_ = 0;
    cache.marker_count_ = 0;
    cache.selected_ = kNoPoint;
    if (!scene.valid || (selected != kNoPoint && to_index(selected) >= std::size(Data::points))) {
        ESP_LOGE(kTag, "Debug stroke preparation rejected: valid=%u selected=%u (%s)",
                 static_cast<unsigned>(scene.valid), static_cast<unsigned>(to_index(selected)),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t prepared_link_count = 0;
    for (uint16_t point_index = 0; point_index < std::size(Data::points); ++point_index) {
        const auto &point = Data::points[point_index];
        if (point.role != PointRole::Anchor || point.parent == PointId::None) {
            continue;
        }
        const esp_err_t prepare_error = CODY2D::prepare_stroke(
            scene.world[to_index(point.parent)], scene.world[point_index],
            Data::debug_link_width, Data::palette[to_index(Data::debug_link)],
            cache.links_[prepared_link_count]
        );
        if (prepare_error != ESP_OK) {
            ESP_LOGE(kTag, "Debug link preparation failed for point=%u: %s",
                     static_cast<unsigned>(to_index(point.id)), esp_err_to_name(prepare_error));
            return prepare_error;
        }
        ++prepared_link_count;
    }

    uint16_t prepared_tangent_count = 0;
    for (const auto &curve : Data::curves) {
        // Keep the same authored ordering as the generic renderer.
        for (const auto &pair : {
                std::pair{CurvePoint::Start, CurvePoint::OutgoingHandle},
                std::pair{CurvePoint::End, CurvePoint::IncomingHandle}}) {
            const PointId anchor = curve.points[to_index(pair.first)];
            const PointId handle = curve.points[to_index(pair.second)];
            const esp_err_t prepare_error = CODY2D::prepare_stroke(
                scene.world[to_index(anchor)], scene.world[to_index(handle)],
                Data::debug_handle_width, Data::palette[to_index(Data::debug_handle)],
                cache.tangents_[prepared_tangent_count]
            );
            if (prepare_error != ESP_OK) {
                ESP_LOGE(kTag, "Debug tangent preparation failed for anchor=%u handle=%u: %s",
                         static_cast<unsigned>(to_index(anchor)),
                         static_cast<unsigned>(to_index(handle)), esp_err_to_name(prepare_error));
                return prepare_error;
            }
            ++prepared_tangent_count;
        }
    }

    uint16_t prepared_marker_count = 0;
    for (uint16_t point_index = 0; point_index < std::size(Data::points); ++point_index) {
        const auto &point = Data::points[point_index];
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
        const esp_err_t prepare_error = CODY2D::prepare_stroke(
            scene.world[point_index], scene.world[point_index], diameter,
            Data::palette[to_index(marker_color)], cache.markers_[prepared_marker_count]
        );
        if (prepare_error != ESP_OK) {
            ESP_LOGE(kTag, "Debug marker preparation failed for point=%u: %s",
                     static_cast<unsigned>(to_index(point.id)), esp_err_to_name(prepare_error));
            return prepare_error;
        }
        ++prepared_marker_count;
    }

    // Counts and provenance are the commit record. Partially written arrays
    // remain unreachable after any earlier failure.
    cache.link_count_ = prepared_link_count;
    cache.tangent_count_ = prepared_tangent_count;
    cache.marker_count_ = prepared_marker_count;
    cache.source_scene_ = &scene;
    cache.selected_ = selected;
    cache.valid_ = true;
    return ESP_OK;
}

// scene: Completed vector face and pearl mapping reused unchanged by every tile.
// strip: RGB565 destination owned by the companion worker.
// first_row: Tile y origin in the unrotated logical canvas.
// debug: Enables bones, Bézier tangents and evaluated point markers.
// selected: Point emphasized by the latest real touch selection.
// profile: Optional caller-owned collector with a monotonic execution clock.
// paused: Draw the Resume action without changing caller-owned geometry.
// first_column: Logical x origin of the compact tile; absolute raster arithmetic is preserved.
// debug_cache: Optional prepared links, tangents and markers for this scene and selection.
// navigation_visible: Hidden scene controls have no raster footprint.
// navigation_opacity: Debug toolbar fade applied without advancing the character clock.
esp_err_t draw_rig_rows(const RigScene &scene, CODY2D::Surface strip,
                        uint16_t first_row, bool debug, PointId selected, RigRasterProfile *profile,
                        bool paused, uint16_t first_column,
                        const RigDebugStrokeCache *debug_cache, bool navigation_visible, uint8_t navigation_opacity)
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
    const RigDebugStrokeView debug_strokes = debug_cache != nullptr
        ? debug_cache->view() : RigDebugStrokeView{};
    if (debug && debug_cache != nullptr &&
        (!debug_strokes.valid || debug_strokes.source_scene != &scene ||
         debug_strokes.selected != selected ||
         debug_strokes.link_count != kAuthoredDebugLinkCount ||
         debug_strokes.tangent_count != kAuthoredDebugTangentCount ||
         debug_strokes.marker_count != kAuthoredDebugMarkerCount)) {
        ESP_LOGE(kTag,
                 "Prepared debug cache rejected: valid=%u source=%u selected=%u expected_selected=%u "
                 "links=%u tangents=%u markers=%u (%s)",
                 static_cast<unsigned>(debug_strokes.valid),
                 static_cast<unsigned>(debug_strokes.source_scene == &scene),
                 static_cast<unsigned>(to_index(debug_strokes.selected)),
                 static_cast<unsigned>(to_index(selected)),
                 static_cast<unsigned>(debug_strokes.link_count),
                 static_cast<unsigned>(debug_strokes.tangent_count),
                 static_cast<unsigned>(debug_strokes.marker_count),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (first_column + strip.width > 466 || first_row + strip.height > 466) {
        ESP_LOGE(kTag, "Rig strip rejected: column=%u width=%u row=%u height=%u (%s)",
                 first_column, strip.width, first_row, strip.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
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
#if CODY_ENABLE_PEARL_JELLY
    result = draw_jelly_rows(scene.jelly, strip, first_row, first_column);
    if (result != ESP_OK) {
        return result;
    }
#endif
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        const auto &path = scene.paths[index];
#if CODY_ENABLE_PEARL_JELLY
        // The sprite owns the body contour and baked relief. The original paths
        // remain evaluated for rig inspection, but must not cover the texture.
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
#endif
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
                                          static_cast<uint8_t>(to_index(definition.last_color)),
                                          path.opacity, first_column, definition.antialiasing);
        } else {
            for (uint16_t segment = 1; segment < path.count; ++segment) {
                result = CODY2D::stroke_rows(strip, first_row, vertices[segment-1], vertices[segment],
                                             definition.stroke * scene.scale,
                                             Data::palette[to_index(definition.first_color)], first_column);
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
        result = draw_debug_rows(
            scene, strip, first_row, selected, profile, stage_started_us, first_column,
            debug_cache != nullptr ? &debug_strokes : nullptr
        );
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
    if (navigation_visible) {
        result = draw_navigation_rows(strip, first_row, debug, paused, first_column, navigation_opacity);
    }
    if (profile != nullptr) {
        profile->navigation_us += profile->now_us() - stage_started_us;
    }
    return result;
}

// scene: Successfully evaluated geometry, shared unchanged with the renderer.
// debug: Include every bone, tangent and selected marker when the overlay is visible.
// bounds: Receives pearl and face coverage clipped to the 466-pixel canvas.
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
#if CODY_ENABLE_PEARL_JELLY
    if (!scene.jelly.valid) {
        ESP_LOGE(kTag, "Artwork bounds rejected: invalid jelly mapping (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (!scene.jelly.bounds.empty()) {
        left = scene.jelly.bounds.left;
        top = scene.jelly.bounds.top;
        right = scene.jelly.bounds.right;
        bottom = scene.jelly.bounds.bottom;
    }
#endif
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        const auto &path = scene.paths[index];
#if CODY_ENABLE_PEARL_JELLY
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
#endif
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

// No parameters: only default-visible controls contribute upper-rim damage.
DamageRect rig_navigation_damage_bounds()
{
    DamageRect bounds{466, 466, 0, 0};
    for (const auto &control : NavigationData::controls) {
        if (control.visible_by_default) {
            bounds.left = std::min(bounds.left, control.bounds.left);
            bounds.top = std::min(bounds.top, control.bounds.top);
            bounds.right = std::max(bounds.right, control.bounds.right);
            bounds.bottom = std::max(bounds.bottom, control.bounds.bottom);
        }
    }
    return bounds.right == 0 ? DamageRect{} : bounds;
}

// control: Named navigation control; None and unrecognized identities are rejected.
// bounds: Receives the existing circle's half-open logical bounds, without changing layout.
esp_err_t rig_control_damage_bounds(RigControl control, DamageRect &bounds)
{
    bounds = {};
    for (const auto &navigation : NavigationData::controls) {
        if (navigation.identity == control) {
            bounds = navigation.bounds;
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Navigation bounds rejected: control=%u (%s)",
             static_cast<unsigned>(control), esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
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
