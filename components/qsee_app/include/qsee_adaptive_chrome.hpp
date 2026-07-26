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
#include "qsee_adaptive_command_bar.hpp"
#include "qsee_device_snapshot.hpp"

namespace qsee {

enum class AdaptiveChromeTiming : uint8_t {
    CommandArc,
    StatusArc,
    DisplayDim,
    DisplayOff,
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
    static constexpr std::array<uint32_t, 7> kArcTimeouts = {
        10, 15, 30, 60, 120, 300, 600,
    };
    static constexpr std::array<uint32_t, 5> kDimTimeouts = {
        15, 30, 60, 120, 0,
    };
    static constexpr std::array<uint32_t, 6> kOffDelays = {
        15, 30, 60, 120, 300, 0,
    };

    // Load every persisted timing and create explicit first-boot values.
    std::expected<void, std::string> begin();

    // Replace route commands while retaining the single mounted Command Arc.
    std::expected<void, std::string> configure_commands(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        std::span<const CommandSpec> commands
    );

    [[nodiscard]] std::optional<std::string_view> resolve_action(
        std::string_view slot_action
    ) const;

    // Restore both arcs and full brightness after touch or valid movement.
    std::expected<void, std::string> note_activity(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Advance independent arc and display timers from one canonical snapshot.
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

    // Re-arm policy after the app regains focus.
    std::expected<void, std::string> resume(
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

private:
    bool detect_motion(const ImuSnapshot &imu);

    std::expected<void, std::string> set_status_visible(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool visible
    );
    std::expected<void, std::string> set_display_idle_stage(
        DisplayIdleStage stage
    );
    std::expected<void, std::string> set_wake_surface(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool enabled
    );
    std::expected<void, std::string> update_wake_surface(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );
    void stop_status_animation(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );
    void status_animation_completed(uint32_t generation);

    AdaptiveCommandBar command_bar_;
    esp_brookesia::gui::SubscriptionId status_animation_id_ = 0;
    uint32_t command_timeout_seconds_ = 10;
    uint32_t status_timeout_seconds_ = 30;
    uint32_t dim_timeout_seconds_ = 30;
    uint32_t off_delay_seconds_ = 60;
    uint32_t status_animation_generation_ = 0;
    int64_t last_activity_us_ = 0;
    int64_t last_imu_sample_us_ = 0;
    float last_acceleration_mps2_[3] = {};
    DisplayIdleStage display_idle_stage_ = DisplayIdleStage::Active;
    bool active_ = false;
    bool status_visible_ = true;
    bool status_target_visible_ = true;
    bool motion_baseline_ready_ = false;
};

}  // namespace qsee
