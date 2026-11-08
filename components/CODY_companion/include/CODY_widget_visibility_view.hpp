#pragma once

#include <array>
#include <optional>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "CODY_widget_state.hpp"

namespace CODY {

#if defined(CODY_DEVICE_UI_ONLY)
inline constexpr uint8_t kVisibilityRowsPerPage = 3;
inline constexpr uint8_t kVisibilityPageCount =
    (kWidgetScreenCount - 1U + kVisibilityRowsPerPage - 1U) / kVisibilityRowsPerPage;

struct WidgetVisibilityRow {
    CODY2D::CapsuleControl control{};
    CODY2D::TextLine status{};
    WidgetScreen screen = WidgetScreen::Device;
};
#else
enum class WidgetVisibilityControl : uint8_t {
    None,
    Toggle,
    OpenChildren,
};
#endif

// Display-worker storage borrowed by the tile callback until the whole frame retires.
struct WidgetVisibilityView {
#if defined(CODY_DEVICE_UI_ONLY)
    std::array<CODY2D::TextLine, 2> captions{};
    std::array<WidgetVisibilityRow, kVisibilityRowsPerPage> rows{};
    uint8_t page = 0;
#else
    std::array<CODY2D::TextLine, 4> captions{};
    CODY2D::CapsuleControl toggle{};
    CODY2D::CapsuleControl subscreens{};
    WidgetScreen screen = WidgetScreen::Cody;
    CompanionPanel panel = CompanionPanel::Home;
    bool children = false;
    bool subscreens_visible = false;
#endif
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    char page_index[32]{};
    WidgetVisibility visibility{};
    uint32_t revision = 0;
    bool saving = false;
    bool save_failed = false;
    bool load_failed = false;
    bool valid = false;
};
static_assert(sizeof(WidgetVisibilityView) <= 512, "The visibility editor uses bounded display storage");

// page: Zero-based page of configurable screens in device-first firmware.
// screen: Selected legacy root entry.
// panel: Legacy Cody child entry.
// children: True when editing legacy Cody child preferences.
// visibility: Last successfully loaded or persisted policy.
// saving: True while the settings owner is processing a pending change.
// save_failed: True when the latest save failed; the confirmed value is still shown.
// load_failed: True when settings could not be loaded; the fallback value is not shown or editable.
// title_font: Immutable native glyphs for the main heading or legacy entry name.
// body_font: Immutable native glyphs for secondary captions and controls.
// view: Persistent display-owned storage, writable after prior frame retirement.
// content: Receives the central renderer; lateral navigation belongs to the caller.
// Exception: Preserves native layout errors and rejects invalid catalog selections or policy.
esp_err_t prepare_widget_visibility_view(
#if defined(CODY_DEVICE_UI_ONLY)
    uint8_t page,
#else
    WidgetScreen screen, CompanionPanel panel, bool children,
#endif
    const WidgetVisibility &visibility, bool saving, bool save_failed, bool load_failed,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    WidgetVisibilityView &view, SceneContent &content);

// view: Fully presented editor state as verified by the caller.
// point: Logical touch point in the selected display orientation.
#if defined(CODY_DEVICE_UI_ONLY)
std::optional<size_t> widget_visibility_row_at(const WidgetVisibilityView &view, CODY2D::Point point);
#else
WidgetVisibilityControl widget_visibility_control_at(const WidgetVisibilityView &view, CODY2D::Point point);
#endif

} // namespace CODY
