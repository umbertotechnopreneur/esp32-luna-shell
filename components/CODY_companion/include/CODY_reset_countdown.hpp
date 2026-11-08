#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"

namespace CODY {

struct ResetCountdownFields {
    uint16_t year = 2026;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint16_t remaining_basis_points = 0;
};

enum class ResetCountdownStatus : uint8_t { Unavailable, TimeUnavailable, Stale, Active, Elapsed };

inline constexpr uint16_t kResetCountdownFlipComplete = 1000;
// Axis-aligned damage only. The perimeter raster stays inside the logical display circle.
inline constexpr DamageRect kResetCountdownRingDamageBounds{3, 3, 463, 463};

struct ResetCountdownView {
    ResetCountdownStatus status = ResetCountdownStatus::Unavailable;
    uint64_t remaining_seconds = 0;
    ResetCountdownFields fields{};
    uint64_t previous_seconds = 0;
    // 0..499 folds the old top. 500..999 unfolds the new bottom. 1000 is steady.
    uint16_t flip_progress = kResetCountdownFlipComplete;
    // The selected date fields show configured local time only with a proven offset.
    int16_t utc_offset_minutes = 0;
    bool local_time = false;
    bool saved = false;
};

// The display owner retains all borrowed text and tile faces through frame retirement.
struct ResetCountdownPresentation {
    struct Caption {
        char text[32]{};
        CODY2D::TextLine line{};
    };
    struct Tile {
        CODY2D::CapsuleControl face{};
        Caption value{};
        Caption previous{};
    };
    std::array<Tile, 4> tiles{};
    std::array<Caption, 8> captions{};
    ResetCountdownView view{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    uint32_t revision = 0;
    uint8_t tile_count = 0;
    uint8_t caption_count = 0;
    bool valid = false;
};
static_assert(sizeof(ResetCountdownPresentation) <= 2048,
    "The countdown uses bounded persistent display storage");

// fields: Reset calendar fields and reported remaining allowance in basis points.
bool valid_reset_countdown_fields(const ResetCountdownFields &fields);

// view: Current account observation and monotonic animation projection from the owner.
// title_font: Immutable native glyphs for the headline and large digits.
// body_font: Immutable native monospace glyphs for unit and account value labels.
// presentation: Persistent display-owned state, reusable after the previous frame retires.
// content: Receives the central native content. The independent perimeter is drawn separately.
// Exception: Returns original glyph/geometry errors and rejects unsupported data or phase.
esp_err_t prepare_reset_countdown(const ResetCountdownView &view,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    ResetCountdownPresentation &presentation, SceneContent &content);

// presentation: Prepared screen, retained until both central and perimeter draws retire.
bool reset_countdown_ring_visible(const ResetCountdownPresentation &presentation);

// remaining: Canonical remaining allowance in basis points, or zero to omit the ring.
// strip: Borrowed RGB565 tile carrying original stride and storage byte order.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original surface error or rejects a tile outside the canvas.
esp_err_t draw_reset_countdown_ring_rows(uint16_t remaining,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column = 0);

// presentation: Immutable screen and exact remaining basis points.
// strip: Borrowed RGB565 tile carrying original stride and storage byte order.
// first_row: Logical y of the tile.
// first_column: Logical x of the tile.
// Exception: Returns the original surface error or rejects a tile outside the canvas.
esp_err_t draw_reset_countdown_ring_rows(const ResetCountdownPresentation &presentation,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column = 0);

} // namespace CODY
