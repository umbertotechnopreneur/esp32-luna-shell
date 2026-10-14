#pragma once

#include "CODY_technical_panels.hpp"

namespace CODY {
inline constexpr Transform kHomeDashboardCharacterTransform{233.f, 122.f, 73.f};
inline constexpr DamageRect kHomeModelBounds{73, 236, 393, 264};

// Serialized display owner; glyph pointers remain valid until complete frame retirement.
struct HomeIdentityPresentation {
    char model[129]{};
    char chat[65]{};
    CODY2D::TextLine model_line{};
    int64_t started_us = 0;
    uint32_t revision = 0;
    uint16_t offset = 0;
    bool valid = false;
};
static_assert(sizeof(HomeIdentityPresentation) <= 288, "Home identity remains bounded persistent storage");

// scene: Existing authored title/reveal state; only native font, position and color are adjusted.
// link: Canonical link/activity observation borrowed from its sole acquisition owner.
// technical: Independently aged canonical metadata; no inferred model or defaults.
// now_us: Original receipt clock used by the shared source-admission policy.
// active_time_us: Pausable display clock controlling the stable model marquee only.
// owner: Persistent serialized caption owner, never copied onto a task stack.
// model_content: Receives the transparent model-row callback and complete damage bounds.
// Exception: Returns and logs original projection or native text/layout failures.
esp_err_t prepare_home_identity(SceneFrame &scene, const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, int64_t now_us, int64_t active_time_us,
    HomeIdentityPresentation &owner, SceneContent &model_content);

// owner: Immutable prepared model window retained through full raster retirement.
// strip: Native RGB565 tile using the original display byte order and stride.
// first_row: Original logical tile y origin.
// first_column: Original logical tile x origin shared with rotation and touch.
// Exception: Returns the real native text/surface error, never a substitute raster.
esp_err_t draw_home_identity_rows(const HomeIdentityPresentation &owner, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column = 0);
} // namespace CODY
