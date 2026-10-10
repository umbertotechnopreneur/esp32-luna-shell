/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <expected>
#include <string>

#include "brookesia/system_core.hpp"

namespace LUNASHELL {

// Controls the single declarative Back/Next pair owned by the app chrome.
class EdgeNavigation final {
public:
    std::expected<void, std::string> configure(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool back_visible,
        bool next_visible
    ) const;
};

}  // namespace LUNASHELL
