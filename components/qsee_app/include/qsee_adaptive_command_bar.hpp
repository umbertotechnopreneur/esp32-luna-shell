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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/system_core.hpp"
#include "qsee_device_snapshot.hpp"

namespace qsee {

// Synchronize passive chrome and lower commands without bypassing Brookesia.
class AdaptiveCommandBar final {
public:
    static constexpr uint32_t kDefaultTimeoutSeconds = 10;

    // Load the persisted timeout and create an explicit first-boot value if absent.
    std::expected<void, std::string> begin();

    // Register the active route paths and reset them to a visible interactive state.
    std::expected<void, std::string> activate(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        std::span<const std::string_view> controls,
        std::span<const std::string_view> visuals,
        std::string_view wake_path
    );

    // Wake hidden chrome before handling a user action.
    std::expected<void, std::string> note_activity(
        const esp_brookesia::system::core::AppGuiRuntime &gui
    );

    // Advance inactivity and motion policy from the canonical device snapshot.
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

    // Stop active animations before the route document or app is torn down.
    void shutdown(const esp_brookesia::system::core::AppGuiRuntime &gui);

    // Persist one supported inactivity timeout before applying it to the controller.
    esp_err_t set_timeout_seconds(uint32_t seconds);

    [[nodiscard]] uint32_t timeout_seconds() const;

private:
    // Compare consecutive validated IMU samples without reading hardware directly.
    bool detect_motion(const ImuSnapshot &imu);

    // Animate all registered paths to one shared opacity.
    std::expected<void, std::string> animate_to(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool visible
    );

    // Apply hit testing only at stable transition boundaries.
    std::expected<void, std::string> set_hit_testing(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool chrome_visible
    );

    // Cancel every outstanding Brookesia animation subscription.
    void stop_animations(const esp_brookesia::system::core::AppGuiRuntime &gui);

    // Finalize a transition only after all synchronized paths complete.
    void animation_completed(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        uint32_t generation
    );

    std::vector<std::string> controls_;
    std::vector<std::string> visuals_;
    std::vector<esp_brookesia::gui::SubscriptionId> animation_ids_;
    std::string wake_path_;
    uint32_t timeout_seconds_ = kDefaultTimeoutSeconds;
    uint32_t animation_generation_ = 0;
    size_t pending_animations_ = 0;
    int64_t last_activity_us_ = 0;
    int64_t last_imu_sample_us_ = 0;
    float last_acceleration_mps2_[3] = {};
    bool active_ = false;
    bool visible_ = true;
    bool animation_target_visible_ = true;
    bool motion_baseline_ready_ = false;
};

}  // namespace qsee
