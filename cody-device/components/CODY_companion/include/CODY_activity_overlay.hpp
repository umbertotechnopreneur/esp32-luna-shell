#pragma once

#include <cstdint>
#include "CODY2D.hpp"
#include "CODY_damage.hpp"

namespace CODY {

// These presentation identities describe authenticated activity, never progress estimates.
enum class ActivityOverlayState : uint8_t {
    Hidden = 0,
    Working = 1,
    Waiting = 2,
    ResponseReady = 3,
    Compacting = 4,
};

struct ActivityOverlayFrame {
    ActivityOverlayState state = ActivityOverlayState::Hidden;
    uint32_t elapsed_us = 0;
};

constexpr uint32_t kActivityOverlayCycleUs = 1800000;
constexpr uint32_t kActivityOverlayResponseUs = 1200000;

// No parameters: absolute logical placement stays separate from character geometry.
constexpr DamageRect activity_overlay_bounds()
{
    // All corners fit their occupied circular chords; status and title stay separate.
    return {324, 118, 374, 179};
}

// frame: Immutable activity presentation captured with the retiring scene frame.
// strip: Borrowed RGB565 tile whose background is already cleared.
// first_row: Logical canvas row represented by the tile.
// first_column: Logical canvas column represented by the tile.
// Exception: Returns the actual state, surface or antialiased primitive failure.
esp_err_t draw_activity_overlay_rows(const ActivityOverlayFrame &frame,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

} // namespace CODY
