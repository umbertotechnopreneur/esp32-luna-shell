#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "CODY_widget_state.hpp"

namespace CODY {

enum class WidgetVisibilityControl : uint8_t {
    None,
    Toggle,
#if !defined(CODY_DEVICE_UI_ONLY)
    OpenChildren,
#endif
};

// Display-worker storage borrowed by the tile callback until the whole frame retires.
struct WidgetVisibilityView {
    std::array<CODY2D::TextLine, 4> captions{};
    CODY2D::CapsuleControl toggle{};
#if !defined(CODY_DEVICE_UI_ONLY)
    CODY2D::CapsuleControl subscreens{};
#endif
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    char page_index[12]{};
    WidgetVisibility visibility{};
#if defined(CODY_DEVICE_UI_ONLY)
    WidgetScreen screen = WidgetScreen::Reset;
#else
    WidgetScreen screen = WidgetScreen::Cody;
    CompanionPanel panel = CompanionPanel::Home;
#endif
    uint32_t revision = 0;
#if !defined(CODY_DEVICE_UI_ONLY)
    bool children = false;
#endif
    bool saving = false;
    bool save_failed = false;
    bool load_failed = false;
#if !defined(CODY_DEVICE_UI_ONLY)
    bool subscreens_visible = false;
#endif
    bool valid = false;
};
static_assert(sizeof(WidgetVisibilityView) <= 512, "The visibility editor uses bounded display storage");

// screen: Selected configurable statistics page in firmware, or legacy root entry.
// panel: Legacy Cody child entry.
// children: True when editing legacy Cody child preferences.
// visibility: Last successfully loaded or persisted policy.
// saving: True while the settings owner is processing a pending change.
// save_failed: True when the latest save failed; the confirmed value is still shown.
// load_failed: True when settings could not be loaded; the fallback value is not shown or editable.
// title_font: Immutable native glyphs for the current entry name.
// body_font: Immutable native glyphs for secondary captions and controls.
// view: Persistent display-owned storage, writable after prior frame retirement.
// content: Receives the central renderer; lateral navigation belongs to the caller.
// Exception: Preserves native layout errors and rejects invalid catalog selections or policy.
esp_err_t prepare_widget_visibility_view(WidgetScreen screen,
#if !defined(CODY_DEVICE_UI_ONLY)
    CompanionPanel panel, bool children,
#endif
    const WidgetVisibility &visibility, bool saving, bool save_failed, bool load_failed,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    WidgetVisibilityView &view, SceneContent &content);

// view: Fully presented editor state as verified by the caller.
// point: Logical touch point in the selected display orientation.
WidgetVisibilityControl widget_visibility_control_at(const WidgetVisibilityView &view, CODY2D::Point point);

} // namespace CODY
