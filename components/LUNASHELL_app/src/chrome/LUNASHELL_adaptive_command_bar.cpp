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


#include "LUNASHELL_adaptive_command_bar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "LUNASHELL_command_arc";
constexpr uint32_t kFadeDurationMs = 280;
constexpr int kDisplayCenterXDp = 233;
constexpr int kMainCenterYDp = 410;
constexpr int kArcRiseLinearDp = 10;
constexpr int kArcRiseQuadraticDp = 5;
constexpr std::string_view kCommandRoot =
    "/LUNASHELL_chrome_screen/chrome/commands";
constexpr std::array<std::string_view, LUNASHELL::AdaptiveCommandBar::kSlotCount>
    kSlotActions = {
        "LUNASHELL.command.0",
        "LUNASHELL.command.1",
        "LUNASHELL.command.2",
        "LUNASHELL.command.3",
        "LUNASHELL.command.4",
    };

struct CommandGeometry {
    int x;
    int y;
    int diameter;
};

// count: Active command count, checked before geometry is requested.
// index: Internal traversal position within this route's ordered command list.
size_t command_center_distance(size_t count, size_t index)
{
    const size_t right_center = count / 2U;
    if ((count % 2U) != 0U) {
        return static_cast<size_t>(std::abs(
            static_cast<int>(index) - static_cast<int>(right_center)
        ));
    }
    const size_t left_center = right_center - 1U;
    if (index < left_center) {
        return left_center - index;
    }
    if (index > right_center) {
        return index - right_center;
    }
    return 0;
}

// count: Number of visible commands on the arc.
// index: Traversal position whose distance from the center chooses its diameter.
int command_diameter(size_t count, size_t index)
{
    const int distance = static_cast<int>(
        command_center_distance(count, index)
    );
    return LUNASHELL::AdaptiveCommandBar::kMainDiameterDp -
           (distance * LUNASHELL::AdaptiveCommandBar::kDiameterStepDp);
}

// count: Number of visible commands on the arc.
// index: Traversal position whose center is lifted along the arc.
int command_center_y(size_t count, size_t index)
{
    const int distance = static_cast<int>(
        command_center_distance(count, index)
    );
    return kMainCenterYDp -
           (kArcRiseLinearDp * distance) -
           (kArcRiseQuadraticDp * distance * distance);
}

// first_diameter: Diameter of the first adjacent button.
// second_diameter: Diameter of its neighbor.
// vertical_delta: Difference between their vertical centers.
int horizontal_center_delta(
    int first_diameter,
    int second_diameter,
    int vertical_delta
)
{
    // Use the combined diameter so odd-sized buttons retain the same
    // edge-to-edge gap as the previous even-sized geometry.
    const int target_distance =
        ((first_diameter + second_diameter) / 2) +
        LUNASHELL::AdaptiveCommandBar::kButtonGapDp;
    const int horizontal_squared =
        (target_distance * target_distance) -
        (vertical_delta * vertical_delta);
    return static_cast<int>(
        std::lround(std::sqrt(static_cast<double>(horizontal_squared)))
    );
}

using CommandLayout = std::array<CommandGeometry, LUNASHELL::AdaptiveCommandBar::kSlotCount>;

// count: Previously checked number of visible commands, including an empty route.
CommandLayout command_layout(size_t count)
{
    CommandLayout layout{};
    if (count == 0) {
        return layout;
    }
    // Settle the middle first, then grow the arc evenly toward both sides.
    const size_t right_center = count / 2U;
    const size_t left_center =
        (count % 2U) == 0U ? right_center - 1U : right_center;
    std::array<int, LUNASHELL::AdaptiveCommandBar::kSlotCount> center_x{};
    std::array<int, LUNASHELL::AdaptiveCommandBar::kSlotCount> center_y{};
    std::array<int, LUNASHELL::AdaptiveCommandBar::kSlotCount> diameter{};

    for (size_t slot = 0; slot < count; ++slot) {
        diameter[slot] = command_diameter(count, slot);
        center_y[slot] = command_center_y(count, slot);
    }
    if (left_center == right_center) {
        center_x[left_center] = kDisplayCenterXDp;
    } else {
        const int center_delta = horizontal_center_delta(
            diameter[left_center],
            diameter[right_center],
            0
        );
        center_x[left_center] =
            kDisplayCenterXDp - (center_delta / 2);
        center_x[right_center] =
            center_x[left_center] + center_delta;
    }

    for (size_t slot = left_center; slot > 0; --slot) {
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
    for (size_t slot = right_center + 1U; slot < count; ++slot) {
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

    for (size_t slot = 0; slot < count; ++slot) {
        layout[slot] = {
            .x = center_x[slot] - (diameter[slot] / 2),
            .y = center_y[slot] - (diameter[slot] / 2),
            .diameter = diameter[slot],
        };
    }
    return layout;
}

// index: Internal slot position, encoded into its existing stable GUI path.
std::string slot_path(size_t index)
{
    return std::string(kCommandRoot) + "/slot" + std::to_string(index);
}

// updates: Pending batch receiving this named binding.
// path: Unique view path in the mounted command document.
// key: Binding name owned by that view.
// value: Explicit value to apply when the batch commits.
// Exceptions: std::bad_alloc if the binding batch cannot be allocated.
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

namespace LUNASHELL {

// Replace command bindings only after any prior animation is fully stopped.
// gui: Brookesia runtime that owns the mounted command arc.
// commands: Named route actions, kept in their intended visual order.
// visible: Requested chrome visibility after the route bindings commit.
// Exceptions: std::bad_alloc if command or binding storage cannot be allocated.
std::expected<void, std::string> AdaptiveCommandBar::activate(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::span<const CommandSpec> commands,
    bool visible
)
{
    if (commands.size() > kSlotCount) {
        return std::unexpected(
            "Command Arc rejected " + std::to_string(commands.size()) +
            " commands; maximum is " + std::to_string(kSlotCount)
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
            const size_t right_center = commands.size() / 2U;
            const size_t left_center =
                (commands.size() % 2U) == 0U
                    ? right_center - 1U
                    : right_center;
            if (index != left_center && index != right_center) {
                return std::unexpected(
                    "Command Arc Main Command must be a central item"
                );
            }
        }
    }
    size_t expected_main_count = 0;
    if (!commands.empty()) {
        const bool has_two_centers = (commands.size() % 2U) == 0U;
        expected_main_count = has_two_centers ? 2U : 1U;
    }
    if (main_count != expected_main_count) {
        return std::unexpected(
            "Command Arc requires one central Main Command for odd layouts "
            "or two for even layouts"
        );
    }

    if (auto result = stop_animations(gui); !result) {
        return result;
    }
    command_count_ = commands.size();
    for (std::string &action : actions_) {
        action.clear();
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(72);
    const CommandLayout layout = command_layout(commands.size());
    add_binding(
        updates,
        kCommandRoot,
        "hidden",
        commands.empty() ? "true" : "false"
    );
    add_binding(
        updates,
        kCommandRoot,
        "opacity",
        visible ? "255" : "0"
    );

    for (size_t index = 0; index < kSlotCount; ++index) {
        const std::string path = slot_path(index);
        const bool present = index < commands.size();
        add_binding(updates, path, "hidden", present ? "false" : "true");
        add_binding(
            updates,
            path,
            "clickable",
            present && visible ? "true" : "false"
        );
        if (!present) {
            continue;
        }

        const CommandSpec &command = commands[index];
        const CommandGeometry &geometry = layout[index];
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
        hit_testing_enabled_.reset();
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
    visible_ = visible;
    animation_target_visible_ = visible;
    hit_testing_enabled_ = active_ && visible;
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
    if (!visible) {
        // The content viewport can expand as soon as the canonical target
        // changes, so retiring Command Arc actions must stop receiving input
        // before their fade begins.
        if (auto result = set_hit_testing(gui, false); !result) {
            return result;
        }
    }
    return animate_to(gui, visible);
}

// Stop command motion and hit testing while retaining the mounted route model.
std::expected<void, std::string> AdaptiveCommandBar::quiesce(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    std::string first_error;
    bool animation_stopped = true;
    if (auto result = stop_animations(gui); !result) {
        animation_stopped = false;
        first_error = result.error();
        ESP_LOGE(
            kTag,
            "Command Arc animation quiescence failed: %s",
            result.error().c_str()
        );
    }
    if (animation_stopped) {
        auto result = gui.set_binding_value(
            kCommandRoot,
            "opacity",
            animation_target_visible_ ? "255" : "0"
        );
        if (!result) {
            ESP_LOGE(
                kTag,
                "Command Arc opacity snap failed during quiescence: %s",
                result.error().c_str()
            );
            if (first_error.empty()) {
                first_error =
                    "Failed to snap Command Arc during quiescence: " +
                    result.error();
            }
        } else {
            visible_ = animation_target_visible_;
        }
    }
    if (auto result = set_hit_testing(gui, false); !result) {
        ESP_LOGE(
            kTag,
            "Command Arc hit-testing quiescence failed: %s",
            result.error().c_str()
        );
        if (first_error.empty()) {
            first_error = result.error();
        }
    }
    if (!first_error.empty()) {
        return std::unexpected(std::move(first_error));
    }
    return {};
}

// Tear down command state while retaining the first real cleanup failure.
std::expected<void, std::string> AdaptiveCommandBar::shutdown(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    std::string first_error;
    if (auto result = stop_animations(gui); !result) {
        first_error = result.error();
        ESP_LOGE(
            kTag,
            "Command Arc animation shutdown failed: %s",
            result.error().c_str()
        );
    }
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
        hit_testing_enabled_.reset();
        ESP_LOGE(
            kTag,
            "Failed to hide Command Arc during shutdown: %s",
            result.error().c_str()
        );
        if (first_error.empty()) {
            first_error = result.error();
        }
    } else {
        hit_testing_enabled_ = false;
    }
    command_count_ = 0;
    active_ = false;
    visible_ = true;
    animation_target_visible_ = true;
    if (!first_error.empty()) {
        return std::unexpected(std::move(first_error));
    }
    return {};
}

bool AdaptiveCommandBar::has_commands() const
{
    return active_;
}

bool AdaptiveCommandBar::target_visible() const
{
    return !active_ || animation_target_visible_;
}

// Start one command transition after reconciling the prior subscription.
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

    if (auto result = stop_animations(gui); !result) {
        return result;
    }
    animation_target_visible_ = visible;
    const uint32_t generation = animation_generation_;
    esp_brookesia::gui::Animation animation{
        .id = "LUNASHELL_command_arc_fade",
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
    ESP_LOGI(
        kTag,
        "Command Arc transition: %s",
        visible ? "visible" : "hidden"
    );
    return {};
}

std::expected<void, std::string> AdaptiveCommandBar::set_hit_testing(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool chrome_visible
)
{
    if (hit_testing_enabled_ == chrome_visible) {
        return {};
    }
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
        hit_testing_enabled_.reset();
        return std::unexpected(
            "Failed to update Command Arc hit testing: " +
            result.error()
        );
    }
    hit_testing_enabled_ = chrome_visible;
    return {};
}

// Cancel the tracked animation and invalidate any completion already queued.
std::expected<void, std::string> AdaptiveCommandBar::stop_animations(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    ++animation_generation_;
    if (animation_id_ != 0) {
        if (auto result = gui.stop_animation(animation_id_); !result) {
            return std::unexpected(
                "Failed to stop Command Arc animation: " + result.error()
            );
        }
        animation_id_ = 0;
    }
    return {};
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
    if (auto result = set_hit_testing(gui, visible_); !result) {
        ESP_LOGE(kTag, "%s", result.error().c_str());
    }
}

}  // namespace LUNASHELL
