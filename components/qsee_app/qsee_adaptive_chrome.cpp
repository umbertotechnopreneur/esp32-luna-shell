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

#include "qsee_adaptive_chrome.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "esp_log.h"
#include "qsee_platform.h"
#include "qsee_power_policy.hpp"

namespace {

constexpr char kTag[] = "qsee_adaptive_chrome";
constexpr uint32_t kStatusFadeDurationMs = 280;
constexpr std::string_view kStatusPath =
    "/qsee_chrome_screen/chrome/status";
constexpr std::string_view kWakePath =
    "/qsee_chrome_screen/chrome/wake";

const char *setting_name(qsee::AdaptiveChromeTiming timing)
{
    using qsee::AdaptiveChromeTiming;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        return "Command Arc timeout";
    case AdaptiveChromeTiming::StatusArc:
        return "Status Arc timeout";
    case AdaptiveChromeTiming::DisplayDim:
        return "display dim timeout";
    case AdaptiveChromeTiming::ScreenSleep:
        return "screen-sleep delay";
    }
    return "adaptive chrome timeout";
}

qsee_power_timeout_t platform_timeout(
    qsee::AdaptiveChromeTiming timing
)
{
    using qsee::AdaptiveChromeTiming;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        return QSEE_POWER_TIMEOUT_COMMAND_ARC;
    case AdaptiveChromeTiming::StatusArc:
        return QSEE_POWER_TIMEOUT_STATUS_ARC;
    case AdaptiveChromeTiming::DisplayDim:
        return QSEE_POWER_TIMEOUT_DISPLAY_DIM;
    case AdaptiveChromeTiming::ScreenSleep:
        return QSEE_POWER_TIMEOUT_SCREEN_SLEEP;
    }
    return QSEE_POWER_TIMEOUT_COMMAND_ARC;
}

qsee::DisplayIdleStage map_display_idle_stage(
    qsee::UiHardwareSnapshot::DisplayPowerMode mode
)
{
    switch (mode) {
    case qsee::UiHardwareSnapshot::DisplayPowerMode::Dimmed:
        return qsee::DisplayIdleStage::Dimmed;
    case qsee::UiHardwareSnapshot::DisplayPowerMode::DisplayOff:
        return qsee::DisplayIdleStage::Off;
    case qsee::UiHardwareSnapshot::DisplayPowerMode::Active:
    case qsee::UiHardwareSnapshot::DisplayPowerMode::Unknown:
        return qsee::DisplayIdleStage::Active;
    }
    return qsee::DisplayIdleStage::Active;
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

std::span<const uint32_t> AdaptiveChromeController::timeout_options(
    AdaptiveChromeTiming timing
)
{
    return power_policy::timeout_options(platform_timeout(timing));
}

std::expected<void, std::string> AdaptiveChromeController::begin()
{
    uint32_t timeout = 0;
    const esp_err_t policy_result =
        qsee_power_manager_get_timeout(
            QSEE_POWER_TIMEOUT_COMMAND_ARC,
            &timeout
        );
    if (policy_result != ESP_OK) {
        return std::unexpected(
            "Power-management policy is unavailable: " +
            std::string(esp_err_to_name(policy_result))
        );
    }
    display_idle_stage_ = DisplayIdleStage::Active;
    status_visible_ = true;
    status_target_visible_ = true;
    return {};
}

std::expected<void, std::string>
AdaptiveChromeController::configure_commands(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    std::span<const CommandSpec> commands
)
{
    if (auto result = command_bar_.activate(gui, commands); !result) {
        return result;
    }
    if (active_) {
        return update_wake_surface(gui);
    }
    return {};
}

std::optional<std::string_view>
AdaptiveChromeController::resolve_action(
    std::string_view slot_action
) const
{
    return command_bar_.resolve_action(slot_action);
}

std::expected<void, std::string>
AdaptiveChromeController::note_activity(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    if (!active_) {
        return {};
    }
    qsee_power_manager_note_activity();
    display_idle_stage_ = DisplayIdleStage::Active;
    if (auto result = set_status_visible(gui, true); !result) {
        return result;
    }
    if (auto result = command_bar_.set_visible(gui, true); !result) {
        return result;
    }
    return update_wake_surface(gui);
}

std::expected<void, std::string> AdaptiveChromeController::update(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    const DeviceSnapshot &snapshot
)
{
    if (!active_) {
        return {};
    }
    if (auto result = command_bar_.set_visible(
            gui,
            snapshot.power.command_arc_visible
        ); !result) {
        return result;
    }
    if (auto result = set_status_visible(
            gui,
            snapshot.power.status_arc_visible
        ); !result) {
        return result;
    }
    display_idle_stage_ =
        map_display_idle_stage(snapshot.power.display_idle_stage);

    const bool system_standby =
        snapshot.power.state != DeviceOperatingState::Active;
    if (system_standby) {
        return set_wake_surface(gui, false);
    }
    return update_wake_surface(gui);
}

std::expected<void, std::string> AdaptiveChromeController::resume(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    active_ = true;
    return note_activity(gui);
}

std::expected<void, std::string> AdaptiveChromeController::shutdown(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    active_ = false;
    stop_status_animation(gui);
    command_bar_.shutdown(gui);

    std::string first_error;
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(3);
    add_binding(updates, kStatusPath, "opacity", "255");
    add_binding(updates, kWakePath, "hidden", "true");
    add_binding(updates, kWakePath, "clickable", "false");
    auto binding_result = gui.set_binding_values(updates);
    if (!binding_result && first_error.empty()) {
        first_error =
            "Failed to reset Adaptive Chrome during shutdown: " +
            binding_result.error();
    }

    status_visible_ = true;
    status_target_visible_ = true;
    display_idle_stage_ = DisplayIdleStage::Active;
    if (!first_error.empty()) {
        return std::unexpected(std::move(first_error));
    }
    return {};
}

esp_err_t AdaptiveChromeController::set_timeout_seconds(
    AdaptiveChromeTiming timing,
    uint32_t seconds
)
{
    const esp_err_t result = qsee_power_manager_set_timeout(
        platform_timeout(timing),
        seconds
    );
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s persistence failed (%s)",
            setting_name(timing),
            esp_err_to_name(result)
        );
        return result;
    }

    ESP_LOGI(
        kTag,
        "%s updated: %lu s",
        setting_name(timing),
        static_cast<unsigned long>(seconds)
    );
    return ESP_OK;
}

uint32_t AdaptiveChromeController::timeout_seconds(
    AdaptiveChromeTiming timing
) const
{
    uint32_t seconds = 0;
    const esp_err_t result = qsee_power_manager_get_timeout(
        platform_timeout(timing),
        &seconds
    );
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s timeout read failed (%s)",
            setting_name(timing),
            esp_err_to_name(result)
        );
        return 0;
    }
    return seconds;
}

size_t AdaptiveChromeController::selected_index(
    AdaptiveChromeTiming timing
) const
{
    const auto values = timeout_options(timing);
    const uint32_t selected = timeout_seconds(timing);
    const auto match = std::find(values.begin(), values.end(), selected);
    return match == values.end()
        ? 0U
        : static_cast<size_t>(std::distance(values.begin(), match));
}

DisplayIdleStage AdaptiveChromeController::display_idle_stage() const
{
    return display_idle_stage_;
}

std::expected<void, std::string>
AdaptiveChromeController::set_status_visible(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool visible
)
{
    if (status_target_visible_ == visible && status_animation_id_ != 0) {
        return {};
    }
    if (status_visible_ == visible && status_animation_id_ == 0) {
        return {};
    }

    stop_status_animation(gui);
    status_target_visible_ = visible;
    const uint32_t generation = status_animation_generation_;
    esp_brookesia::gui::Animation animation{
        .id = "qsee_status_arc_fade",
        .trigger = esp_brookesia::gui::AnimationTrigger::Manual,
        .property = esp_brookesia::gui::AnimationProperty::Opacity,
        .from_mode = esp_brookesia::gui::AnimationValueMode::Current,
        .from = 0,
        .to_mode = esp_brookesia::gui::AnimationValueMode::Absolute,
        .to = visible ? 255 : 0,
        .duration = static_cast<int32_t>(kStatusFadeDurationMs),
        .delay = 0,
        .easing = esp_brookesia::gui::AnimationEasing::EaseInOut,
        .repeat = 0,
        .playback = false,
    };
    auto result = gui.start_view_animation_with_result(
        kStatusPath,
        animation,
        [this, generation]() {
            status_animation_completed(generation);
        }
    );
    if (!result) {
        status_target_visible_ = status_visible_;
        return std::unexpected(
            "Failed to animate Status Arc: " + result.error()
        );
    }
    status_animation_id_ = result->subscription_id;
    ESP_LOGI(
        kTag,
        "Status Arc transition: %s",
        visible ? "visible" : "hidden"
    );
    return {};
}

std::expected<void, std::string>
AdaptiveChromeController::set_wake_surface(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool enabled
)
{
    const std::vector<esp_brookesia::gui::BindingValueUpdate> updates = {
        {
            .absolute_path = std::string(kWakePath),
            .key = "hidden",
            .value = enabled ? "false" : "true",
        },
        {
            .absolute_path = std::string(kWakePath),
            .key = "clickable",
            .value = enabled ? "true" : "false",
        },
    };
    auto result = gui.set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update the Adaptive Chrome Wake Surface: " +
            result.error()
        );
    }
    return {};
}

std::expected<void, std::string>
AdaptiveChromeController::update_wake_surface(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    const bool should_capture =
        active_ &&
        (display_idle_stage_ != DisplayIdleStage::Active ||
         !status_target_visible_ ||
         !command_bar_.target_visible());
    return set_wake_surface(gui, should_capture);
}

void AdaptiveChromeController::stop_status_animation(
    const esp_brookesia::system::core::AppGuiRuntime &gui
)
{
    ++status_animation_generation_;
    if (status_animation_id_ != 0) {
        (void)gui.stop_animation(status_animation_id_);
        status_animation_id_ = 0;
    }
}

void AdaptiveChromeController::status_animation_completed(
    uint32_t generation
)
{
    if (generation != status_animation_generation_) {
        return;
    }
    status_animation_id_ = 0;
    status_visible_ = status_target_visible_;
}

}  // namespace qsee
