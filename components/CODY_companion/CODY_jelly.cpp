#include "CODY_jelly.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include "CODY_rig.hpp"
#include "cody_jelly_data.hpp"
#include "cody_jelly_skins.hpp"
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_jelly";
constexpr int32_t kFixedOne = 65536;
constexpr uint16_t kCanvasSize = 466;
static_assert(std::size(JellyData::rows) == 33);
static_assert(JellyData::width > 1 && JellyData::height > 1);

static_assert(std::size(JellyFrame{}.controls) == std::size(JellyData::rows));
std::atomic<JellySkinId> s_selected_skin{JellySkinCatalog::default_skin};

// id: Exact stable authored ID, never a display name or catalog position.
const JellySkinDefinition *find_skin(const char *id)
{
    if (id == nullptr) {
        return nullptr;
    }
    for (const auto &definition : JellySkinCatalog::definitions) {
        if (std::strcmp(definition.key, id) == 0) {
            return &definition;
        }
    }
    return nullptr;
}

// identity: Typed authored identity, independent from catalog ordering.
const JellySkinDefinition *find_skin(JellySkinId identity)
{
    for (const auto &definition : JellySkinCatalog::definitions) {
        if (definition.id == identity) {
            return &definition;
        }
    }
    return nullptr;
}

// identity: Authored point whose ancestry determines whether it belongs to the face.
bool is_facial_point(PointId identity)
{
    for (size_t depth = 0; depth < std::size(Data::points) && identity != PointId::None; ++depth) {
        switch (identity) {
            case PointId::CheekLeft:
            case PointId::CheekRight:
            case PointId::EyeLeft:
            case PointId::EyeLeftShine:
            case PointId::EyeRight:
            case PointId::EyeRightShine:
            case PointId::BrowLeft:
            case PointId::BrowRight:
            case PointId::Nose:
            case PointId::Mouth:
                return true;
            default:
                identity = Data::points[to_index(identity)].parent;
                break;
        }
    }
    return false;
}

// scene: Rig with an already prepared, monotonic sprite row mapping.
// point: Unwarped logical position, replaced only after a finite forward mapping succeeds.
esp_err_t warp_face_point(const RigScene &scene, CODY2D::Vec2 &point)
{
    if (!scene.jelly.valid || !std::isfinite(point.x) || !std::isfinite(point.y)) {
        ESP_LOGE(kTag, "Face warp input rejected: valid=%u point=%f,%f (%s)",
                 scene.jelly.valid, point.x, point.y, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto center = scene.world[to_index(PointId::Center)];
    const float local_y = (point.y - center.y) / scene.scale;
    size_t support = 0;
    while (support + 1 < std::size(JellyData::rows) - 1 &&
           local_y >= JellyData::rows[support + 1].local_y) {
        ++support;
    }
    const auto &source_upper = JellyData::rows[support];
    const auto &source_lower = JellyData::rows[support + 1];
    const auto &upper = scene.jelly.controls[support];
    const auto &lower = scene.jelly.controls[support + 1];
    // Endpoint segments extend linearly for authored facial handles outside the
    // sprite rectangle. No expression is silently clamped to the body boundary.
    const float fraction = (local_y - source_upper.local_y) /
                           (source_lower.local_y - source_upper.local_y);
    const float left = upper.left + (lower.left - upper.left) * fraction;
    const float right = upper.right + (lower.right - upper.right) * fraction;
    const float local_x = (point.x - center.x) / scene.scale;
    const float horizontal = (local_x - JellyData::local_left) /
                             (JellyData::local_right - JellyData::local_left);
    const CODY2D::Vec2 warped{
        left + (right - left) * horizontal,
        upper.y + (lower.y - upper.y) * fraction
    };
    if (!std::isfinite(warped.x) || !std::isfinite(warped.y) ||
        std::abs(warped.x) > 2048.f || std::abs(warped.y) > 2048.f) {
        ESP_LOGE(kTag, "Face warp output rejected: %f,%f (%s)",
                 warped.x, warped.y, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    point = warped;
    return ESP_OK;
}

// first: Packed premultiplied RGBA8 texel at the lower interpolation endpoint.
// second: Packed premultiplied RGBA8 texel at the upper interpolation endpoint.
// weight: Eight-bit fraction in [0,255]; 256 would select the second endpoint.
uint32_t lerp_rgba(uint32_t first, uint32_t second, uint32_t weight)
{
    // Two independent 16-bit lanes keep the full multiply in 32-bit arithmetic.
    constexpr uint32_t lanes = 0x00ff00ffU;
    const uint32_t inverse = 256U - weight;
    const uint32_t red_blue = (((first & lanes) * inverse + (second & lanes) * weight +
                               0x00800080U) >> 8U) & lanes;
    const uint32_t green_alpha = ((((first >> 8U) & lanes) * inverse +
                                   ((second >> 8U) & lanes) * weight +
                                   0x00800080U) >> 8U) & lanes;
    return red_blue | (green_alpha << 8U);
}

// source_x: Signed Q16 source coordinate at the destination pixel's center.
// upper: First source scanline selected once for the destination row.
// lower: Adjacent source scanline, or upper at the texture's lower edge.
// vertical_weight: Eight-bit source-row interpolation fraction.
uint32_t sample_rgba(int32_t source_x, const uint32_t *upper,
                     const uint32_t *lower, uint32_t vertical_weight)
{
    // This clamps the sampler footprint, not input state. The master has real
    // transparent padding, so extending its edge never manufactures visible ink.
    const int32_t clipped = std::clamp<int32_t>(source_x, 0, (JellyData::width - 1) * kFixedOne);
    const uint32_t column = static_cast<uint32_t>(clipped) >> 16U;
    const uint32_t next = std::min(column + 1U, static_cast<uint32_t>(JellyData::width - 1));
    const uint32_t horizontal_weight = (static_cast<uint32_t>(clipped) >> 8U) & 255U;
    const uint32_t above = lerp_rgba(upper[column], upper[next], horizontal_weight);
    const uint32_t below = lerp_rgba(lower[column], lower[next], horizontal_weight);
    return lerp_rgba(above, below, vertical_weight);
}

// source: Bilinearly filtered premultiplied RGBA8 sample.
// destination: Existing native RGB565 canvas pixel under the sprite.
uint16_t blend_rgba(uint32_t source, uint16_t destination)
{
    const uint32_t alpha = source >> 24U;
    if (alpha == 0) {
        return destination;
    }
    uint32_t red = source & 255U;
    uint32_t green = (source >> 8U) & 255U;
    uint32_t blue = (source >> 16U) & 255U;
    if (alpha != 255U && destination != 0) {
        const uint32_t inverse = 255U - alpha;
        red += (((destination >> 11U) * 255U / 31U) * inverse + 127U) / 255U;
        green += ((((destination >> 5U) & 63U) * 255U / 63U) * inverse + 127U) / 255U;
        blue += (((destination & 31U) * 255U / 31U) * inverse + 127U) / 255U;
    }
    // Quantize only the composed sample. Premultiplied interpolation keeps the
    // translucent contour clean; no black matte is baked into the source colors.
    const uint32_t native_red = (std::min<uint32_t>(red, 255U) * 31U + 127U) / 255U;
    const uint32_t native_green = (std::min<uint32_t>(green, 255U) * 63U + 127U) / 255U;
    const uint32_t native_blue = (std::min<uint32_t>(blue, 255U) * 31U + 127U) / 255U;
    return static_cast<uint16_t>((native_red << 11U) | (native_green << 5U) | native_blue);
}

}  // namespace

// No parameters: expose bounded catalog size without treating position as identity.
size_t jelly_skin_count()
{
    return std::size(JellySkinCatalog::definitions);
}

// index: Traversal offset at this API boundary, never persisted or used as identity.
// selection: Receives immutable catalog data only after a valid bounds check.
esp_err_t jelly_skin_at(size_t index, JellySkinSelection &selection)
{
    if (index >= std::size(JellySkinCatalog::definitions)) {
        ESP_LOGE(kTag, "Skin catalog index rejected: %u (%s)",
                 static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto &definition = JellySkinCatalog::definitions[index];
    selection = {
        definition.key,
        definition.display_name,
        definition.id == JellySkinCatalog::default_skin,
    };
    return ESP_OK;
}

// id: Exact authored identity selected for subsequent evaluated frames.
// Exception: Null and unknown IDs are rejected without changing the current skin.
esp_err_t select_jelly_skin(const char *id)
{
    if (id == nullptr) {
        ESP_LOGE(kTag, "Skin selection rejected: id=<null> (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const JellySkinDefinition *definition = find_skin(id);
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Skin selection rejected: id=%s (%s)",
                 id, esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    s_selected_skin.store(definition->id, std::memory_order_release);
    ESP_LOGI(kTag, "Skin selected for subsequent frames: id=%s name=%s%s",
             definition->key, definition->display_name,
             definition->id == JellySkinCatalog::default_skin ? " default" : "");
    return ESP_OK;
}

// selection: Receives the current typed selection after validating the catalog identity.
esp_err_t selected_jelly_skin(JellySkinSelection &selection)
{
    const JellySkinId identity = s_selected_skin.load(std::memory_order_acquire);
    const JellySkinDefinition *definition = find_skin(identity);
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Selected skin identity rejected: value=%u (%s)",
                 static_cast<unsigned>(identity), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    selection = {
        definition->key,
        definition->display_name,
        definition->id == JellySkinCatalog::default_skin,
    };
    return ESP_OK;
}

// identity: Semantic vector surface path replaced by the approved sprite.
bool jelly_replaces_path(PathId identity)
{
    switch (identity) {
        case PathId::Body:
        case PathId::BodyShadowLower:
        case PathId::BodyShadowCrown:
        case PathId::BodyShadowLeft:
        case PathId::BodyShadowRight:
            return true;
        default:
            return false;
    }
}

// scene: Evaluated world points and scale, before the enclosing scene becomes valid.
// frame: Caller-owned mapping, made valid only after every visible row is prepared.
esp_err_t prepare_jelly_frame(const RigScene &scene, JellyFrame &frame)
{
    frame.valid = false;
    frame.bounds = {};
    frame.skin_id = static_cast<uint16_t>(s_selected_skin.load(std::memory_order_acquire));
    if (!std::isfinite(scene.scale) || scene.scale <= 0 || scene.scale > 204.f) {
        ESP_LOGE(kTag, "Prepare scale rejected: %f (%s)", scene.scale,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto center = scene.world[to_index(PointId::Center)];
    std::array<CODY2D::Vec2, std::size(JellyData::anchors)> offsets{};
    for (size_t index = 0; index < std::size(JellyData::anchors); ++index) {
        const auto &anchor = JellyData::anchors[index];
        const auto position = scene.world[to_index(anchor.point)];
        offsets[index] = {position.x - center.x - anchor.rest.x * scene.scale,
                          position.y - center.y - anchor.rest.y * scene.scale};
    }
    auto &controls = frame.controls;
    for (size_t index = 0; index < controls.size(); ++index) {
        const auto &binding = JellyData::rows[index];
        auto &control = controls[index];
        control = {center.y + binding.local_y * scene.scale,
                   center.x + JellyData::local_left * scene.scale,
                   center.x + JellyData::local_right * scene.scale};
        for (size_t anchor = 0; anchor < offsets.size(); ++anchor) {
            control.left += offsets[anchor].x * binding.left[anchor];
            control.right += offsets[anchor].x * binding.right[anchor];
            control.y += offsets[anchor].y * binding.vertical[anchor];
        }
        if (!std::isfinite(control.y) || !std::isfinite(control.left) || !std::isfinite(control.right) ||
            std::abs(control.y) > 2048.f || std::abs(control.left) > 2048.f ||
            std::abs(control.right) > 2048.f || control.right - control.left <= .001f ||
            (index > 0 && control.y - controls[index - 1].y <= .001f)) {
            ESP_LOGE(kTag, "Prepare row %u rejected: y=%f left=%f right=%f (%s)",
                     static_cast<unsigned>(binding.id), control.y, control.left, control.right,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    const int first = std::clamp(static_cast<int>(std::ceil(controls.front().y - .5f)), 0, 466);
    const int last = std::clamp(static_cast<int>(std::ceil(controls.back().y - .5f)), 0, 466);
    size_t support = 0;
    for (int row = first; row < last; ++row) {
        const float sample_y = row + .5f;
        while (support + 1 < controls.size() - 1 && sample_y >= controls[support + 1].y) {
            ++support;
        }
        const auto &upper = controls[support];
        const auto &lower = controls[support + 1];
        const float fraction = (sample_y - upper.y) / (lower.y - upper.y);
        const float left = upper.left + (lower.left - upper.left) * fraction;
        const float right = upper.right + (lower.right - upper.right) * fraction;
        auto &mapping = frame.rows[row];
        mapping = {};
        mapping.left = static_cast<uint16_t>(std::clamp(static_cast<int>(std::floor(left)), 0, 466));
        mapping.right = static_cast<uint16_t>(std::clamp(static_cast<int>(std::ceil(right)), 0, 466));
        if (mapping.left >= mapping.right) {
            continue;
        }
        const float step = JellyData::width / (right - left);
        // Bound the fixed-point step before conversion. This is rejection of a
        // degenerate mapping, not an implicit change to authored motion controls.
        if (!std::isfinite(step) || step <= 0 || step > 1024.f) {
            ESP_LOGE(kTag, "Prepare row %d sampling step rejected: %f (%s)", row, step,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        const auto &source_upper = JellyData::rows[support];
        const auto &source_lower = JellyData::rows[support + 1];
        const float source_y = source_upper.source_y +
            (source_lower.source_y - source_upper.source_y) * fraction - .5f;
        mapping.source_x = static_cast<int32_t>(std::lround(((mapping.left + .5f - left) * step - .5f) * kFixedOne));
        mapping.source_step = static_cast<int32_t>(std::lround(step * kFixedOne));
        mapping.source_y = static_cast<int32_t>(std::lround(source_y * kFixedOne));
        if (frame.bounds.empty()) {
            frame.bounds = {mapping.left, static_cast<uint16_t>(row), mapping.right,
                            static_cast<uint16_t>(row + 1)};
        } else {
            frame.bounds.left = std::min(frame.bounds.left, mapping.left);
            frame.bounds.right = std::max(frame.bounds.right, mapping.right);
            frame.bounds.bottom = static_cast<uint16_t>(row + 1);
        }
    }
    frame.valid = true;
    return ESP_OK;
}

// scene: Uncommitted rig with a valid sprite mapping; keeps authored local coordinates intact.
esp_err_t deform_jelly_face(RigScene &scene)
{
    for (size_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
        auto &path = scene.paths[index];
        const float coverage = definition.antialiasing == CODY2D::FillAntialiasing::Coverage ? .5f : 0.f;
        const float margin = definition.stroke * scene.scale * .5f + coverage;
        for (uint16_t offset = 0; offset < path.count; ++offset) {
            auto &point = scene.vertices[path.first_vertex + offset];
            const esp_err_t error = warp_face_point(scene, point);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Face path %s warp failed: %s", definition.name, esp_err_to_name(error));
                return error;
            }
            if (offset == 0) {
                path.first_scan_y = point.y - margin;
                path.last_scan_y = point.y + margin;
            } else {
                path.first_scan_y = std::min(path.first_scan_y, point.y - margin);
                path.last_scan_y = std::max(path.last_scan_y, point.y + margin);
            }
        }
    }
    // Cubics are sampled before the nonlinear warp. Warping only their control
    // points would follow a different curve from the painted texture between rows.
    for (const auto &definition : Data::points) {
        if (!is_facial_point(definition.id)) {
            continue;
        }
        const esp_err_t error = warp_face_point(scene, scene.world[to_index(definition.id)]);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Face point %s warp failed: %s", definition.name, esp_err_to_name(error));
            return error;
        }
    }
    return ESP_OK;
}

// frame: Prepared logical rows; no floating-point work or PNG decode occurs here.
// strip: RGB565 output with caller-owned writable storage and explicit byte order.
// first_row: Logical tile y origin, independent of the framebuffer's row stride.
// first_column: Logical tile x origin, preserving identical samples across tile splits.
esp_err_t draw_jelly_rows(const JellyFrame &frame, CODY2D::Surface strip,
                         uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (!frame.valid || first_row + strip.height > kCanvasSize || first_column + strip.width > kCanvasSize) {
        ESP_LOGE(kTag, "Draw rejected: valid=%u tile=%u,%u %ux%u (%s)", frame.valid,
                 first_column, first_row, strip.width, strip.height, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto skin = static_cast<JellySkinId>(frame.skin_id);
    const uint32_t *pixels = JellyData::pixels_for(skin);
    if (pixels == nullptr || find_skin(skin) == nullptr) {
        ESP_LOGE(kTag, "Draw skin rejected: value=%u (%s)", frame.skin_id,
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t first = std::max(first_row, frame.bounds.top);
    const uint16_t last = std::min(static_cast<uint16_t>(first_row + strip.height), frame.bounds.bottom);
    for (uint16_t row = first; row < last; ++row) {
        const auto &mapping = frame.rows[row];
        const uint16_t left = std::max(first_column, mapping.left);
        const uint16_t right = std::min(static_cast<uint16_t>(first_column + strip.width), mapping.right);
        if (left >= right) {
            continue;
        }
        const int32_t source_y = std::clamp<int32_t>(mapping.source_y, 0, (JellyData::height - 1) * kFixedOne);
        const uint32_t source_row = static_cast<uint32_t>(source_y) >> 16U;
        const uint32_t next_row = std::min(source_row + 1U, static_cast<uint32_t>(JellyData::height - 1));
        const uint32_t vertical_weight = (static_cast<uint32_t>(source_y) >> 8U) & 255U;
        const auto *upper = pixels + source_row * JellyData::width;
        const auto *lower = pixels + next_row * JellyData::width;
        int32_t source_x = mapping.source_x + (left - mapping.left) * mapping.source_step;
        auto *destination = strip.pixels + (row - first_row) * strip.stride + left - first_column;
        for (uint16_t column = left; column < right; ++column) {
            const uint32_t sample = sample_rgba(source_x, upper, lower, vertical_weight);
            // Alpha blending uses native channels even when the tile is ready for the panel.
            const uint16_t native_destination = CODY2D::rgb565_stored_to_native(
                *destination, strip.byte_order
            );
            const uint16_t blended_color = blend_rgba(sample, native_destination);
            *destination = CODY2D::rgb565_native_to_stored(blended_color, strip.byte_order);
            ++destination;
            source_x += mapping.source_step;
        }
    }
    return ESP_OK;
}

// previous: Last completed scene's immutable row mapping.
// current: Next evaluated row mapping, including both texture and alpha geometry.
bool same_jelly_frame(const JellyFrame &previous, const JellyFrame &current)
{
    if (!previous.valid || !current.valid || previous.skin_id != current.skin_id ||
        previous.bounds.left != current.bounds.left ||
        previous.bounds.top != current.bounds.top || previous.bounds.right != current.bounds.right ||
        previous.bounds.bottom != current.bounds.bottom) {
        return false;
    }
    for (uint16_t row = current.bounds.top; row < current.bounds.bottom; ++row) {
        const auto &old_row = previous.rows[row];
        const auto &new_row = current.rows[row];
        if (old_row.left != new_row.left || old_row.right != new_row.right ||
            old_row.source_x != new_row.source_x || old_row.source_step != new_row.source_step ||
            old_row.source_y != new_row.source_y) {
            return false;
        }
    }
    return true;
}

}  // namespace CODY
