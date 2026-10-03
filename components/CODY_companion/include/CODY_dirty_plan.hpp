#pragma once

#include <array>
#include <cstdint>

#include "CODY_rig.hpp"

namespace CODY {

// One conservative horizontal span per pair of logical rows. The panel needs
// even boundaries on both axes; no pixel cache or heap allocation is involved.
struct DirtyPlan {
    static constexpr uint16_t row_pair_count = 233;

    struct RowSpan {
        uint16_t left = 0;
        uint16_t right = 0;
    };

    std::array<RowSpan, row_pair_count> row_pairs{};
};

// plan: Caller-owned accumulator, whose existing damage is retained.
// damage: Half-open logical rectangle inside 466x466, expanded to even boundaries.
esp_err_t include_damage(DirtyPlan &plan, const DamageRect &damage);

// scene: Valid evaluated artwork; brightness changes dirty its whole footprint.
// plan: Existing geometry damage retained while texture, face and stroke bounds are added.
esp_err_t include_rig_artwork_damage(const RigScene &scene, DirtyPlan &plan);

// plan: Immutable paired-row spans accumulated for the current frame.
// first_row: Even logical origin of the available raster tile.
// row_count: Positive even row count, wholly inside the 466-row canvas.
// damage: Receives aligned compact bounds, or an empty rectangle when unchanged.
esp_err_t dirty_tile_bounds(const DirtyPlan &plan, uint16_t first_row,
                            uint16_t row_count, DamageRect &damage);

// previous: Immutable geometry from the last completely displayed rig frame.
// current: Successfully evaluated geometry for the next complete frame.
// previous_debug: Whether the completed frame included the skeleton overlay.
// current_debug: Whether the next frame includes the skeleton overlay.
// previous_selected: Stable point ID highlighted in the completed frame, or kNoPoint.
// current_selected: Stable point ID highlighted in the next frame, or kNoPoint.
// plan: Reset on entry, then receives conservative old/new changed artwork bounds.
esp_err_t plan_rig_damage(const RigScene &previous, const RigScene &current,
                          bool previous_debug, bool current_debug,
                          PointId previous_selected, PointId current_selected,
                          DirtyPlan &plan);

}  // namespace CODY
