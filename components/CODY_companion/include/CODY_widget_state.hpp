#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#if !defined(CODY_DEVICE_UI_ONLY)
#include "CODY_technical_panels.hpp"
#endif

namespace CODY {

// These IDs are persisted as bit positions. Display order belongs to the catalog.
enum class WidgetScreen : uint8_t {
    Device = 0,
#if !defined(CODY_DEVICE_UI_ONLY)
    Cody = Device,
#endif
    Reset = 1,
    Limits = 2,
    Today = 3,
    LastSevenDays = 4,
    TokenHistory = 5,
    Credits = 6,
    Account = 7,
#if defined(CODY_DEVICE_UI_ONLY)
    Context = 8,
    About = 9,
#endif
};

template <typename Id>
struct WidgetDescriptor {
    Id id;
    const char *label;
};

#if defined(CODY_DEVICE_UI_ONLY)
inline constexpr std::size_t kWidgetScreenCount = 10;
#else
inline constexpr std::size_t kWidgetScreenCount = 8;
#endif
inline constexpr std::array<WidgetDescriptor<WidgetScreen>, kWidgetScreenCount> kWidgetScreens{{
#if defined(CODY_DEVICE_UI_ONLY)
    {WidgetScreen::Device, "Device"},
#else
    {WidgetScreen::Cody, "Cody"},
#endif
    {WidgetScreen::Reset, "Reset countdown"},
    {WidgetScreen::Limits, "Codex limits"},
    {WidgetScreen::Today, "Today"},
    {WidgetScreen::LastSevenDays, "Last 7 days"},
    {WidgetScreen::TokenHistory, "Token history"},
    {WidgetScreen::Credits, "Credits"},
    {WidgetScreen::Account, "Codex account"},
#if defined(CODY_DEVICE_UI_ONLY)
    {WidgetScreen::Context, "Context"},
    {WidgetScreen::About, "About"},
#endif
}};

#if !defined(CODY_DEVICE_UI_ONLY)
// Activity (1) is a legacy technical panel and is outside the Cody widget catalog.
inline constexpr std::array<WidgetDescriptor<CompanionPanel>, 4> kWidgetPanels{{
    {CompanionPanel::Home, "Tokens"},
    {CompanionPanel::ModelContext, "Model & Context"},
    {CompanionPanel::SessionLink, "Device"},
    {CompanionPanel::ChatActivity, "Chat & Activity"},
}};
#endif

enum class WidgetStep : uint8_t { Previous, Next };
enum class WidgetScope : uint8_t { All, Statistics };
enum class WidgetReturnRoute : uint8_t { None, Options };

namespace WidgetStateDetail {

// id: Stable catalog bit position, including a potentially unknown caller value.
template <typename Id>
constexpr uint16_t bit(Id id)
{
    const auto index = static_cast<uint8_t>(id);
    return index < 16 ? static_cast<uint16_t>(1U << index) : 0;
}

// catalog: Ordered descriptors with IDs independent of their index.
template <typename Id, std::size_t Size>
constexpr uint16_t mask(const std::array<WidgetDescriptor<Id>, Size> &catalog)
{
    uint16_t result = 0;
    for (const auto &entry : catalog) {
        result |= bit(entry.id);
    }
    return result;
}

// catalog: Ordered descriptors to search.
// enabled: Inclusion predicate for the current preference and scope.
template <typename Id, std::size_t Size, typename Enabled>
constexpr std::optional<Id> first(const std::array<WidgetDescriptor<Id>, Size> &catalog, Enabled enabled)
{
    for (const auto &entry : catalog) {
        if (enabled(entry.id)) {
            return entry.id;
        }
    }
    return std::nullopt;
}

// catalog: Ordered descriptors to traverse, wrapping once.
// current: Starting identity, which may have been hidden since selection.
// step: Direction through the catalog.
// enabled: Inclusion predicate for the current preference and scope.
template <typename Id, std::size_t Size, typename Enabled>
constexpr std::optional<Id> next(const std::array<WidgetDescriptor<Id>, Size> &catalog,
    Id current, WidgetStep step, Enabled enabled)
{
    std::size_t index = 0;
    while (index < Size && catalog[index].id != current) {
        ++index;
    }
    if (index == Size) {
        return first(catalog, enabled);
    }
    for (std::size_t visited = 0; visited < Size; ++visited) {
        index = (index + (step == WidgetStep::Next ? 1 : Size - 1)) % Size;
        const Id candidate = catalog[index].id;
        if (enabled(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace WidgetStateDetail

#if defined(CODY_DEVICE_UI_ONLY)
inline constexpr uint8_t kWidgetVisibilityVersion = 2;
#else
inline constexpr uint8_t kWidgetVisibilityVersion = 1;
#endif
inline constexpr uint16_t kWidgetScreenMask = WidgetStateDetail::mask(kWidgetScreens);
#if defined(CODY_DEVICE_UI_ONLY)
// Keep the existing settings record layout. The child mask is no longer a UI policy.
inline constexpr uint8_t kWidgetPanelMask = 0x1d;
#else
inline constexpr uint8_t kWidgetPanelMask = WidgetStateDetail::mask(kWidgetPanels);
#endif
static_assert(kWidgetScreens.front().id == WidgetScreen::Device,
    "Device is the first page in the widget catalog");
static_assert(kWidgetScreenMask == (1U << kWidgetScreenCount) - 1U && kWidgetPanelMask == 0x1d,
    "The visibility record must cover every stable screen identity");

struct WidgetVisibility {
    uint8_t version = kWidgetVisibilityVersion;
    uint16_t screen_mask = kWidgetScreenMask;
    uint8_t panel_mask = kWidgetPanelMask;
};

// visibility: Decoded or proposed preference, before it is applied or persisted.
constexpr bool valid_widget_visibility(WidgetVisibility visibility)
{
    return visibility.version == kWidgetVisibilityVersion && visibility.screen_mask != 0 &&
        (visibility.screen_mask & ~kWidgetScreenMask) == 0 &&
        (visibility.panel_mask & ~kWidgetPanelMask) == 0;
}

// visibility: Current preference.
// screen: Top-level widget identity.
constexpr bool widget_visible(WidgetVisibility visibility, WidgetScreen screen)
{
#if defined(CODY_DEVICE_UI_ONLY)
    if (screen == WidgetScreen::Device) {
        return true;
    }
#endif
    return (visibility.screen_mask & WidgetStateDetail::bit(screen)) != 0;
}

#if !defined(CODY_DEVICE_UI_ONLY)
// visibility: Current preference, regardless of the parent Cody flag.
// panel: Stable child identity, excluding the legacy Activity panel.
constexpr bool widget_panel_enabled(WidgetVisibility visibility, CompanionPanel panel)
{
    return (visibility.panel_mask & kWidgetPanelMask & WidgetStateDetail::bit(panel)) != 0;
}

// visibility: Current preference, including the parent Cody flag.
// panel: Child identity whose effective visibility is requested.
constexpr bool widget_visible(WidgetVisibility visibility, CompanionPanel panel)
{
    return widget_visible(visibility, WidgetScreen::Cody) && widget_panel_enabled(visibility, panel);
}
#endif

// visibility: Editable preference. Remains unchanged if the proposal is rejected.
// screen: Top-level identity to change.
// enabled: Requested state; the permanent device page cannot be disabled in firmware.
constexpr bool set_widget_visibility(WidgetVisibility &visibility, WidgetScreen screen, bool enabled)
{
    const uint16_t bit = WidgetStateDetail::bit(screen) & kWidgetScreenMask;
    if (bit == 0 || !valid_widget_visibility(visibility)) {
        return false;
    }
    const uint16_t candidate = enabled ? visibility.screen_mask | bit : visibility.screen_mask & ~bit;
#if defined(CODY_DEVICE_UI_ONLY)
    if (screen == WidgetScreen::Device && !enabled) {
        return false;
    }
    visibility.screen_mask = candidate | WidgetStateDetail::bit(WidgetScreen::Device);
#else
    if (candidate == 0) {
        return false;
    }
    visibility.screen_mask = candidate;
#endif
    return true;
}

#if !defined(CODY_DEVICE_UI_ONLY)
// visibility: Editable preference. The child flag can change even while Cody is hidden.
// panel: Child identity to change.
// enabled: Requested state; Cody can be visible with no lower panel.
constexpr bool set_widget_visibility(WidgetVisibility &visibility, CompanionPanel panel, bool enabled)
{
    const uint8_t bit = WidgetStateDetail::bit(panel) & kWidgetPanelMask;
    if (bit == 0 || !valid_widget_visibility(visibility)) {
        return false;
    }
    visibility.panel_mask = enabled ? visibility.panel_mask | bit : visibility.panel_mask & ~bit;
    return true;
}
#endif

// visibility: Current preference.
// scope: All roots or the existing statistics-only entry from Options.
constexpr std::optional<WidgetScreen> first_visible_widget(WidgetVisibility visibility,
    WidgetScope scope = WidgetScope::All)
{
    return WidgetStateDetail::first(kWidgetScreens, [visibility, scope](WidgetScreen screen) {
        return (scope == WidgetScope::All || screen != WidgetScreen::Device) && widget_visible(visibility, screen);
    });
}

// visibility: Current preference.
// current: Root to advance from, even if it is no longer enabled.
// step: Direction through the visible roots, wrapping once.
// scope: All roots or the existing statistics-only entry from Options.
constexpr std::optional<WidgetScreen> next_visible_widget(WidgetVisibility visibility, WidgetScreen current,
    WidgetStep step = WidgetStep::Next, WidgetScope scope = WidgetScope::All)
{
    return WidgetStateDetail::next(kWidgetScreens, current, step, [visibility, scope](WidgetScreen screen) {
        return (scope == WidgetScope::All || screen != WidgetScreen::Device) && widget_visible(visibility, screen);
    });
}

#if !defined(CODY_DEVICE_UI_ONLY)
// visibility: Current preference. Returns no panel when Cody or all its children are hidden.
constexpr std::optional<CompanionPanel> first_visible_widget_panel(WidgetVisibility visibility)
{
    return WidgetStateDetail::first(kWidgetPanels, [visibility](CompanionPanel panel) {
        return widget_visible(visibility, panel);
    });
}

// visibility: Current preference.
// current: Child to advance from, even if it is no longer enabled.
// step: Direction through the visible children, wrapping once.
constexpr std::optional<CompanionPanel> next_visible_widget_panel(WidgetVisibility visibility,
    CompanionPanel current, WidgetStep step = WidgetStep::Next)
{
    return WidgetStateDetail::next(kWidgetPanels, current, step, [visibility](CompanionPanel panel) {
        return widget_visible(visibility, panel);
    });
}
#endif

// Display-worker state. Callers derive presentation and contact eligibility from this selection.
struct WidgetRouteState {
    WidgetVisibility visibility{};
    WidgetScreen selected_screen = WidgetScreen::Device;
#if !defined(CODY_DEVICE_UI_ONLY)
    CompanionPanel selected_panel = CompanionPanel::Home;
#endif
    std::optional<WidgetScreen> retained_screen;
    WidgetReturnRoute return_route = WidgetReturnRoute::None;
};

// route: Current and retained widget identities. Changes only after policy validation.
// visibility: Preference loaded or successfully persisted by the settings owner.
inline bool apply_widget_visibility(WidgetRouteState &route, WidgetVisibility visibility)
{
    if (!valid_widget_visibility(visibility)) {
        return false;
    }
    const auto fallback = first_visible_widget(visibility).value_or(WidgetScreen::Device);
    route.visibility = visibility;
    if (!widget_visible(visibility, route.selected_screen)) {
        route.selected_screen = fallback;
    }
    if (route.retained_screen && !widget_visible(visibility, *route.retained_screen)) {
        route.retained_screen = fallback;
    }
#if !defined(CODY_DEVICE_UI_ONLY)
    if (!widget_panel_enabled(visibility, route.selected_panel)) {
        const auto first_enabled = WidgetStateDetail::first(kWidgetPanels, [visibility](CompanionPanel panel) {
            return widget_panel_enabled(visibility, panel);
        });
        route.selected_panel = first_enabled.value_or(CompanionPanel::Home);
    }
#endif
    return true;
}

// route: Display-worker selection.
// screen: Visible top-level widget to select. Rejection leaves the selection intact.
constexpr bool select_widget_screen(WidgetRouteState &route, WidgetScreen screen)
{
    if (!widget_visible(route.visibility, screen)) {
        return false;
    }
    route.selected_screen = screen;
    return true;
}

#if !defined(CODY_DEVICE_UI_ONLY)
// route: Display-worker selection.
// panel: Effectively visible Cody child to select. Rejection leaves the selection intact.
constexpr bool select_widget_panel(WidgetRouteState &route, CompanionPanel panel)
{
    if (!widget_visible(route.visibility, panel)) {
        return false;
    }
    route.selected_panel = panel;
    return true;
}
#endif

} // namespace CODY
