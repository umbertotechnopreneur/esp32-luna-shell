#pragma once

#include <array>
#include <cstdint>
#include "CODY2D_paths.hpp"

namespace CODY {

constexpr uint16_t kMaxPoints = 192;
constexpr uint16_t kMaxPaths = 16;
constexpr uint16_t kSubdivisions = 12;
constexpr uint16_t kMaxVertices = 64 * kSubdivisions + kMaxPaths;
constexpr uint16_t kNoPoint = UINT16_MAX;

// These channels compose independently. They are not an enumeration of whole faces.
struct Channels { float values[6] = {1, 1, 0, 0, .15f, 0}; };
struct Transform { float x = 233; float y = 235; float scale = 145; };

struct PointDefinition {
    const char *name;
    float x, y, dx, dy;
    int16_t parent;
    int8_t channel;
    uint8_t role;  // 0 anchor, 1 path knot, 2 cubic handle.
};
struct CurveDefinition { uint16_t points[4]; };
struct PathDefinition {
    uint16_t first_curve, curve_count;
    uint8_t first_color, last_color;
    float stroke;
    int8_t visible_channel;
};
struct EvaluatedPath {
    uint16_t first_vertex = 0, count = 0;
    bool visible = false;
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

// channels: Independent finite controls, each strictly inside its authored range.
// transform: Position and uniform scale in logical display pixels.
// scene: Caller-owned evaluated geometry, invalidated immediately on entry.
esp_err_t evaluate_rig(const Channels &channels, Transform transform, RigScene &scene);

// scene: Successfully evaluated geometry used by both artwork and debug.
// strip: Caller-owned full-width RGB565 tile.
// first_row: Logical y origin of the tile.
// debug: Draw skeleton links, curve handles and point markers from scene.
// selected: Optional stable point index to emphasize; kNoPoint selects none.
esp_err_t draw_rig_rows(const RigScene &scene, CODY2D::Surface strip,
                        uint16_t first_row, bool debug, uint16_t selected = kNoPoint);

// scene: Evaluated, valid geometry in current logical coordinates.
// point: Logical touch coordinate, already mapped by the display/touch owner.
// radius: Maximum selection distance in pixels.
uint16_t nearest_rig_point(const RigScene &scene, CODY2D::Vec2 point, float radius);

// index: Stable point index, or kNoPoint for no selection.
const char *rig_point_name(uint16_t index);

}  // namespace CODY
