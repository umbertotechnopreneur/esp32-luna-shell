/*
 * LUNASHELL.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 LUNASHELL.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * LUNASHELL.ai: https://LUNASHELL.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@LUNASHELL.ai
 */

// Shared color palette and visual constants for LUNASHELL UI surfaces.
// Keeps the app look consistent across screens and widgets.

#pragma once

#include <cstdint>
#include <string_view>

namespace LUNASHELL::palette {

enum class ActionRole : uint8_t {
    Primary,
    Success,
    Warning,
    Danger,
    Neutral,
};

struct ActionColors {
    std::string_view top;
    std::string_view bottom;
    std::string_view border;
    std::string_view glow;
    std::string_view pressed_top;
    std::string_view pressed_bottom;
    std::string_view pressed_border;
};

inline constexpr ActionColors kPrimaryAction{
    .top = "#247fd6",
    .bottom = "#0a346b",
    .border = "#65c8ff",
    .glow = "#0b55d9",
    .pressed_top = "#1765ad",
    .pressed_bottom = "#082955",
    .pressed_border = "#9cddff",
};

inline constexpr ActionColors kSuccessAction{
    .top = "#0ab49d",
    .bottom = "#045a51",
    .border = "#38f1d7",
    .glow = "#009c9d",
    .pressed_top = "#078f7e",
    .pressed_bottom = "#03473f",
    .pressed_border = "#7dffe9",
};

inline constexpr ActionColors kWarningAction{
    .top = "#e58b11",
    .bottom = "#754008",
    .border = "#ffc45c",
    .glow = "#d97706",
    .pressed_top = "#bd6e08",
    .pressed_bottom = "#603407",
    .pressed_border = "#ffe09b",
};

inline constexpr ActionColors kDangerAction{
    .top = "#e54b59",
    .bottom = "#741827",
    .border = "#ff7185",
    .glow = "#d92d50",
    .pressed_top = "#b93643",
    .pressed_bottom = "#5b121f",
    .pressed_border = "#ffabba",
};

inline constexpr ActionColors kNeutralAction{
    .top = "#58778d",
    .bottom = "#203443",
    .border = "#86aabd",
    .glow = "#35596d",
    .pressed_top = "#456174",
    .pressed_bottom = "#192a36",
    .pressed_border = "#b2cad6",
};

constexpr const ActionColors &action_colors(ActionRole role)
{
    switch (role) {
    case ActionRole::Primary:
        return kPrimaryAction;
    case ActionRole::Success:
        return kSuccessAction;
    case ActionRole::Warning:
        return kWarningAction;
    case ActionRole::Danger:
        return kDangerAction;
    case ActionRole::Neutral:
        return kNeutralAction;
    }
    return kNeutralAction;
}

inline constexpr std::string_view kButtonText = "#ffffff";

inline constexpr std::string_view kStatusReady = "#20dfc1";
inline constexpr std::string_view kStatusWarning = "#f0b44d";
inline constexpr std::string_view kStatusMissing = "#eb4d5c";
inline constexpr std::string_view kStatusFault = "#ff3859";
inline constexpr std::string_view kStatusMuted = "#617f91";
inline constexpr std::string_view kStatusCharge = "#f4ffff";

}  // namespace LUNASHELL::palette
