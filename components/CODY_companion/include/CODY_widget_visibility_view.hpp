#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "CODY_widget_state.hpp"

namespace CODY {

enum class WidgetVisibilityControl : uint8_t { None, Toggle, OpenChildren };

// Display-worker storage borrowed by the tile callback until the whole frame retires.
struct WidgetVisibilityView {
    std::array<CODY2D::TextLine, 4> captions{};
    CODY2D::CapsuleControl toggle{};
    CODY2D::CapsuleControl subscreens{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    char page_index[12]{};
    WidgetVisibility visibility{};
    WidgetScreen screen = WidgetScreen::Cody;
    CompanionPanel panel = CompanionPanel::Home;
    uint32_t revision = 0;
    bool children = false;
    bool saving = false;
    bool save_failed = false;
    bool load_failed = false;
    bool subscreens_visible = false;
    bool valid = false;
};
static_assert(sizeof(WidgetVisibilityView) <= 512, "The visibility editor uses bounded display storage");

// screen: Current root entry, used on the top-level carousel.
// panel: Current Cody child entry, used on the subscreens carousel.
// children: True when editing Cody child preferences.
// visibility: Last successfully loaded or persisted policy.
// saving: True while the settings owner is processing a pending change.
// save_failed: True when the latest save failed; the confirmed value is still shown.
// load_failed: True when settings could not be loaded; the fallback value is not shown or editable.
// title_font: Immutable native glyphs for the current entry name.
// body_font: Immutable native glyphs for secondary captions and controls.
// view: Persistent display-owned storage, writable after prior frame retirement.
// content: Receives the central renderer; lateral navigation belongs to the caller.
// Exception: Preserves native layout errors and rejects invalid catalog selections or policy.
esp_err_t prepare_widget_visibility_view(WidgetScreen screen, CompanionPanel panel, bool children,
    const WidgetVisibility &visibility, bool saving, bool save_failed, bool load_failed,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    WidgetVisibilityView &view, SceneContent &content);

// view: Fully presented editor state as verified by the caller.
// point: Logical touch point in the selected display orientation.
WidgetVisibilityControl widget_visibility_control_at(const WidgetVisibilityView &view, CODY2D::Point point);

} // namespace CODY
