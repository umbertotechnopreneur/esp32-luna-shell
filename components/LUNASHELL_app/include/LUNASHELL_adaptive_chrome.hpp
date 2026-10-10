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
 * Original file notice:
 *
 * ESP32 Luna Shell
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 ESP32 Luna Shell. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Author website: https://umbertogiacobbi.biz
 *
 *
 * VBWR E */


#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"
#include "LUNASHELL_adaptive_command_bar.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {

enum class AdaptiveChromeTiming : uint8_t {
    CommandArc,
    StatusArc,
    DisplayDim,
    ScreenIdle,
};

enum class DisplayIdleStage : uint8_t {
    Active,
    Dimmed,
    Off,
};

// Coordinates both chrome arcs and display power from one inactivity policy.
class AdaptiveChromeController final {
public:
    static constexpr uint32_t kPolicyPeriodMs = 200;

    [[nodiscard]] static std::span<const uint32_t> timeout_options(
        AdaptiveChromeTiming timing
    );

    // Bind to the already initialized global power-management policy.
    std::expected<void, std::string> begin();

    // Replace route commands while retaining the single mounted Command Arc.
    std::expected<void, std::string> configure_commands(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        std::span<const CommandSpec> commands
    );

    [[nodiscard]] std::optional<std::string_view> resolve_action(
        std::string_view slot_action
    ) const;

    // Publish activity; the next canonical snapshot owns every visual change.
    std::expected<void, std::string> note_activity(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Render the canonical power-management state without owning timers.
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

    // Re-arm policy after the app regains focus.
    std::expected<void, std::string> resume(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Stop animations and hit targets without changing the active route model.
    std::expected<void, std::string> quiesce(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Stop animations, release hit targets, and restore full display power.
    std::expected<void, std::string> shutdown(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    esp_err_t set_timeout_seconds(
        AdaptiveChromeTiming timing,
        uint32_t seconds
    );

    [[nodiscard]] uint32_t timeout_seconds(
        AdaptiveChromeTiming timing
    ) const;
    [[nodiscard]] size_t selected_index(
        AdaptiveChromeTiming timing
    ) const;
    [[nodiscard]] DisplayIdleStage display_idle_stage() const;
    [[nodiscard]] bool command_arc_visible() const;
    [[nodiscard]] bool status_arc_visible() const;

private:
    std::expected<void, std::string> set_status_visible(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool visible
    );
    std::expected<void, std::string> set_wake_surface(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool enabled
    );
    std::expected<void, std::string> update_wake_surface(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );
    std::expected<void, std::string> stop_status_animation(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );
    void status_animation_completed(uint32_t generation);

    AdaptiveCommandBar command_bar_;
    esp_brookesia::gui::SubscriptionId status_animation_id_ = 0;
    uint32_t status_animation_generation_ = 0;
    DisplayIdleStage display_idle_stage_ = DisplayIdleStage::Active;
    std::optional<bool> wake_surface_enabled_;
    bool active_ = false;
    bool fullscreen_active_ = false;
    bool command_target_visible_ = true;
    bool status_visible_ = true;
    bool status_target_visible_ = true;
};

}  // namespace LUNASHELL
