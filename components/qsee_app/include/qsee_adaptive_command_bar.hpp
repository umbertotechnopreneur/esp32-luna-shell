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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"
#include "qsee_palette.hpp"

namespace qsee {

struct CommandSpec {
    std::string_view icon;
    std::string_view action;
    palette::ActionRole role = palette::ActionRole::Primary;
    bool is_main = false;
};

// Owns one persistent, route-configurable command arc under Brookesia.
class AdaptiveCommandBar final {
public:
    static constexpr size_t kSlotCount = 5;
    static constexpr int kMainDiameterDp = 61;
    static constexpr int kDiameterStepDp = 6;
    static constexpr int kButtonGapDp = 9;

    // Replace the route model while retaining the same mounted visual component.
    std::expected<void, std::string> activate(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        std::span<const CommandSpec> commands,
        bool visible
    );

    // Resolve one persistent slot action to the active route command.
    [[nodiscard]] std::optional<std::string_view> resolve_action(
        std::string_view slot_action
    ) const;

    // Fade the mounted command component without recreating route controls.
    std::expected<void, std::string> set_visible(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool visible
    );

    // Stop motion and input before the renderer enters device standby.
    std::expected<void, std::string> quiesce(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Stop active animations before the route document or app is torn down.
    std::expected<void, std::string> shutdown(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    [[nodiscard]] bool has_commands() const;
    [[nodiscard]] bool target_visible() const;

private:
    // Animate the one mounted command component to the target opacity.
    std::expected<void, std::string> animate_to(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool visible
    );

    // Apply hit testing only at stable transition boundaries.
    std::expected<void, std::string> set_hit_testing(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool chrome_visible
    );

    // Cancel the outstanding Brookesia animation subscription.
    std::expected<void, std::string> stop_animations(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Finalize the one component transition.
    void animation_completed(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        uint32_t generation
    );

    std::array<std::string, kSlotCount> actions_{};
    size_t command_count_ = 0;
    esp_brookesia::gui::SubscriptionId animation_id_ = 0;
    uint32_t animation_generation_ = 0;
    std::optional<bool> hit_testing_enabled_;
    bool active_ = false;
    bool visible_ = true;
    bool animation_target_visible_ = true;
};

}  // namespace qsee
