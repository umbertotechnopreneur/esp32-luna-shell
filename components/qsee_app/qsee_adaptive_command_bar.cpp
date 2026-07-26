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
#include <string>
#include <vector>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "qsee_command_arc";
constexpr uint32_t kFadeDurationMs = 280;
constexpr int kDisplayCenterXDp = 233;
constexpr int kMainCenterYDp = 389;
constexpr int kArcRiseLinearDp = 10;
constexpr int kArcRiseQuadraticDp = 5;
constexpr std::string_view kCommandRoot =
    "/qsee_chrome_screen/chrome/commands";
constexpr std::array<std::string_view, qsee::AdaptiveCommandBar::kSlotCount>
    kSlotActions = {
        "qsee.command.0",
        "qsee.command.1",
        "qsee.command.2",
        "qsee.command.3",
        "qsee.command.4",
    };

struct CommandGeometry {
    int x;
    int y;
    int diameter;
};

int command_diameter(size_t main_index, size_t index)
{
    const int distance = std::abs(
        static_cast<int>(index) - static_cast<int>(main_index)
    );
    return qsee::AdaptiveCommandBar::kMainDiameterDp -
           (distance * qsee::AdaptiveCommandBar::kDiameterStepDp);
}

int command_center_y(size_t main_index, size_t index)
{
    const int distance = std::abs(
        static_cast<int>(index) - static_cast<int>(main_index)
    );
    return kMainCenterYDp -
           (kArcRiseLinearDp * distance) -
           (kArcRiseQuadraticDp * distance * distance);
}

int horizontal_center_delta(
    int first_diameter,
    int second_diameter,
    int vertical_delta
)
{
    const int target_distance =
        (first_diameter / 2) +
        (second_diameter / 2) +
        qsee::AdaptiveCommandBar::kButtonGapDp;
    const int horizontal_squared =
        (target_distance * target_distance) -
        (vertical_delta * vertical_delta);
    return static_cast<int>(
        std::lround(std::sqrt(static_cast<double>(horizontal_squared)))
    );
}

CommandGeometry command_geometry(size_t count, size_t index)
{
    const size_t main_index = count / 2U;
    std::array<int, qsee::AdaptiveCommandBar::kSlotCount> center_x{};
    std::array<int, qsee::AdaptiveCommandBar::kSlotCount> center_y{};
    std::array<int, qsee::AdaptiveCommandBar::kSlotCount> diameter{};

    for (size_t slot = 0; slot < count; ++slot) {
        diameter[slot] = command_diameter(main_index, slot);
        center_y[slot] = command_center_y(main_index, slot);
    }
    center_x[main_index] = kDisplayCenterXDp;

    for (size_t slot = main_index; slot > 0; --slot) {
        const int vertical_delta =
            std::abs(center_y[slot] - center_y[slot - 1U]);
        center_x[slot - 1U] =
            center_x[slot] -
            horizontal_center_delta(
                diameter[slot],
                diameter[slot - 1U],
                vertical_delta
            );
    }
    for (size_t slot = main_index + 1U; slot < count; ++slot) {
        const int vertical_delta =
            std::abs(center_y[slot] - center_y[slot - 1U]);
        center_x[slot] =
            center_x[slot - 1U] +
            horizontal_center_delta(
                diameter[slot - 1U],
                diameter[slot],
                vertical_delta
            );
    }

    return {
        .x = center_x[index] - (diameter[index] / 2),
        .y = center_y[index] - (diameter[index] / 2),
        .diameter = diameter[index],
    };
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

std::expected<void, std::string> AdaptiveCommandBar::activate(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::span<const CommandSpec> commands
)
{
    if (commands.size() > kSlotCount) {
        return std::unexpected(
            "Command Arc rejected " + std::to_string(commands.size()) +
            " commands; maximum is " + std::to_string(kSlotCount)
        );
    }
    if (!commands.empty() && (commands.size() % 2U) == 0U) {
        return std::unexpected(
            "Command Arc requires an odd command count so one Main Command "
            "can stay on the horizontal axis"
        );
    }

    size_t main_count = 0;
    for (size_t index = 0; index < commands.size(); ++index) {
        const CommandSpec &command = commands[index];
        if (command.icon.empty() || command.action.empty()) {
            return std::unexpected(
                "Command Arc rejected an empty icon or action"
            );
        }
        if (command.is_main) {
            ++main_count;
            if (index != commands.size() / 2U) {
                return std::unexpected(
                    "Command Arc Main Command must be the central item"
                );
            }
        }
    }
    if (!commands.empty() && main_count != 1U) {
        return std::unexpected(
            "Command Arc requires exactly one central Main Command"
        );
    }

    stop_animations(gui);
    command_count_ = commands.size();
    for (std::string &action : actions_) {
        action.clear();
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(72);
    add_binding(
        updates,
        kCommandRoot,
        "hidden",
        commands.empty() ? "true" : "false"
    );
    add_binding(updates, kCommandRoot, "opacity", "255");

    for (size_t index = 0; index < kSlotCount; ++index) {
        const std::string path = slot_path(index);
        const bool present = index < commands.size();
        add_binding(updates, path, "hidden", present ? "false" : "true");
        add_binding(updates, path, "clickable", present ? "true" : "false");
        if (!present) {
            continue;
        }

        const CommandSpec &command = commands[index];
        const CommandGeometry geometry =
            command_geometry(commands.size(), index);
        const palette::ActionColors &colors =
            palette::action_colors(command.role);
        actions_[index].assign(command.action);

        add_binding(updates, path, "x", std::to_string(geometry.x));
        add_binding(updates, path, "y", std::to_string(geometry.y));
        add_binding(
            updates,
            path,
            "size",
            std::to_string(geometry.diameter)
        );
        add_binding(
            updates,
            path,
            "radius",
            std::to_string(geometry.diameter / 2)
        );
        add_binding(updates, path, "top", colors.top);
        add_binding(updates, path, "bottom", colors.bottom);
        add_binding(updates, path, "border", colors.border);
        add_binding(updates, path, "glow", colors.glow);
        add_binding(updates, path, "pressed_top", colors.pressed_top);
        add_binding(updates, path, "pressed_bottom", colors.pressed_bottom);
        add_binding(updates, path, "pressed_border", colors.pressed_border);
        add_binding(updates, path + "/icon", "icon", command.icon);
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
            "Failed to configure the reusable Command Arc: " +
            result.error()
        );
    }

    active_ = !commands.empty();
    visible_ = true;
    animation_target_visible_ = true;
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

std::expected<void, std::string> AdaptiveCommandBar::set_visible(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool visible
)
{
    if (!active_) {
        return {};
    }
    if (visible) {
        if (auto result = set_hit_testing(gui, true); !result) {
            return result;
        }
    }
    return animate_to(gui, visible);
}

void AdaptiveCommandBar::shutdown(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    stop_animations(gui);
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve((kSlotCount * 2U) + 1U);
    add_binding(updates, kCommandRoot, "hidden", "true");
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
            "Failed to hide Command Arc during shutdown: %s",
            result.error().c_str()
        );
    }
    command_count_ = 0;
    active_ = false;
    visible_ = true;
    animation_target_visible_ = true;
}

bool AdaptiveCommandBar::has_commands() const
{
    return active_;
}

bool AdaptiveCommandBar::target_visible() const
{
    return !active_ || animation_target_visible_;
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
    const uint32_t generation = animation_generation_;
    esp_brookesia::gui::Animation animation{
        .id = "qsee_command_arc_fade",
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
            "Failed to animate the reusable Command Arc: " +
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
    updates.reserve(command_count_);
    for (size_t index = 0; index < command_count_; ++index) {
        add_binding(
            updates,
            slot_path(index),
            "clickable",
            chrome_visible ? "true" : "false"
        );
    }
    auto result = gui.set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update Command Arc hit testing: " +
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
