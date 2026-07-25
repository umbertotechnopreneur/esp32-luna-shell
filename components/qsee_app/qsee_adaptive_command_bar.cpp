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

#include "qsee_adaptive_command_bar.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "esp_log.h"
#include "esp_timer.h"
#include "qsee_settings.hpp"

namespace {

constexpr char kTag[] = "qsee_command_bar";
constexpr char kTimeoutSetting[] = "cmd_arc_s";
constexpr uint32_t kFadeDurationMs = 280;
constexpr float kAccelerationDeltaThresholdMps2 = 0.25F;
constexpr float kAngularVelocityThresholdDps = 2.5F;

// Accept only the timeout values exposed by the original Settings selector.
bool is_supported_timeout(uint32_t seconds)
{
    switch (seconds) {
    case 10:
    case 15:
    case 30:
    case 60:
    case 120:
    case 300:
    case 600:
        return true;
    default:
        return false;
    }
}

// Update one declared Brookesia binding and retain the failing path in the error.
std::expected<void, std::string> set_binding(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::string_view path,
    std::string_view key,
    std::string_view value
)
{
    auto result = gui.set_binding_value(path, key, std::string(value));
    if (!result) {
        return std::unexpected(
            "Failed to update adaptive chrome '" + std::string(path) +
            "/" + std::string(key) + "': " + result.error()
        );
    }
    return {};
}

}  // namespace

namespace qsee {

// Load a valid saved timeout or explicitly create the first-boot value.
std::expected<void, std::string> AdaptiveCommandBar::begin()
{
    auto &settings = UserSettingsService::instance();
    esp_err_t result = settings.isInitialized()
        ? ESP_OK
        : settings.initialize();
    if (result != ESP_OK) {
        return std::unexpected(
            "Adaptive chrome settings initialization failed: " +
            std::string(esp_err_to_name(result))
        );
    }

    uint32_t saved_seconds = 0;
    result = settings.getUInt32(kTimeoutSetting, &saved_seconds);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        saved_seconds = kDefaultTimeoutSeconds;
        result = settings.setUInt32(kTimeoutSetting, saved_seconds);
        if (result != ESP_OK) {
            return std::unexpected(
                "Adaptive chrome first-boot timeout persistence failed: " +
                std::string(esp_err_to_name(result))
            );
        }
        ESP_LOGI(
            kTag,
            "Persisted explicit first-boot timeout: %lu s",
            static_cast<unsigned long>(saved_seconds)
        );
    } else if (result != ESP_OK) {
        return std::unexpected(
            "Adaptive chrome timeout read failed: " +
            std::string(esp_err_to_name(result))
        );
    }

    if (!is_supported_timeout(saved_seconds)) {
        return std::unexpected(
            "Adaptive chrome timeout is invalid: " +
            std::to_string(saved_seconds)
        );
    }
    timeout_seconds_ = saved_seconds;
    return {};
}

// Replace route targets and make every new route fully visible before refresh.
std::expected<void, std::string> AdaptiveCommandBar::activate(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::span<const std::string_view> controls,
    std::span<const std::string_view> visuals,
    std::string_view wake_path
)
{
    stop_animations(gui);
    controls_.assign(controls.begin(), controls.end());
    visuals_.assign(visuals.begin(), visuals.end());
    wake_path_.assign(wake_path);
    active_ = !controls_.empty() || !visuals_.empty();
    visible_ = true;
    animation_target_visible_ = true;
    last_activity_us_ = esp_timer_get_time();
    last_imu_sample_us_ = 0;
    motion_baseline_ready_ = false;

    for (const std::string &path : visuals_) {
        if (auto result = set_binding(gui, path, "opacity", "255"); !result) {
            return result;
        }
    }
    for (const std::string &path : controls_) {
        if (auto result = set_binding(gui, path, "opacity", "255"); !result) {
            return result;
        }
    }
    return set_hit_testing(gui, true);
}

// Reset inactivity and reverse an in-flight fade before dispatching the action.
std::expected<void, std::string> AdaptiveCommandBar::note_activity(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    if (!active_) {
        return {};
    }
    last_activity_us_ = esp_timer_get_time();
    if (visible_ && animation_target_visible_ && pending_animations_ == 0) {
        return {};
    }
    if (auto result = set_hit_testing(gui, true); !result) {
        return result;
    }
    return animate_to(gui, true);
}

// Combine inactivity and canonical IMU motion without reading LVGL or hardware.
std::expected<void, std::string> AdaptiveCommandBar::update(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    const DeviceSnapshot &snapshot
)
{
    const bool motion_activity = detect_motion(snapshot.imu);
    if (!active_) {
        return {};
    }
    if (motion_activity) {
        return note_activity(gui);
    }

    const int64_t timeout_us =
        static_cast<int64_t>(timeout_seconds_) * 1000000LL;
    if (esp_timer_get_time() - last_activity_us_ >= timeout_us &&
        animation_target_visible_) {
        return animate_to(gui, false);
    }
    return {};
}

// Invalidate callbacks before the Brookesia document can disappear.
void AdaptiveCommandBar::shutdown(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    stop_animations(gui);
    controls_.clear();
    visuals_.clear();
    wake_path_.clear();
    active_ = false;
}

// Reject undeclared values and keep runtime state unchanged if persistence fails.
esp_err_t AdaptiveCommandBar::set_timeout_seconds(uint32_t seconds)
{
    if (!is_supported_timeout(seconds)) {
        ESP_LOGE(
            kTag,
            "Adaptive chrome timeout is unsupported: %lu s",
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result =
        UserSettingsService::instance().setUInt32(kTimeoutSetting, seconds);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Adaptive chrome timeout persistence failed (%s)",
            esp_err_to_name(result)
        );
        return result;
    }
    timeout_seconds_ = seconds;
    last_activity_us_ = esp_timer_get_time();
    ESP_LOGI(
        kTag,
        "Adaptive chrome timeout updated: %lu s",
        static_cast<unsigned long>(seconds)
    );
    return ESP_OK;
}

uint32_t AdaptiveCommandBar::timeout_seconds() const
{
    return timeout_seconds_;
}

// Reject stale, unavailable, or non-finite samples before comparing motion.
bool AdaptiveCommandBar::detect_motion(const ImuSnapshot &imu)
{
    const bool valid =
        imu.health == DeviceHealth::Ready &&
        imu.last_error == ESP_OK &&
        imu.data_valid &&
        !imu.data_stale &&
        imu.last_success_at_us > 0;
    if (!valid) {
        motion_baseline_ready_ = false;
        last_imu_sample_us_ = 0;
        return false;
    }
    if (imu.last_success_at_us == last_imu_sample_us_) {
        return false;
    }
    last_imu_sample_us_ = imu.last_success_at_us;

    for (size_t axis = 0; axis < 3U; ++axis) {
        if (!std::isfinite(imu.acceleration_mps2[axis]) ||
            !std::isfinite(imu.angular_velocity_dps[axis])) {
            motion_baseline_ready_ = false;
            return false;
        }
    }

    if (!motion_baseline_ready_) {
        std::copy(
            std::begin(imu.acceleration_mps2),
            std::end(imu.acceleration_mps2),
            last_acceleration_mps2_
        );
        motion_baseline_ready_ = true;
        return false;
    }

    float acceleration_delta_squared = 0.0F;
    float angular_velocity_squared = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration_delta =
            imu.acceleration_mps2[axis] - last_acceleration_mps2_[axis];
        acceleration_delta_squared +=
            acceleration_delta * acceleration_delta;
        angular_velocity_squared +=
            imu.angular_velocity_dps[axis] * imu.angular_velocity_dps[axis];
        last_acceleration_mps2_[axis] = imu.acceleration_mps2[axis];
    }

    return acceleration_delta_squared >=
               kAccelerationDeltaThresholdMps2 *
                   kAccelerationDeltaThresholdMps2 ||
           angular_velocity_squared >=
               kAngularVelocityThresholdDps *
                   kAngularVelocityThresholdDps;
}

// Start one opacity animation per path and finalize only after the full set completes.
std::expected<void, std::string> AdaptiveCommandBar::animate_to(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool visible
)
{
    if (animation_target_visible_ == visible && pending_animations_ != 0) {
        return {};
    }
    if (visible_ == visible && pending_animations_ == 0) {
        return visible ? set_hit_testing(gui, true) : set_hit_testing(gui, false);
    }

    stop_animations(gui);
    animation_target_visible_ = visible;
    if (visible) {
        if (auto result = set_hit_testing(gui, true); !result) {
            return result;
        }
    }

    pending_animations_ = controls_.size() + visuals_.size();
    if (pending_animations_ == 0) {
        visible_ = visible;
        return set_hit_testing(gui, visible);
    }

    const uint32_t generation = animation_generation_;
    const auto start_path =
        [this, &gui, generation, visible](const std::string &path)
        -> std::expected<void, std::string> {
        esp_brookesia::gui::Animation animation{
            .id = "qsee_adaptive_fade",
            .trigger = esp_brookesia::gui::AnimationTrigger::Manual,
            .property = esp_brookesia::gui::AnimationProperty::Opacity,
            .from_mode = esp_brookesia::gui::AnimationValueMode::Current,
            .from = 0,
            .to_mode = esp_brookesia::gui::AnimationValueMode::Absolute,
            .to = visible ? 255 : 0,
            .duration = static_cast<int32_t>(kFadeDurationMs),
            .delay = 0,
            .easing = esp_brookesia::gui::AnimationEasing::EaseInOut,
            .repeat = 0,
            .playback = false,
        };
        auto result = gui.start_view_animation_with_result(
            path,
            animation,
            [this, gui, generation]() {
                animation_completed(gui, generation);
            }
        );
        if (!result) {
            return std::unexpected(
                "Failed to animate adaptive chrome '" + path +
                "': " + result.error()
            );
        }
        animation_ids_.push_back(result->subscription_id);
        return {};
    };

    for (const std::string &path : visuals_) {
        if (auto result = start_path(path); !result) {
            stop_animations(gui);
            return result;
        }
    }
    for (const std::string &path : controls_) {
        if (auto result = start_path(path); !result) {
            stop_animations(gui);
            return result;
        }
    }

    if (pending_animations_ == 0) {
        animation_ids_.clear();
    }
    return {};
}

// Keep hidden commands inert and expose one transparent wake target instead.
std::expected<void, std::string> AdaptiveCommandBar::set_hit_testing(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool chrome_visible
)
{
    if (!chrome_visible) {
        for (const std::string &path : controls_) {
            if (auto result = set_binding(gui, path, "clickable", "false"); !result) {
                return result;
            }
        }
    }

    if (!wake_path_.empty()) {
        if (auto result = set_binding(
                gui,
                wake_path_,
                "hidden",
                chrome_visible ? "true" : "false"
            ); !result) {
            return result;
        }
        if (auto result = set_binding(
                gui,
                wake_path_,
                "clickable",
                chrome_visible ? "false" : "true"
            ); !result) {
            return result;
        }
    }

    if (chrome_visible) {
        for (const std::string &path : controls_) {
            if (auto result = set_binding(gui, path, "clickable", "true"); !result) {
                return result;
            }
        }
    }
    return {};
}

// Stop subscriptions and invalidate every completion callback from the old generation.
void AdaptiveCommandBar::stop_animations(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    ++animation_generation_;
    for (esp_brookesia::gui::SubscriptionId animation_id : animation_ids_) {
        if (animation_id != 0) {
            (void)gui.stop_animation(animation_id);
        }
    }
    animation_ids_.clear();
    pending_animations_ = 0;
}

// Disable commands only after every path reaches zero opacity.
void AdaptiveCommandBar::animation_completed(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    uint32_t generation
)
{
    if (generation != animation_generation_ || pending_animations_ == 0) {
        return;
    }
    --pending_animations_;
    if (pending_animations_ != 0) {
        return;
    }

    visible_ = animation_target_visible_;
    animation_ids_.clear();
    if (!visible_) {
        if (auto result = set_hit_testing(gui, false); !result) {
            ESP_LOGE(kTag, "%s", result.error().c_str());
        }
    }
}

}  // namespace qsee
