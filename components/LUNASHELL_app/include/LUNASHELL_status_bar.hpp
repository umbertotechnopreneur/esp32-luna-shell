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
#include <optional>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {

// Owns the complete snapshot-to-visual policy for the persistent top ribbon.
class LUNASHELLStatusBar final {
public:
    void invalidate();
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

private:
    struct VisualState {
        std::string_view usb_color;
        std::string_view battery_color;
        std::string_view sd_color;
        std::string_view battery_icon;
        bool battery_charging;

        bool operator==(const VisualState &) const = default;
    };

    std::optional<VisualState> visual_state_;
};

}  // namespace LUNASHELL
