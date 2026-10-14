#pragma once

#include "CODY_technical_panels.hpp"
#include "CODY2D_paths.hpp"
#include "CODY2D_controls.hpp"

namespace CODY {
enum class HomeDashboardArcState : uint8_t { Unavailable, Measured, Stale, Invalid, OverLimit, Compacting };
inline constexpr DamageRect kHomeDashboardBounds{40, 268, 426, 393};
inline constexpr unsigned kHomeDashboardSegments = 20;
inline constexpr unsigned kHomeDashboardArcVertices = 64;

struct HomeDashboardArcPath {
    CODY2D::Vec2 vertices[kHomeDashboardArcVertices]{};
    uint8_t vertex_count = 0;
    uint8_t color_slot = 0;
};

// Serialized component-owned presentation; borrowed glyph strings stay alive until frame retirement.
struct HomeDashboardPresentation {
    char captions[6][48]{};
    char prior_captions[6][48]{};
    CODY2D::TextLine lines[6]{};
    // One continuous track and at most two accent ribbons when an indeterminate sweep wraps.
    HomeDashboardArcPath arc_paths[3]{};
    uint16_t prior_colors[6]{};
    CODY2D::Point prior_origins[6]{};
    const CODY2D::MonoFont *prior_fonts[6]{};
    CODY2D::CapsuleControl price_capsule{};
    bool price_visible = false;
    CompanionPanel panel = CompanionPanel::Home;
    HomeDashboardArcState arc_state = HomeDashboardArcState::Unavailable;
    uint32_t revision = 0;
    uint16_t arc_units = 0;
    uint16_t animation_step = 0;
    uint8_t line_count = 0;
    uint8_t arc_path_count = 0;
    bool valid = false;
};
static_assert(sizeof(HomeDashboardPresentation) <= 6144, "Dashboard remains a bounded persistent owner, never a stack copy");

// current: Stable lower-panel identity; cloud, state and model retain their independent owners.
// next: Receives Home -> ModelContext -> SessionLink -> Home; legacy Activity retires to Home.
// Exception: Logs and returns invalid argument for an unknown identity.
esp_err_t next_home_dashboard_panel(CompanionPanel current, CompanionPanel &next);

// panel: Explicit lower-panel identity, including the default Home token arc.
// link: Canonical link/activity from the synchronized observation owner.
// technical: Independently valid and aged canonical technical counters.
// title_font: Immutable native title glyphs for the primary numeric value.
// body_font: Immutable native body glyphs for labels and fallback widths.
// now_us: Monotonic receipt clock used exclusively for source ages.
// active_time_us: Local presentation clock used exclusively for page and indeterminate animation.
// presentation: Persistent serialized owner retained until the full frame retires.
// content: Receives a transparent lower-panel callback and complete damage bounds.
// motion: Optional canonical activity classifier result; null is explicitly unavailable.
// small_font: Optional immutable native small font for secondary metrics and motion.
// device_ip: Original canonical local-device address, never the paired PC address.
// device_ip_state: Independent canonical validity/freshness of the local Wi-Fi address.
// Exception: Returns and logs projection, formatting, geometry or native raster preparation errors.
esp_err_t prepare_home_dashboard(CompanionPanel panel,
    const LUNASHELL::CodexLinkSnapshot &link, const LUNASHELL::CodexTechnicalSnapshot &technical,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    int64_t now_us, int64_t active_time_us, HomeDashboardPresentation &presentation,
    SceneContent &content, const LUNASHELL::ActivitySnapshot *motion = nullptr,
    const CODY2D::MonoFont *small_font = nullptr, const char *device_ip = "",
    PanelValueState device_ip_state = PanelValueState::Unavailable);

// presentation: Immutable prepared component owner, never a temporary copy.
// strip: Native RGB565 tile, including original byte order and stride.
// first_row: Logical y origin shared with touch and display rotation.
// first_column: Logical x origin shared with touch and display rotation.
// Exception: Returns the original state, text or antialiased ribbon raster error.
esp_err_t draw_home_dashboard_rows(const HomeDashboardPresentation &presentation,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column = 0);
} // namespace CODY
