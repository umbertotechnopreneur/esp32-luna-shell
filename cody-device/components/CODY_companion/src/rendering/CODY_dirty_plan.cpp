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

#include "CODY_dirty_plan.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_dirty_plan";
constexpr uint16_t kCanvasSize = 466;
static_assert(DirtyPlan::row_pair_count * 2U == kCanvasSize);

struct StrokePrimitive {
    CODY2D::Vec2 from{};
    CODY2D::Vec2 to{};
    float width = 0;
    ColorId color{};
    bool visible = false;
};

// point: Logical geometry, checked before any conversion to integer bounds.
bool valid_point(CODY2D::Vec2 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
        std::abs(point.x) <= 2048.f && std::abs(point.y) <= 2048.f;
}

// first: One evaluated canvas position.
// second: Corresponding canvas position in the other completed evaluation.
bool same_point(CODY2D::Vec2 first, CODY2D::Vec2 second)
{
    return first.x == second.x && first.y == second.y;
}

// scene: Completed vector and pearl evaluation whose storage extents must be safe.
// selected: Stable point identity, or kNoPoint when nothing is highlighted.
// operation: Identifies the previous/current scene in failure logs.
esp_err_t validate_scene(const RigScene &scene, PointId selected, const char *operation)
{
    if (!scene.valid || !std::isfinite(scene.scale) || scene.scale <= 0 ||
        (selected != kNoPoint && to_index(selected) >= std::size(Data::points))) {
        ESP_LOGE(kTag, "%s rejected: valid=%u scale=%f selected=%u (%s)",
            operation, scene.valid, scene.scale, static_cast<unsigned>(to_index(selected)),
            esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
#if CODY_ENABLE_PEARL_JELLY
    if (!scene.jelly.valid) {
        ESP_LOGE(kTag, "%s rejected: invalid jelly mapping (%s)", operation,
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
#endif
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        if (!valid_point(scene.world[index])) {
            ESP_LOGE(kTag, "%s point %s rejected (%s)", operation, Data::points[index].name,
                esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &path = scene.paths[index];
        const auto &definition = Data::paths[index];
        const float width = definition.stroke * scene.scale;
        const uint32_t vertex_end = static_cast<uint32_t>(path.first_vertex) + path.count;
        const uint16_t minimum_count = definition.stroke == 0 ? 3 : 2;
        if (path.count < minimum_count || vertex_end > scene.vertices.size() ||
            (definition.stroke == 0 && path.count > CODY2D::kMaxPathVertices) ||
            !std::isfinite(width) || width < 0 || width > CODY2D::kMaxStrokeWidth) {
            ESP_LOGE(kTag, "%s path %s rejected: first=%u count=%u width=%f (%s)",
                operation, definition.name, path.first_vertex, path.count, width,
                esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        for (uint16_t offset = 0; offset < path.count; ++offset) {
            if (!valid_point(scene.vertices[path.first_vertex + offset])) {
                ESP_LOGE(kTag, "%s path %s vertex %u rejected (%s)", operation,
                    definition.name, offset, esp_err_to_name(ESP_ERR_INVALID_ARG));
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return ESP_OK;
}

// plan: Paired-row accumulator receiving the clipped rectangle.
// left: Conservative floating-point left edge from validated geometry.
// top: Conservative floating-point top edge from validated geometry.
// right: Conservative floating-point right edge from validated geometry.
// bottom: Conservative floating-point bottom edge from validated geometry.
esp_err_t include_float_bounds(DirtyPlan &plan, float left, float top,
                               float right, float bottom)
{
    // Geometry can leave the canvas. Clip before unsigned conversion; a fully
    // off-screen primitive has no displayed footprint to invalidate.
    if (left >= right || top >= bottom || right <= 0 || bottom <= 0 ||
        left >= kCanvasSize || top >= kCanvasSize) {
        return ESP_OK;
    }
    const DamageRect bounds{
        static_cast<uint16_t>(std::floor(std::max(0.f, left))),
        static_cast<uint16_t>(std::floor(std::max(0.f, top))),
        static_cast<uint16_t>(std::ceil(std::min(static_cast<float>(kCanvasSize), right))),
        static_cast<uint16_t>(std::ceil(std::min(static_cast<float>(kCanvasSize), bottom)))
    };
    return include_damage(plan, bounds);
}

// plan: Accumulator receiving the complete visible capsule footprint.
// stroke: Validated logical endpoints, round width and visibility.
esp_err_t include_stroke(DirtyPlan &plan, const StrokePrimitive &stroke)
{
    if (!stroke.visible) {
        return ESP_OK;
    }
    const float margin = stroke.width * .5f + 1.f;
    return include_float_bounds(plan,
        std::min(stroke.from.x, stroke.to.x) - margin,
        std::min(stroke.from.y, stroke.to.y) - margin,
        std::max(stroke.from.x, stroke.to.x) + margin,
        std::max(stroke.from.y, stroke.to.y) + margin);
}

// plan: Accumulator retaining damage from every previously inspected primitive.
// previous: Primitive as last displayed, including whether it was visible.
// current: Primitive to display next, including its native palette identity.
esp_err_t include_changed_stroke(DirtyPlan &plan, const StrokePrimitive &previous,
                                 const StrokePrimitive &current)
{
    if (!previous.visible && !current.visible) {
        return ESP_OK;
    }
    if (previous.visible == current.visible && previous.width == current.width &&
        previous.color == current.color && same_point(previous.from, current.from) &&
        same_point(previous.to, current.to)) {
        return ESP_OK;
    }
    const esp_err_t previous_error = include_stroke(plan, previous);
    if (previous_error != ESP_OK) {
        return previous_error;
    }
    return include_stroke(plan, current);
}

// previous: Valid completed geometry containing the old path's vertices.
// old_path: Validated old path extent.
// current: Valid next geometry containing the new path's vertices.
// new_path: Validated new path extent.
bool same_path_geometry(const RigScene &previous, const EvaluatedPath &old_path,
                         const RigScene &current, const EvaluatedPath &new_path)
{
    if (old_path.count != new_path.count) {
        return false;
    }
    for (uint16_t offset = 0; offset < old_path.count; ++offset) {
        if (!same_point(previous.vertices[old_path.first_vertex + offset],
                        current.vertices[new_path.first_vertex + offset])) {
            return false;
        }
    }
    return true;
}

// plan: Accumulator receiving the entire filled polygon footprint.
// scene: Valid scene owning the evaluated vertices.
// path: Valid path extent whose visibility and opacity determine whether it draws.
esp_err_t include_fill(DirtyPlan &plan, const RigScene &scene, const EvaluatedPath &path)
{
    if (!path.visible || path.opacity == 0) {
        return ESP_OK;
    }
    const auto first = scene.vertices[path.first_vertex];
    float left = first.x;
    float top = first.y;
    float right = first.x;
    float bottom = first.y;
    for (uint16_t offset = 1; offset < path.count; ++offset) {
        const auto point = scene.vertices[path.first_vertex + offset];
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    // A changed top/bottom also changes the gradient throughout the polygon,
    // so restricting filled-path damage to moving edges would leave stale ink.
    return include_float_bounds(plan, left - 1.f, top - 1.f, right + 1.f, bottom + 1.f);
}

// plan: Accumulator retaining old and new footprints for this filled path.
// previous: Immutable completed scene.
// old_path: Path metadata from the completed scene.
// current: Successfully evaluated next scene.
// new_path: Corresponding path metadata from the next scene.
esp_err_t include_changed_fill(DirtyPlan &plan,
                               const RigScene &previous, const EvaluatedPath &old_path,
                               const RigScene &current, const EvaluatedPath &new_path)
{
    const bool old_visible = old_path.visible && old_path.opacity != 0;
    const bool new_visible = new_path.visible && new_path.opacity != 0;
    if (!old_visible && !new_visible) {
        return ESP_OK;
    }
    if (old_visible == new_visible && old_path.opacity == new_path.opacity &&
        same_path_geometry(previous, old_path, current, new_path)) {
        return ESP_OK;
    }
    const esp_err_t previous_error = include_fill(plan, previous, old_path);
    if (previous_error != ESP_OK) {
        return previous_error;
    }
    return include_fill(plan, current, new_path);
}

// scene: Valid scene supplying canvas coordinates and the current overall scale.
// path: Valid evaluated stroke path extent.
// definition: Immutable authored stroke style.
// segment: Storage traversal index of the segment's second endpoint.
StrokePrimitive path_segment(const RigScene &scene, const EvaluatedPath &path,
                              const PathDefinition &definition, uint16_t segment)
{
    if (!path.visible || segment >= path.count) {
        return {};
    }
    return {scene.vertices[path.first_vertex + segment - 1U],
        scene.vertices[path.first_vertex + segment], definition.stroke * scene.scale,
        definition.first_color, true};
}

// scene: Valid scene supplying the point's logical position.
// definition: Authored stable identity and role selecting the ordinary marker style.
// selected: Stable highlighted point identity for this frame.
// visible: Whether this frame contains the skeleton overlay.
StrokePrimitive point_marker(const RigScene &scene, const PointDefinition &definition,
                              PointId selected, bool visible)
{
    float diameter = Data::debug_anchor_diameter;
    ColorId color = Data::debug_knot;
    if (definition.id == selected) {
        diameter = Data::debug_selected_diameter;
        color = Data::debug_selected;
    } else if (definition.role == PointRole::Handle) {
        diameter = Data::debug_handle_diameter;
        color = Data::debug_handle;
    } else if (definition.role == PointRole::Anchor) {
        color = Data::debug_anchor;
    }
    const auto position = scene.world[to_index(definition.id)];
    return {position, position, diameter, color, visible};
}

}  // namespace

// plan: Caller-owned accumulator, whose existing damage is retained.
// damage: Half-open logical rectangle inside 466x466, expanded to even boundaries.
esp_err_t include_damage(DirtyPlan &plan, const DamageRect &damage)
{
    if (damage.left > damage.right || damage.top > damage.bottom ||
        damage.right > kCanvasSize || damage.bottom > kCanvasSize) {
        ESP_LOGE(kTag, "Damage rectangle rejected: [%u,%u,%u,%u] (%s)",
            damage.left, damage.top, damage.right, damage.bottom,
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (damage.empty()) {
        return ESP_OK;
    }
    const uint16_t left = static_cast<uint16_t>(damage.left & ~1U);
    const uint16_t right = static_cast<uint16_t>((damage.right + 1U) & ~1U);
    const uint16_t first_pair = damage.top / 2U;
    const uint16_t after_last_pair = (damage.bottom + 1U) / 2U;
    for (uint16_t pair = first_pair; pair < after_last_pair; ++pair) {
        auto &span = plan.row_pairs[pair];
        if (span.left == span.right) {
            span = {left, right};
        } else {
            span.left = std::min(span.left, left);
            span.right = std::max(span.right, right);
        }
    }
    return ESP_OK;
}

// plan: Immutable paired-row spans accumulated for the current frame.
// first_row: Even logical origin of the available raster tile.
// row_count: Positive even row count, wholly inside the 466-row canvas.
// damage: Receives aligned compact bounds, or an empty rectangle when unchanged.
esp_err_t dirty_tile_bounds(const DirtyPlan &plan, uint16_t first_row,
                            uint16_t row_count, DamageRect &damage)
{
    damage = {};
    const uint32_t after_last_row = static_cast<uint32_t>(first_row) + row_count;
    if (first_row % 2U != 0 || row_count == 0 || row_count % 2U != 0 ||
        after_last_row > kCanvasSize) {
        ESP_LOGE(kTag, "Dirty tile rejected: row=%u rows=%u (%s)", first_row, row_count,
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t left = kCanvasSize;
    uint16_t right = 0;
    uint16_t top = kCanvasSize;
    uint16_t bottom = 0;
    for (uint16_t pair = first_row / 2U; pair < after_last_row / 2U; ++pair) {
        const auto &span = plan.row_pairs[pair];
        if (span.left > span.right || span.right > kCanvasSize ||
            span.left % 2U != 0 || span.right % 2U != 0) {
            ESP_LOGE(kTag, "Dirty row pair %u rejected: [%u,%u] (%s)",
                pair, span.left, span.right, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        if (span.left == span.right) {
            continue;
        }
        left = std::min(left, span.left);
        right = std::max(right, span.right);
        top = std::min(top, static_cast<uint16_t>(pair * 2U));
        bottom = static_cast<uint16_t>((pair + 1U) * 2U);
    }
    if (left < right) {
        damage = {left, top, right, bottom};
    }
    return ESP_OK;
}

// scene: Valid geometry supplying exactly the texture and visible path footprints.
// plan: Existing accumulated damage; this helper does not reset it.
esp_err_t include_rig_artwork_damage(const RigScene &scene, DirtyPlan &plan)
{
    esp_err_t error = validate_scene(scene, kNoPoint, "Fading scene");
    if (error != ESP_OK) {
        return error;
    }
    error = include_damage(plan, scene.personality.bounds);
    if (error != ESP_OK) {
        return error;
    }
#if CODY_ENABLE_PEARL_JELLY
    error = include_damage(plan, scene.jelly.bounds);
    if (error != ESP_OK) {
        return error;
    }
#endif
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
#if CODY_ENABLE_PEARL_JELLY
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
#endif
        const auto &path = scene.paths[index];
        if (definition.stroke == 0) {
            error = include_fill(plan, scene, path);
        } else {
            for (uint16_t segment = 1; segment < path.count && error == ESP_OK; ++segment) {
                error = include_stroke(plan, path_segment(scene, path, definition, segment));
            }
        }
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

// previous: Immutable geometry from the last completely displayed rig frame.
// current: Successfully evaluated geometry for the next complete frame.
// previous_debug: Whether the completed frame included the skeleton overlay.
// current_debug: Whether the next frame includes the skeleton overlay.
// previous_selected: Stable point ID highlighted in the completed frame, or kNoPoint.
// current_selected: Stable point ID highlighted in the next frame, or kNoPoint.
// plan: Reset on entry, then receives old/new texture, face and overlay damage.
esp_err_t plan_rig_damage(const RigScene &previous, const RigScene &current,
                          bool previous_debug, bool current_debug,
                          PointId previous_selected, PointId current_selected,
                          DirtyPlan &plan)
{
    plan = {};
    esp_err_t result = validate_scene(previous, previous_selected, "Previous scene");
    if (result != ESP_OK) {
        return result;
    }
    result = validate_scene(current, current_selected, "Current scene");
    if (result != ESP_OK) {
        return result;
    }
    if (!same_personality_frame(previous.personality, current.personality)) {
        result = include_damage(plan, previous.personality.bounds);
        if (result != ESP_OK) {
            return result;
        }
        result = include_damage(plan, current.personality.bounds);
        if (result != ESP_OK) {
            return result;
        }
    }
#if CODY_ENABLE_PEARL_JELLY
    if (!same_jelly_frame(previous.jelly, current.jelly)) {
        // A row warp moves painted light as well as alpha. Redraw both complete
        // footprints, even when a vector edge's bounds happen to be unchanged.
        result = include_damage(plan, previous.jelly.bounds);
        if (result != ESP_OK) {
            return result;
        }
        result = include_damage(plan, current.jelly.bounds);
        if (result != ESP_OK) {
            return result;
        }
    }
#endif
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
#if CODY_ENABLE_PEARL_JELLY
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
#endif
        const auto &old_path = previous.paths[index];
        const auto &new_path = current.paths[index];
        if (definition.stroke == 0) {
            result = include_changed_fill(plan, previous, old_path, current, new_path);
        } else {
            const uint16_t segment_end = std::max(old_path.count, new_path.count);
            for (uint16_t segment = 1; segment < segment_end; ++segment) {
                result = include_changed_stroke(plan,
                    path_segment(previous, old_path, definition, segment),
                    path_segment(current, new_path, definition, segment));
                if (result != ESP_OK) {
                    return result;
                }
            }
        }
        if (result != ESP_OK) {
            return result;
        }
    }
    if (!previous_debug && !current_debug) {
        return ESP_OK;
    }
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const auto &point = Data::points[index];
        if (point.role == PointRole::Anchor && point.parent != PointId::None) {
            const StrokePrimitive old_link{previous.world[to_index(point.parent)],
                previous.world[index], Data::debug_link_width, Data::debug_link, previous_debug};
            const StrokePrimitive new_link{current.world[to_index(point.parent)],
                current.world[index], Data::debug_link_width, Data::debug_link, current_debug};
            result = include_changed_stroke(plan, old_link, new_link);
            if (result != ESP_OK) {
                return result;
            }
        }
        result = include_changed_stroke(plan,
            point_marker(previous, point, previous_selected, previous_debug),
            point_marker(current, point, current_selected, current_debug));
        if (result != ESP_OK) {
            return result;
        }
    }
    constexpr CurvePoint tangent_endpoints[][2] = {
        {CurvePoint::Start, CurvePoint::OutgoingHandle},
        {CurvePoint::End, CurvePoint::IncomingHandle}
    };
    for (const auto &curve : Data::curves) {
        for (const auto &endpoints : tangent_endpoints) {
            const auto anchor = to_index(curve.points[to_index(endpoints[0])]);
            const auto handle = to_index(curve.points[to_index(endpoints[1])]);
            const StrokePrimitive old_tangent{previous.world[anchor], previous.world[handle],
                Data::debug_handle_width, Data::debug_handle, previous_debug};
            const StrokePrimitive new_tangent{current.world[anchor], current.world[handle],
                Data::debug_handle_width, Data::debug_handle, current_debug};
            result = include_changed_stroke(plan, old_tangent, new_tangent);
            if (result != ESP_OK) {
                return result;
            }
        }
    }
    return ESP_OK;
}

}  // namespace CODY
