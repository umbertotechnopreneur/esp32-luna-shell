#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "CODY_damage.hpp"

namespace CODY {

struct RigScene;
enum class PathId : uint16_t;

struct JellySkinSelection {
    const char *id = nullptr;
    const char *display_name = nullptr;
    bool is_default = false;
};

// A row is an internal storage span, not the identity of an authored lobe.
struct JellyScanline {
    uint16_t left = 0;
    uint16_t right = 0;
    int32_t source_x = 0;
    int32_t source_step = 0;
    int32_t source_y = 0;
};

struct JellyControl {
    float y = 0;
    float left = 0;
    float right = 0;
};

// Caller-owned PSRAM, copied together with the completed rig. No heap ownership.
struct JellyFrame {
    std::array<JellyScanline, 466> rows{};
    std::array<JellyControl, 33> controls{};
    DamageRect bounds{};
    uint16_t skin_id = 0;
    bool valid = false;
};

// No parameters: number of authored skins available to a future selection screen.
size_t jelly_skin_count();

// index: Internal catalog traversal offset; identity still comes from the returned stable ID.
// selection: Receives immutable ID, user-facing name and default state on success only.
// Exception: Returns ESP_ERR_INVALID_ARG when index is outside the fixed catalog.
esp_err_t jelly_skin_at(size_t index, JellySkinSelection &selection);

// id: Exact authored internal ID, such as "cody.skin.pearl_mist.v1".
// Exception: Returns ESP_ERR_INVALID_ARG for null or ESP_ERR_NOT_FOUND for an unknown ID.
esp_err_t select_jelly_skin(const char *id);

// selection: Receives the current session selection on success only.
// Exception: Returns ESP_ERR_INVALID_STATE if internal selection no longer resolves uniquely.
esp_err_t selected_jelly_skin(JellySkinSelection &selection);

// identity: Authored path whose vector surface is replaced by the pearl sprite.
bool jelly_replaces_path(PathId identity);

// scene: World points and scale already evaluated by the rig; not yet committed valid.
// frame: Invalidated on entry; receives the complete, clipped inverse row mapping.
// Exception: Returns the real geometry error; no texture or pose fallback is substituted.
esp_err_t prepare_jelly_frame(const RigScene &scene, JellyFrame &frame);

// scene: Prepared sprite mapping and unwarped facial vertices; invalidated by the caller on failure.
// The exact forward row warp moves sampled paths and their debug/touch points together.
esp_err_t deform_jelly_face(RigScene &scene);

// frame: Immutable prepared mapping for this exact evaluated rig.
// strip: Writable native RGB565 tile, with explicit stride and capacity.
// first_row: Logical top of the tile before shared display rotation.
// first_column: Logical left of the tile; sampling is independent of tile boundaries.
// Exception: Returns the real surface, frame, bounds or skin identity error.
esp_err_t draw_jelly_rows(const JellyFrame &frame, CODY2D::Surface strip,
                         uint16_t first_row, uint16_t first_column);

// previous: Last successfully presented mapping.
// current: Next successfully prepared mapping.
bool same_jelly_frame(const JellyFrame &previous, const JellyFrame &current);

}  // namespace CODY
