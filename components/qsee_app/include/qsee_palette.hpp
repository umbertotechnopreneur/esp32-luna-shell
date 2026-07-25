/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

// Shared color palette and visual constants for QSee UI surfaces.
// Keeps the app look consistent across screens and widgets.

#pragma once

#include <cstdint>

namespace qsee::palette {

constexpr uint32_t kTealTop = 0x20B486;
constexpr uint32_t kTealBottom = 0x087B68;
constexpr uint32_t kTealPressedTop = 0x159A73;
constexpr uint32_t kTealPressedBottom = 0x056252;

constexpr uint32_t kNeutralTop = 0x70879B;
constexpr uint32_t kNeutralBottom = 0x3E566B;
constexpr uint32_t kNeutralPressedTop = 0x5C7184;
constexpr uint32_t kNeutralPressedBottom = 0x304657;

constexpr uint32_t kButtonText = 0xFFFFFF;

}  // namespace qsee::palette
