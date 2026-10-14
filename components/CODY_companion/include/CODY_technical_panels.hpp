#pragma once

#include <cstdint>
#include "CODY_scene.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class CompanionPanel : uint8_t { Home = 0, Activity = 1, ModelContext = 2, SessionLink = 3 };
enum class PanelValueState : uint8_t { Unavailable = 0, Current = 1, Stale = 2, Invalid = 3 };

// Shared passive-detail artwork and transparent content layout; these are logical coordinates.
inline constexpr Transform kCompanionPanelCharacterTransform{233.f, 115.f, 50.f};
inline constexpr DamageRect kCompanionPanelContentBounds{73, 170, 393, 402};

struct PanelMetric {
    uint64_t value = 0;
    PanelValueState state = PanelValueState::Unavailable;
};

// Bounded borrowed metadata is consumed during preparation, never retained by a raster callback.
struct PanelInputs {
    const char *project = "";
    const char *branch = "";
    const char *description = "";
    const char *chat_id = "";
    const char *model = "";
    const char *effort = "";
    const char *peer_ip = "";
    const char *firmware_version = "";
    LUNASHELL::CodexActivityState phase = LUNASHELL::CodexActivityState::None;
    LUNASHELL::CodexCompactionState compaction = LUNASHELL::CodexCompactionState::None;
    PanelValueState link = PanelValueState::Unavailable;
    PanelValueState activity = PanelValueState::Unavailable;
    PanelValueState model_state = PanelValueState::Unavailable;
    PanelValueState effort_state = PanelValueState::Unavailable;
    LUNASHELL::CodexCostBand cost_band = LUNASHELL::CodexCostBand::Unknown;
    PanelValueState cost_state = PanelValueState::Unavailable;
    bool connected = false;
    bool simulated = false;
    bool context_coherent = false;
    bool context_estimated = false;
    bool counters_pending = false;
    PanelMetric used{};
    PanelMetric capacity{};
    PanelMetric threshold{};
    PanelMetric cumulative{};
    PanelMetric input{};
    PanelMetric output{};
    PanelMetric activity_age_us{};
    PanelMetric usage_age_us{};
    PanelMetric turn_elapsed_us{};
    PanelMetric compaction_elapsed_us{};
};

enum class ModelDetailPage : uint8_t { Context = 0, TokenDetails = 1 };

// One serialized owner retains all captions and prepared borrowed font lines through retirement.
struct PanelPresentation {
    char captions[8][32]{};
    CODY2D::TextLine lines[8]{};
    char title[32]{};
    char page[4]{};
    char banner[32]{};
    char hint[32]{};
    char cost_caption[32]{};
    CODY2D::TextLine title_line{};
    CODY2D::TextLine page_line{};
    CODY2D::TextLine banner_line{};
    CODY2D::TextLine hint_line{};
    CODY2D::TextLine cost_line{};
    // Serialized prior captions avoid a large temporary copy in the nested preparation frame.
    char prior_captions[8][32]{};
    char prior_title[32]{};
    char prior_banner[32]{};
    char prior_hint[32]{};
    uint16_t prior_row_colors[8]{};
    CompanionPanel panel = CompanionPanel::Home;
    ModelDetailPage model_page = ModelDetailPage::Context;
    LUNASHELL::CodexCostBand cost_band = LUNASHELL::CodexCostBand::Unknown;
    uint32_t revision = 0;
    uint32_t compaction_clock_us = 0;
    uint16_t bar_pixels = 0;
    uint16_t bar_color = 0x632C;
    uint8_t line_count = 0;
    bool context_bar_visible = false;
    bool compaction_active = false;
    bool valid = false;
};
static_assert(sizeof(PanelPresentation) <= 1600, "Panel captions must remain one bounded owner buffer");

// current: Stable companion view; Home enters Model & Context and technical views cycle independently.
// next: Receives the next typed technical identity after validation.
// Exception: Returns invalid argument for an unknown panel identity.
esp_err_t next_companion_panel(CompanionPanel current, CompanionPanel &next);

// panel: Explicit technical page identity; Home is not a technical presentation.
// inputs: Bounded current or explicitly stale metadata from its authenticated/synthetic owner.
// font: Actual immutable native body font; technical text never uses typewriter timing.
// active_time_us: Nonnegative presentation time for numeric page rotation and the compaction accent.
// presentation: Serialized caption owner, retained in place until every frame retires.
// content: Receives a transparent detail callback below the reserved character artwork.
// motion: Optional canonical experimental model result; null preserves the original details.
// Exception: Returns the original metadata, metric or native font/layout error.
esp_err_t prepare_companion_panel(CompanionPanel panel, const PanelInputs &inputs,
    const CODY2D::MonoFont &font, int64_t active_time_us,
    PanelPresentation &presentation, SceneContent &content, const LUNASHELL::ActivitySnapshot *motion = nullptr);

// link: Canonical link/activity observation borrowed for one projection.
// technical: Canonical independently aged metadata, never copied or mutated.
// now_us: Local monotonic receipt clock, independent of presentation animation.
// firmware_version: Immutable running image identity, or empty when unavailable.
// inputs: Receives bounded borrowed fields and independently valid metrics.
// Exception: Returns the original duration or clock error without fabricating source data.
esp_err_t project_companion_panel_inputs(const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, int64_t now_us,
    const char *firmware_version, PanelInputs &inputs);

// panel: Explicit selected technical identity.
// link: Coherent canonical link/activity projection borrowed only during preparation.
// technical: Independently aged technical projection from the same canonical observation.
// font: Actual native body font retained by prepared glyph lines.
// now_us: Local monotonic observation time; remote ages are durations, never remote clock values.
// active_time_us: Independent local presentation clock; never contributes to source freshness or ages.
// presentation: Serialized stable caption owner outside the rendering task stack.
// content: Receives the transparent native detail callback below the artwork on success.
// firmware_version: Canonical running-image identity; absent host metadata remains unavailable.
// motion: Optional canonical experimental activity result; null preserves the ordinary panel.
// Exception: Returns the real projection or native preparation error.
esp_err_t prepare_companion_panel_from_observation(CompanionPanel panel,
    const LUNASHELL::CodexLinkSnapshot &link, const LUNASHELL::CodexTechnicalSnapshot &technical,
    const CODY2D::MonoFont &font, int64_t now_us, int64_t active_time_us,
    PanelPresentation &presentation, SceneContent &content,
    const char *firmware_version = "", const LUNASHELL::ActivitySnapshot *motion = nullptr);

// presentation: Successfully prepared serialized panel state.
// active_time_us: Nonnegative active clock; this changes only the compaction accent.
// changed: Receives whether its quantized indeterminate opacity changed.
// Exception: Returns invalid panel/clock state without fabricating compaction progress.
esp_err_t advance_companion_panel(PanelPresentation &presentation, int64_t active_time_us, bool &changed);

// presentation: Immutable panel captured for the current complete frame.
// strip: Borrowed native RGB565 tile, with its original byte order and stride.
// first_row: Logical tile y origin shared with rendering and touch rotation.
// first_column: Logical tile x origin shared with rendering and touch rotation.
// Exception: Returns the original surface, font or antialiased control drawing failure.
esp_err_t draw_companion_panel_rows(const PanelPresentation &presentation,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column);

} // namespace CODY
