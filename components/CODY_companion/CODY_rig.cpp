#include "CODY_rig.hpp"

#include <cmath>
#include <iterator>
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_rig";
static_assert(std::size(Data::points) <= kMaxPoints);
static_assert(std::size(Data::paths) <= kMaxPaths);
static_assert(std::size(Data::curves) * kSubdivisions + std::size(Data::paths) <= kMaxVertices);

// control: Four already evaluated control points.
// t: Cubic parameter between zero and one.
CODY2D::Vec2 cubic(const CODY2D::Vec2 (&control)[4], float t)
{
    const float u = 1 - t;
    return {
        u*u*u*control[0].x + 3*u*u*t*control[1].x + 3*u*t*t*control[2].x + t*t*t*control[3].x,
        u*u*u*control[0].y + 3*u*u*t*control[1].y + 3*u*t*t*control[2].y + t*t*t*control[3].y
    };
}
}  // namespace

// channels: Authored independent facial controls; invalid values stop evaluation.
// transform: Logical translation and scale, shared by every feature and debug marker.
// scene: Caller-owned geometry buffer, never retained as valid after failure.
esp_err_t evaluate_rig(const Channels &channels, Transform transform, RigScene &scene)
{
    scene.valid = false;
    for (uint16_t index = 0; index < std::size(channels.values); ++index) {
        const float value = channels.values[index];
        if (!std::isfinite(value) || value < Data::channel_min[index] || value > Data::channel_max[index]) {
            ESP_LOGE(kTag, "Channel %s rejected: %f (%s)", Data::channel_names[index], value,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!std::isfinite(transform.x) || !std::isfinite(transform.y) || !std::isfinite(transform.scale) ||
        transform.x < 0 || transform.x >= 466 || transform.y < 0 || transform.y >= 466 ||
        transform.scale < 20 || transform.scale > 200) {
        ESP_LOGE(kTag, "Transform rejected: %f,%f scale=%f (%s)", transform.x, transform.y,
                 transform.scale, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    scene.scale = transform.scale * (1 + .018f * channels.values[5]);
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const auto &definition = Data::points[index];
        const float value = definition.channel < 0 ? 0 : channels.values[definition.channel];
        const CODY2D::Vec2 local{definition.x + definition.dx * value, definition.y + definition.dy * value};
        scene.local[index] = local;
        scene.world[index] = {transform.x + local.x * scene.scale, transform.y + local.y * scene.scale};
    }
    uint16_t vertex = 0;
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        auto &path = scene.paths[index];
        path.first_vertex = vertex;
        path.visible = definition.visible_channel < 0 || channels.values[definition.visible_channel] >= .25f;
        for (uint16_t curve_index = 0; curve_index < definition.curve_count; ++curve_index) {
            const auto &curve = Data::curves[definition.first_curve + curve_index];
            CODY2D::Vec2 control[4];
            for (uint16_t p = 0; p < 4; ++p) control[p] = scene.world[curve.points[p]];
            if (curve_index == 0) scene.vertices[vertex++] = control[0];
            for (uint16_t step = 1; step <= kSubdivisions; ++step) {
                scene.vertices[vertex++] = cubic(control, static_cast<float>(step) / kSubdivisions);
            }
        }
        path.count = vertex - path.first_vertex;
    }
    scene.valid = true;
    return ESP_OK;
}

// scene: One completed evaluation used unchanged throughout the frame's strips.
// strip: RGB565 destination owned by the companion worker.
// first_row: Tile y origin in the unrotated logical canvas.
// debug: Enables bones, Bézier tangents and evaluated point markers.
// selected: Point emphasized by the latest real touch selection.
esp_err_t draw_rig_rows(const RigScene &scene, CODY2D::Surface strip,
                        uint16_t first_row, bool debug, uint16_t selected)
{
    if (!scene.valid || (selected != kNoPoint && selected >= std::size(Data::points))) {
        ESP_LOGE(kTag, "Draw rejected: valid=%u selected=%u (%s)", scene.valid, selected,
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = CODY2D::clear(strip, Data::palette[0]);
    if (result != ESP_OK) return result;
    for (uint16_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        const auto &path = scene.paths[index];
        if (!path.visible) continue;
        const auto *vertices = scene.vertices.data() + path.first_vertex;
        if (definition.stroke == 0) {
            result = CODY2D::fill_path_rows(strip, first_row, vertices, path.count,
                                          Data::palette, definition.first_color, definition.last_color);
        } else {
            for (uint16_t segment = 1; segment < path.count; ++segment) {
                result = CODY2D::stroke_rows(strip, first_row, vertices[segment-1], vertices[segment],
                                             definition.stroke * scene.scale, Data::palette[definition.first_color]);
                if (result != ESP_OK) return result;
            }
        }
        if (result != ESP_OK) return result;
    }
    if (debug) {
        for (uint16_t index = 0; index < std::size(Data::points); ++index) {
            const auto &point = Data::points[index];
            if (point.role == 0 && point.parent >= 0) {
                result = CODY2D::stroke_rows(strip, first_row, scene.world[point.parent], scene.world[index], 1, Data::palette[83]);
                if (result != ESP_OK) return result;
            }
        }
        for (const auto &curve : Data::curves) {
            for (uint16_t pair = 0; pair < 4; pair += 2) {
                result = CODY2D::stroke_rows(strip, first_row, scene.world[curve.points[pair]],
                                             scene.world[curve.points[pair+1]], 1, Data::palette[160]);
                if (result != ESP_OK) return result;
            }
        }
        for (uint16_t index = 0; index < std::size(Data::points); ++index) {
            const auto &point = Data::points[index];
            const uint16_t color = Data::palette[point.role == 2 ? 160 : point.role == 0 ? 1 : 20];
            result = CODY2D::stroke_rows(strip, first_row, scene.world[index], scene.world[index],
                                         index == selected ? 9 : point.role == 2 ? 3 : 5,
                                         index == selected ? Data::palette[110] : color);
            if (result != ESP_OK) return result;
        }
    }
    // The circular left control belongs to navigation, outside the 320px safe square.
    for (int row = 0; row < strip.height; ++row) {
        const float dy = first_row + row + .5f - 233;
        for (int x = 10; x < 70 && x < strip.width; ++x) {
            const float dx = x + .5f - 40;
            if (dx*dx + dy*dy <= 900) {
                const int slot = 20 + static_cast<int>((dy + 30) * 63 / 60);
                strip.pixels[row * strip.stride + x] = Data::palette[slot];
            }
        }
    }
    result = CODY2D::stroke_rows(strip, first_row, {44, 222}, {33, 233}, 3, Data::palette[3]);
    if (result != ESP_OK) return result;
    return CODY2D::stroke_rows(strip, first_row, {33, 233}, {44, 244}, 3, Data::palette[3]);
}

// scene: Valid evaluated geometry from the latest completed frame.
// point: Current logical input coordinate.
// radius: Positive finite search radius; no selection is returned beyond it.
uint16_t nearest_rig_point(const RigScene &scene, CODY2D::Vec2 point, float radius)
{
    if (!scene.valid || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(radius) || radius <= 0) {
        ESP_LOGE(kTag, "Point selection rejected: valid=%u x=%f y=%f radius=%f", scene.valid, point.x, point.y, radius);
        return kNoPoint;
    }
    float distance = radius*radius;
    uint16_t selected = kNoPoint;
    for (uint16_t index = 0; index < std::size(Data::points); ++index) {
        const float dx = point.x - scene.world[index].x, dy = point.y - scene.world[index].y;
        const float candidate = dx*dx + dy*dy;
        if (candidate <= distance) { distance = candidate; selected = index; }
    }
    return selected;
}

// index: Stable point index from selection or geometry traversal.
const char *rig_point_name(uint16_t index)
{
    return index < std::size(Data::points) ? Data::points[index].name : nullptr;
}
}  // namespace CODY
