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
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "qsee_settings.hpp"

namespace {

constexpr char kTag[] = "qsee_command_bar";
constexpr char kTimeoutSetting[] = "cmd_arc_s";
constexpr uint32_t kFadeDurationMs = 280;
constexpr float kAccelerationDeltaThresholdMps2 = 0.25F;
constexpr float kAngularVelocityThresholdDps = 2.5F;
constexpr std::string_view kCommandRoot =
    "/qsee_chrome_screen/chrome/commands";
constexpr std::string_view kWakePath =
    "/qsee_chrome_screen/chrome/commands/wake";
constexpr std::array<std::string_view, qsee::AdaptiveCommandBar::kSlotCount>
    kSlotActions = {
        "qsee.command.0",
        "qsee.command.1",
        "qsee.command.2",
        "qsee.command.3",
        "qsee.command.4",
        "qsee.command.5",
    };

struct ArcPoint {
    int x;
    int y;
};

constexpr std::array<ArcPoint, 1> kLayout1 = {{{206, 362}}};
constexpr std::array<ArcPoint, 2> kLayout2 = {{{177, 358}, {235, 358}}};
constexpr std::array<ArcPoint, 3> kLayout3 = {{
    {148, 350},
    {206, 362},
    {264, 350},
}};
constexpr std::array<ArcPoint, 4> kLayout4 = {{
    {119, 342},
    {177, 358},
    {235, 358},
    {293, 342},
}};
constexpr std::array<ArcPoint, 5> kLayout5 = {{
    {90, 330},
    {148, 348},
    {206, 358},
    {264, 348},
    {322, 330},
}};
constexpr std::array<ArcPoint, 6> kLayout6 = {{
    {56, 330},
    {114, 344},
    {172, 360},
    {230, 360},
    {288, 344},
    {346, 330},
}};

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

ArcPoint arc_point(size_t count, size_t index)
{
    switch (count) {
    case 1:
        return kLayout1[index];
    case 2:
        return kLayout2[index];
    case 3:
        return kLayout3[index];
    case 4:
        return kLayout4[index];
    case 5:
        return kLayout5[index];
    case 6:
        return kLayout6[index];
    default:
        return {0, 0};
    }
}

std::string slot_path(size_t index)
{
    return std::string(kCommandRoot) + "/slot" + std::to_string(index);
}

void add_binding(
    std::vector<esp_brookesia::gui::BindingValueUpdate> &updates,
    std::string_view path,
    std::string_view key,
    std::string_view value
)
{
    updates.push_back({
        .absolute_path = std::string(path),
        .key = std::string(key),
        .value = std::string(value),
    });
}

}  // namespace

namespace qsee {

std::expected<void, std::string> AdaptiveCommandBar::begin()
{
    auto &settings = UserSettingsService::instance();
    esp_err_t result = settings.isInitialized()
        ? ESP_OK
        : settings.initialize();
    if (result != ESP_OK) {
        return std::unexpected(
            "Adaptive command settings initialization failed: " +
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
                "Adaptive command first-boot timeout persistence failed: " +
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
            "Adaptive command timeout read failed: " +
            std::string(esp_err_to_name(result))
        );
    }

    if (!is_supported_timeout(saved_seconds)) {
        return std::unexpected(
            "Adaptive command timeout is invalid: " +
            std::to_string(saved_seconds)
        );
    }
    timeout_seconds_ = saved_seconds;
    return {};
}

std::expected<void, std::string> AdaptiveCommandBar::activate(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::span<const CommandSpec> commands
)
{
    if (commands.size() > kSlotCount) {
        return std::unexpected(
            "Adaptive command arc rejected " +
            std::to_string(commands.size()) +
            " commands; maximum is " + std::to_string(kSlotCount)
        );
    }
    for (const CommandSpec &command : commands) {
        if (command.icon.empty() || command.action.empty()) {
            return std::unexpected(
                "Adaptive command arc rejected an empty icon or action"
            );
        }
    }

    stop_animations(gui);
    command_count_ = commands.size();
    for (std::string &action : actions_) {
        action.clear();
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(80);
    add_binding(
        updates,
        std::string(kCommandRoot),
        "hidden",
        commands.empty() ? "true" : "false"
    );
    add_binding(
        updates,
        std::string(kCommandRoot),
        "opacity",
        "255"
    );
    add_binding(updates, std::string(kWakePath), "hidden", "true");
    add_binding(updates, std::string(kWakePath), "clickable", "false");

    for (size_t index = 0; index < kSlotCount; ++index) {
        const std::string path = slot_path(index);
        const bool present = index < commands.size();
        add_binding(
            updates,
            path,
            "hidden",
            present ? "false" : "true"
        );
        add_binding(
            updates,
            path,
            "clickable",
            present ? "true" : "false"
        );
        if (!present) {
            continue;
        }

        const CommandSpec &command = commands[index];
        actions_[index].assign(command.action);
        const ArcPoint point = arc_point(commands.size(), index);
        const palette::ActionColors &colors =
            palette::action_colors(command.role);
        add_binding(updates, path, "x", std::to_string(point.x));
        add_binding(updates, path, "y", std::to_string(point.y));
        add_binding(updates, path, "top", colors.top);
        add_binding(updates, path, "bottom", colors.bottom);
        add_binding(updates, path, "border", colors.border);
        add_binding(updates, path, "glow", colors.glow);
        add_binding(updates, path, "pressed_top", colors.pressed_top);
        add_binding(updates, path, "pressed_bottom", colors.pressed_bottom);
        add_binding(updates, path, "pressed_border", colors.pressed_border);
        add_binding(
            updates,
            path + "/icon",
            "icon",
            command.icon
        );
        add_binding(
            updates,
            path + "/icon",
            "color",
            palette::kButtonText
        );
    }

    auto result = gui.set_binding_values(updates);
    if (!result) {
        command_count_ = 0;
        for (std::string &action : actions_) {
            action.clear();
        }
        active_ = false;
        return std::unexpected(
            "Failed to configure reusable adaptive command arc: " +
            result.error()
        );
    }

    active_ = !commands.empty();
    visible_ = true;
    animation_target_visible_ = true;
    last_activity_us_ = esp_timer_get_time();
    last_imu_sample_us_ = 0;
    motion_baseline_ready_ = false;
    return {};
}

std::optional<std::string_view> AdaptiveCommandBar::resolve_action(
    std::string_view slot_action
) const
{
    for (size_t index = 0; index < kSlotActions.size(); ++index) {
        if (slot_action == kSlotActions[index] &&
            index < command_count_ &&
            !actions_[index].empty()) {
            return actions_[index];
        }
    }
    return std::nullopt;
}

std::expected<void, std::string> AdaptiveCommandBar::note_activity(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    if (!active_) {
        return {};
    }
    last_activity_us_ = esp_timer_get_time();
    if (visible_ && animation_target_visible_ && animation_id_ == 0) {
        return {};
    }
    if (auto result = set_hit_testing(gui, true); !result) {
        return result;
    }
    return animate_to(gui, true);
}

std::expected<void, std::string> AdaptiveCommandBar::update(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    const DeviceSnapshot &snapshot
)
{
    if (!active_) {
        return {};
    }
    if (detect_motion(snapshot.imu)) {
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

void AdaptiveCommandBar::shutdown(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    stop_animations(gui);
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve((kSlotCount * 2U) + 3U);
    add_binding(updates, std::string(kCommandRoot), "hidden", "true");
    add_binding(updates, std::string(kWakePath), "hidden", "true");
    add_binding(updates, std::string(kWakePath), "clickable", "false");
    for (size_t index = 0; index < kSlotCount; ++index) {
        const std::string path = slot_path(index);
        add_binding(updates, path, "hidden", "true");
        add_binding(updates, path, "clickable", "false");
        actions_[index].clear();
    }
    auto result = gui.set_binding_values(updates);
    if (!result) {
        ESP_LOGE(
            kTag,
            "Failed to hide adaptive command arc during shutdown: %s",
            result.error().c_str()
        );
    }
    command_count_ = 0;
    active_ = false;
    visible_ = true;
    animation_target_visible_ = true;
}

esp_err_t AdaptiveCommandBar::set_timeout_seconds(uint32_t seconds)
{
    if (!is_supported_timeout(seconds)) {
        ESP_LOGE(
            kTag,
            "Adaptive command timeout is unsupported: %lu s",
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result =
        UserSettingsService::instance().setUInt32(kTimeoutSetting, seconds);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Adaptive command timeout persistence failed (%s)",
            esp_err_to_name(result)
        );
        return result;
    }
    timeout_seconds_ = seconds;
    last_activity_us_ = esp_timer_get_time();
    ESP_LOGI(
        kTag,
        "Adaptive command timeout updated: %lu s",
        static_cast<unsigned long>(seconds)
    );
    return ESP_OK;
}

uint32_t AdaptiveCommandBar::timeout_seconds() const
{
    return timeout_seconds_;
}

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

std::expected<void, std::string> AdaptiveCommandBar::animate_to(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool visible
)
{
    if (animation_target_visible_ == visible && animation_id_ != 0) {
        return {};
    }
    if (visible_ == visible && animation_id_ == 0) {
        return set_hit_testing(gui, visible);
    }

    stop_animations(gui);
    animation_target_visible_ = visible;
    if (visible) {
        if (auto result = set_hit_testing(gui, true); !result) {
            return result;
        }
    }

    const uint32_t generation = animation_generation_;
    esp_brookesia::gui::Animation animation{
        .id = "qsee_adaptive_command_fade",
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
        kCommandRoot,
        animation,
        [this, gui, generation]() {
            animation_completed(gui, generation);
        }
    );
    if (!result) {
        return std::unexpected(
            "Failed to animate reusable adaptive command arc: " +
            result.error()
        );
    }
    animation_id_ = result->subscription_id;
    return {};
}

std::expected<void, std::string> AdaptiveCommandBar::set_hit_testing(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool chrome_visible
)
{
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(command_count_ + 2U);
    for (size_t index = 0; index < command_count_; ++index) {
        add_binding(
            updates,
            slot_path(index),
            "clickable",
            chrome_visible ? "true" : "false"
        );
    }
    add_binding(
        updates,
        std::string(kWakePath),
        "hidden",
        chrome_visible || !active_ ? "true" : "false"
    );
    add_binding(
        updates,
        std::string(kWakePath),
        "clickable",
        chrome_visible || !active_ ? "false" : "true"
    );
    auto result = gui.set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update adaptive command hit testing: " +
            result.error()
        );
    }
    return {};
}

void AdaptiveCommandBar::stop_animations(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    ++animation_generation_;
    if (animation_id_ != 0) {
        (void)gui.stop_animation(animation_id_);
        animation_id_ = 0;
    }
}

void AdaptiveCommandBar::animation_completed(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    uint32_t generation
)
{
    if (generation != animation_generation_) {
        return;
    }
    animation_id_ = 0;
    visible_ = animation_target_visible_;
    if (!visible_) {
        if (auto result = set_hit_testing(gui, false); !result) {
            ESP_LOGE(kTag, "%s", result.error().c_str());
        }
    }
}

}  // namespace qsee
