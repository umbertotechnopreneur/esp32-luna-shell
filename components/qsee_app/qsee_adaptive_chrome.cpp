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
#include <cmath>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "qsee_platform.h"
#include "qsee_settings.hpp"

namespace {

constexpr char kTag[] = "qsee_adaptive_chrome";
constexpr char kCommandTimeoutKey[] = "command_arc_s";
constexpr char kStatusTimeoutKey[] = "status_arc_s";
constexpr char kDimTimeoutKey[] = "display_dim_s";
constexpr char kOffDelayKey[] = "display_off_s";
constexpr uint32_t kStatusFadeDurationMs = 280;
constexpr float kAccelerationDeltaThresholdMps2 = 0.25F;
constexpr float kAngularVelocityThresholdDps = 2.5F;
constexpr std::string_view kStatusPath =
    "/qsee_chrome_screen/chrome/status";
constexpr std::string_view kWakePath =
    "/qsee_chrome_screen/chrome/wake";

std::span<const uint32_t> presets(qsee::AdaptiveChromeTiming timing)
{
    using qsee::AdaptiveChromeController;
    using qsee::AdaptiveChromeTiming;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
    case AdaptiveChromeTiming::StatusArc:
        return AdaptiveChromeController::kArcTimeouts;
    case AdaptiveChromeTiming::DisplayDim:
        return AdaptiveChromeController::kDimTimeouts;
    case AdaptiveChromeTiming::DisplayOff:
        return AdaptiveChromeController::kOffDelays;
    }
    return {};
}

const char *setting_key(qsee::AdaptiveChromeTiming timing)
{
    using qsee::AdaptiveChromeTiming;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        return kCommandTimeoutKey;
    case AdaptiveChromeTiming::StatusArc:
        return kStatusTimeoutKey;
    case AdaptiveChromeTiming::DisplayDim:
        return kDimTimeoutKey;
    case AdaptiveChromeTiming::DisplayOff:
        return kOffDelayKey;
    }
    return "";
}

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
    case AdaptiveChromeTiming::DisplayOff:
        return "display-off delay";
    }
    return "adaptive chrome timeout";
}

uint32_t default_value(qsee::AdaptiveChromeTiming timing)
{
    using qsee::AdaptiveChromeTiming;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        return 10;
    case AdaptiveChromeTiming::StatusArc:
        return 30;
    case AdaptiveChromeTiming::DisplayDim:
        return 30;
    case AdaptiveChromeTiming::DisplayOff:
        return 60;
    }
    return 0;
}

bool is_supported(qsee::AdaptiveChromeTiming timing, uint32_t seconds)
{
    const auto values = presets(timing);
    return std::find(values.begin(), values.end(), seconds) != values.end();
}

std::expected<uint32_t, std::string> load_setting(
    qsee::UserSettingsService &settings,
    qsee::AdaptiveChromeTiming timing
)
{
    uint32_t value = 0;
    esp_err_t result = settings.getUInt32(setting_key(timing), &value);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value(timing);
        result = settings.setUInt32(setting_key(timing), value);
        if (result != ESP_OK) {
            return std::unexpected(
                std::string(setting_name(timing)) +
                " first-boot persistence failed: " +
                esp_err_to_name(result)
            );
        }
        ESP_LOGI(
            kTag,
            "Persisted explicit first-boot %s: %lu s",
            setting_name(timing),
            static_cast<unsigned long>(value)
        );
    } else if (result != ESP_OK) {
        return std::unexpected(
            std::string(setting_name(timing)) + " read failed: " +
            esp_err_to_name(result)
        );
    }

    if (!is_supported(timing, value)) {
        return std::unexpected(
            std::string(setting_name(timing)) +
            " contains unsupported value " + std::to_string(value)
        );
    }
    return value;
}

qsee_display_power_mode_t platform_power_mode(
    qsee::DisplayIdleStage stage
)
{
    using qsee::DisplayIdleStage;
    switch (stage) {
    case DisplayIdleStage::Active:
        return QSEE_DISPLAY_POWER_ACTIVE;
    case DisplayIdleStage::Dimmed:
        return QSEE_DISPLAY_POWER_DIMMED;
    case DisplayIdleStage::Off:
        return QSEE_DISPLAY_POWER_OFF;
    }
    return QSEE_DISPLAY_POWER_ACTIVE;
}

const char *display_stage_name(qsee::DisplayIdleStage stage)
{
    using qsee::DisplayIdleStage;
    switch (stage) {
    case DisplayIdleStage::Active:
        return "active";
    case DisplayIdleStage::Dimmed:
        return "dimmed";
    case DisplayIdleStage::Off:
        return "off";
    }
    return "unknown";
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

std::expected<void, std::string> AdaptiveChromeController::begin()
{
    auto &settings = UserSettingsService::instance();
    const esp_err_t initialization_result = settings.isInitialized()
        ? ESP_OK
        : settings.initialize();
    if (initialization_result != ESP_OK) {
        return std::unexpected(
            "Adaptive Chrome settings initialization failed: " +
            std::string(esp_err_to_name(initialization_result))
        );
    }

    auto command_result =
        load_setting(settings, AdaptiveChromeTiming::CommandArc);
    if (!command_result) {
        return std::unexpected(command_result.error());
    }
    auto status_result =
        load_setting(settings, AdaptiveChromeTiming::StatusArc);
    if (!status_result) {
        return std::unexpected(status_result.error());
    }
    auto dim_result =
        load_setting(settings, AdaptiveChromeTiming::DisplayDim);
    if (!dim_result) {
        return std::unexpected(dim_result.error());
    }
    auto off_result =
        load_setting(settings, AdaptiveChromeTiming::DisplayOff);
    if (!off_result) {
        return std::unexpected(off_result.error());
    }

    command_timeout_seconds_ = *command_result;
    status_timeout_seconds_ = *status_result;
    dim_timeout_seconds_ = *dim_result;
    off_delay_seconds_ = *off_result;
    last_activity_us_ = esp_timer_get_time();
    last_imu_sample_us_ = 0;
    motion_baseline_ready_ = false;
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
    last_activity_us_ = esp_timer_get_time();
    if (display_idle_stage_ != DisplayIdleStage::Active) {
        if (auto result =
                set_display_idle_stage(DisplayIdleStage::Active);
            !result) {
            return result;
        }
    }
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
    if (detect_motion(snapshot.imu)) {
        return note_activity(gui);
    }

    const int64_t elapsed_us =
        esp_timer_get_time() - last_activity_us_;
    const auto expired = [elapsed_us](uint32_t seconds) {
        return seconds > 0U &&
               elapsed_us >= static_cast<int64_t>(seconds) * 1000000LL;
    };

    if (expired(command_timeout_seconds_)) {
        if (auto result = command_bar_.set_visible(gui, false); !result) {
            return result;
        }
    }
    if (expired(status_timeout_seconds_)) {
        if (auto result = set_status_visible(gui, false); !result) {
            return result;
        }
    }

    DisplayIdleStage target_stage = DisplayIdleStage::Active;
    if (expired(dim_timeout_seconds_)) {
        target_stage = DisplayIdleStage::Dimmed;
        const int64_t off_at_us =
            static_cast<int64_t>(dim_timeout_seconds_ + off_delay_seconds_) *
            1000000LL;
        if (off_delay_seconds_ > 0U && elapsed_us >= off_at_us) {
            target_stage = DisplayIdleStage::Off;
        }
    }

    if (target_stage != display_idle_stage_) {
        if (target_stage != DisplayIdleStage::Active) {
            if (auto result = set_wake_surface(gui, true); !result) {
                return result;
            }
        }
        if (auto result = set_display_idle_stage(target_stage); !result) {
            (void)update_wake_surface(gui);
            return result;
        }
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
    if (display_idle_stage_ != DisplayIdleStage::Active) {
        if (auto result =
                set_display_idle_stage(DisplayIdleStage::Active);
            !result) {
            first_error = result.error();
        }
    }

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
    last_imu_sample_us_ = 0;
    motion_baseline_ready_ = false;
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
    if (!is_supported(timing, seconds)) {
        ESP_LOGE(
            kTag,
            "%s rejected unsupported value: %lu s",
            setting_name(timing),
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = UserSettingsService::instance().setUInt32(
        setting_key(timing),
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

    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        command_timeout_seconds_ = seconds;
        break;
    case AdaptiveChromeTiming::StatusArc:
        status_timeout_seconds_ = seconds;
        break;
    case AdaptiveChromeTiming::DisplayDim:
        dim_timeout_seconds_ = seconds;
        break;
    case AdaptiveChromeTiming::DisplayOff:
        off_delay_seconds_ = seconds;
        break;
    }
    last_activity_us_ = esp_timer_get_time();
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
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
        return command_timeout_seconds_;
    case AdaptiveChromeTiming::StatusArc:
        return status_timeout_seconds_;
    case AdaptiveChromeTiming::DisplayDim:
        return dim_timeout_seconds_;
    case AdaptiveChromeTiming::DisplayOff:
        return off_delay_seconds_;
    }
    return 0;
}

size_t AdaptiveChromeController::selected_index(
    AdaptiveChromeTiming timing
) const
{
    const auto values = presets(timing);
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

bool AdaptiveChromeController::detect_motion(const ImuSnapshot &imu)
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
    return {};
}

std::expected<void, std::string>
AdaptiveChromeController::set_display_idle_stage(
    DisplayIdleStage stage
)
{
    const esp_err_t result =
        qsee_display_set_power_mode(platform_power_mode(stage));
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Display transition to %s failed (%s)",
            display_stage_name(stage),
            esp_err_to_name(result)
        );
        return std::unexpected(
            std::string("Display transition to ") +
            display_stage_name(stage) + " failed: " +
            esp_err_to_name(result)
        );
    }
    display_idle_stage_ = stage;
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
