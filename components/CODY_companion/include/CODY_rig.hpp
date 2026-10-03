#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "CODY2D_paths.hpp"
#include "CODY_damage.hpp"

namespace CODY {

constexpr uint16_t kMaxPoints = 192;
constexpr uint16_t kMaxPaths = 16;
constexpr uint16_t kSubdivisions = 12;
constexpr uint16_t kMaxVertices = 64 * kSubdivisions + kMaxPaths;
// Domain values are always addressed by enum; integers are confined to table traversal.
enum class FaceChannel : uint8_t {
    EyeLeft, EyeRight, BrowLeft, BrowRight, MouthOpen, Breath,
    MouthWidth, NoseSize, CheekSize, CheekOpacity, Count, None = UINT8_MAX
};
enum class ContourControl : uint8_t { Fullness, Amplitude, Speed, PhaseSpread, Count };
enum class PointRole : uint8_t { Anchor, Knot, Handle };

enum class RigControl : uint8_t { None, Back, Overlay, Pause };

// point: Logical display coordinate already mapped by the shared rotation owner.
RigControl rig_control_at(CODY2D::Vec2 point);
enum class CurvePoint : uint8_t { Start, OutgoingHandle, IncomingHandle, End };
enum class PointId : uint16_t;
enum class CurveId : uint16_t;
enum class PathId : uint16_t;
enum class EntityId : uint16_t;
enum class ContourLobeId : uint8_t;
enum class ColorId : uint16_t;

// identity: Named enum converted only when addressing compact storage.
template <typename Identity>
constexpr auto to_index(Identity identity)
{
    static_assert(std::is_enum_v<Identity>);
    return static_cast<std::underlying_type_t<Identity>>(identity);
}

constexpr PointId kNoPoint = static_cast<PointId>(UINT16_MAX);
constexpr auto kFaceChannelCount = to_index(FaceChannel::Count);
constexpr auto kContourControlCount = to_index(ContourControl::Count);

struct Channels {
    float values[kFaceChannelCount] = {};

    // channel: Semantic facial control; callers validate values at the public boundary.
    float &operator[](FaceChannel channel)
    {
        return values[to_index(channel)];
    }

    // channel: Semantic facial control to read from immutable state.
    const float &operator[](FaceChannel channel) const
    {
        return values[to_index(channel)];
    }
};

struct Transform {
    float x = 233;
    float y = 235;
    float scale = 145;
};

struct ContourControls {
    float values[kContourControlCount] = {};

    // control: Named outline control, independent from facial animation.
    float &operator[](ContourControl control)
    {
        return values[to_index(control)];
    }

    // control: Named outline control to read from immutable state.
    const float &operator[](ContourControl control) const
    {
        return values[to_index(control)];
    }
};

struct ContourState {
    ContourControls controls{};
    float phase = 0;
};

struct ContourLobeDefinition {
    PointId point;
    float direction_x;
    float direction_y;
    float fullness;
    float wave;
    float phase_turns;
};

struct PointDefinition {
    PointId id;
    const char *name;
    // Parent-relative normalized offset and independent channel displacement.
    float x;
    float y;
    float dx;
    float dy;
    PointId parent;
    FaceChannel channel;
    PointRole role;
};

struct CurveDefinition {
    CurveId id;
    PointId points[4];
};

struct PointFaceTransform {
    FaceChannel x_channel;
    FaceChannel y_channel;
};

struct EntityDefinition {
    EntityId id;
    const char *name;
    const char *label;
    PointId anchor;
};

struct PathDefinition {
    PathId id;
    const char *name;
    CurveId first_curve;
    uint16_t curve_count;
    ColorId first_color;
    ColorId last_color;
    float stroke;
    FaceChannel visible_channel;
    FaceChannel opacity_channel;
};

struct EvaluatedPath {
    uint16_t first_vertex = 0;
    uint16_t count = 0;
    bool visible = false;
    uint8_t opacity = 255;
};

// Allocate this once in caller-owned PSRAM. It contains no heap-owning objects.
struct RigScene {
    std::array<CODY2D::Vec2, kMaxPoints> local{};
    std::array<CODY2D::Vec2, kMaxPoints> world{};
    std::array<CODY2D::Vec2, kMaxVertices> vertices{};
    std::array<EvaluatedPath, kMaxPaths> paths{};
    float scale = 0;
    bool valid = false;
};

// Execution clock is supplied by the firmware owner, keeping this renderer
// portable. These durations are independent of geometry and DMA timings.
struct RigRasterProfile {
    int64_t (*now_us)() = nullptr;
    int64_t clear_us = 0;
    int64_t artwork_us = 0;
    int64_t skeleton_us = 0;
    int64_t navigation_us = 0;
};

// channels: Independent finite controls, each strictly inside its authored range.
// transform: Position and uniform scale in logical display pixels.
// scene: Caller-owned evaluated geometry, invalidated immediately on entry.
// contour: Optional independent shape state; nullptr preserves the authored outline.
esp_err_t evaluate_rig(const Channels &channels, Transform transform, RigScene &scene,
                       const ContourState *contour = nullptr);

// defaults come from the YAML-generated immutable contour table.
ContourControls contour_defaults();

// create the authored controls with zero integrated phase.
ContourState make_contour_state();

// state: Previous contour controls and phase, unchanged by a failed advance.
// target: Independent requested shape controls in their authored ranges.
// delta_ms: Nonnegative active elapsed time; zero pauses phase and transitions.
// next: Receives the complete next state only on success; may alias state.
esp_err_t advance_contour(const ContourState &state, const ContourControls &target,
                          float delta_ms, ContourState &next);

// state: Previous unanimated independent facial controls.
// target: Requested facial controls, including feature dimensions and cheek opacity.
// delta_ms: Active elapsed time; zero freezes transitions while still rejecting invalid inputs.
// next: Receives the new controls on success; may alias state.
esp_err_t advance_face(const Channels &state, const Channels &target,
                       float delta_ms, Channels &next);

// base: Independent caller-selected facial controls.
// elapsed_ms: Monotonic elapsed animation time; no frame-count dependency.
Channels animate_rig(const Channels &base, uint32_t elapsed_ms);

// scene: Successfully evaluated geometry used by both artwork and debug.
// strip: Caller-owned full-width RGB565 tile.
// first_row: Logical y origin of the tile.
// debug: Draw skeleton links, curve handles and point markers from scene.
// selected: Optional stable point ID to emphasize; kNoPoint selects none.
// profile: Optional caller-owned collector with a monotonic execution clock.
// paused: Show Play instead of Pause; scene geometry remains owned by the caller.
esp_err_t draw_rig_rows(const RigScene &scene, CODY2D::Surface strip,
                        uint16_t first_row, bool debug, PointId selected = kNoPoint,
                        RigRasterProfile *profile = nullptr, bool paused = false);

// scene: Successfully evaluated geometry, shared unchanged with the renderer.
// debug: Include every bone, tangent and selected marker when the overlay is visible.
// bounds: Receives conservative artwork coverage clipped to the 466-pixel canvas.
esp_err_t rig_damage_bounds(const RigScene &scene, bool debug, DamageRect &bounds);

// No parameters: fixed navigation occupies its own lateral control band.
DamageRect rig_navigation_damage_bounds();

// scene: Evaluated, valid geometry in current logical coordinates.
// point: Logical touch coordinate, already mapped by the display/touch owner.
// radius: Maximum selection distance in pixels.
// selected: Receives the nearest stable point, or kNoPoint for a valid empty hit.
esp_err_t nearest_rig_point(const RigScene &scene, CODY2D::Vec2 point, float radius, PointId &selected);

// identity: Stable point identity, or kNoPoint for no selection.
const char *rig_point_name(PointId identity);

}  // namespace CODY
