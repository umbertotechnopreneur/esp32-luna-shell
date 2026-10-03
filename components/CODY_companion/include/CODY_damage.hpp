#pragma once

#include "CODY2D.hpp"

namespace CODY {

// Half-open logical bounds. An empty result never submits a panel transaction.
struct DamageRect {
    uint16_t left = 0;
    uint16_t top = 0;
    uint16_t right = 0;
    uint16_t bottom = 0;

    // No parameters: zero width or height denotes unchanged pixels.
    bool empty() const { return left == right || top == bottom; }
};

// Conservative logical candidates from the frame owner. Old and new artwork
// footprints both matter: departing pixels need to be erased too.
struct DamageRegions {
    static constexpr uint16_t capacity = 4;
    DamageRect rectangles[capacity]{};
    uint16_t count = 0;
};

// tile: Fully rendered ordinary RGB565 rows, before any in-place packing.
// first_row: Logical origin of the tile in the complete canvas.
// previous: PSRAM-owned previous frame; never a DMA source.
// compare_previous: False after startup, failed frames, rotation or panel invalidation.
// regions: Complete conservative coverage of everything that could have changed.
// damage: Receives the paired-column bounds of actual changed pixels.
esp_err_t measure_damage_rows(CODY2D::Surface tile, uint16_t first_row,
                             CODY2D::Surface previous, bool compare_previous,
                             const DamageRegions &regions,
                             DamageRect &damage);

// tile: Fully rendered tile whose pixels become the candidate cache contents.
// first_row: Logical row offset into the complete cache.
// previous: Frame-owned cache; its owner marks it invalid until full-frame completion.
// damage: Changed region measured before the cache copy.
// packed: Receives a compact transfer view borrowing the tile's storage.
esp_err_t prepare_damage_rows(CODY2D::Surface tile, uint16_t first_row,
                             CODY2D::Surface previous, const DamageRect &damage,
                             CODY2D::Surface &packed);

}  // namespace CODY
