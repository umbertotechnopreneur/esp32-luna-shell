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


#include "LUNASHELL_app.hpp"
#include "LUNASHELL_app_internal.hpp"

#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "esp_log.h"

namespace {

using esp_brookesia::system::core::AppContext;
using LUNASHELL::app_detail::kTag;

// route: Named screen whose ordinary actions are forwarded to the app.
std::span<const std::string_view> forwarded_actions_for_route(LUNASHELL::Route route)
{
    static constexpr std::array<std::string_view, 1> kBatteryActions = {
        "LUNASHELL.chrome.activity.battery",
    };
    static constexpr std::array<std::string_view, 1> kGpsActions = {
        "LUNASHELL.chrome.activity.gps",
    };
    static constexpr std::array<std::string_view, 2> kLegActions = {
        "LUNASHELL.leg.previous",
        "LUNASHELL.leg.next",
    };
    static constexpr std::array<std::string_view, 1> kLogActions = {
        "LUNASHELL.chrome.activity.logs",
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::size_t kHardwareActionCount = 14;
#else
    static constexpr std::size_t kHardwareActionCount = 13;
#endif
    static constexpr std::array<
        std::string_view,
        kHardwareActionCount
    > kHardwareActions = {
        "LUNASHELL.chrome.activity.hardware",
        "LUNASHELL.hardware.detail.0",
        "LUNASHELL.hardware.detail.1",
        "LUNASHELL.hardware.detail.2",
        "LUNASHELL.hardware.detail.3",
        "LUNASHELL.hardware.detail.4",
        "LUNASHELL.hardware.detail.5",
        "LUNASHELL.hardware.detail.6",
        "LUNASHELL.hardware.detail.7",
        "LUNASHELL.hardware.detail.8",
        "LUNASHELL.hardware.detail.9",
        "LUNASHELL.hardware.detail.10",
        "LUNASHELL.hardware.detail.11",
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        "LUNASHELL.hardware.detail.12",
#endif
    };
    static constexpr std::array<std::string_view, 1> kHardwareDetailActions = {
        "LUNASHELL.chrome.activity.hardware_detail",
    };
    static constexpr std::array<std::string_view, 2> kSettingsActions = {
        "LUNASHELL.chrome.activity.settings",
        "LUNASHELL.open.about",
    };
    static constexpr std::array<std::string_view, 2> kStorageActions = {
        "LUNASHELL.chrome.activity.storage",
        "LUNASHELL.storage.format",
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::array<std::string_view, 1> kTofSettingsActions = {
        "LUNASHELL.chrome.activity.tof_settings",
    };
#endif

    std::span<const std::string_view> forwarded_actions;
    switch (route) {
    case LUNASHELL::Route::Battery:
        forwarded_actions = kBatteryActions;
        break;
    case LUNASHELL::Route::Gps:
        forwarded_actions = kGpsActions;
        break;
    case LUNASHELL::Route::Leg:
        forwarded_actions = kLegActions;
        break;
    case LUNASHELL::Route::Logs:
        forwarded_actions = kLogActions;
        break;
    case LUNASHELL::Route::Hardware:
        forwarded_actions = kHardwareActions;
        break;
    case LUNASHELL::Route::HardwareDetail:
        forwarded_actions = kHardwareDetailActions;
        break;
    case LUNASHELL::Route::Settings:
        forwarded_actions = kSettingsActions;
        break;
    case LUNASHELL::Route::Storage:
        forwarded_actions = kStorageActions;
        break;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case LUNASHELL::Route::TofSettings:
        forwarded_actions = kTofSettingsActions;
        break;
#endif
    default:
        break;
    }

    return forwarded_actions;
}

}  // namespace

namespace LUNASHELL {

// document_id: Identity captured when this screen's callbacks were connected.
// generation: Loading generation that must still own the visible document.
bool App::is_current_screen_document(
    esp_brookesia::gui::DocumentId document_id,
    uint32_t generation
) const
{
    return active_screen_document_id_.has_value() &&
           *active_screen_document_id_ == document_id &&
           screen_document_generation_ == generation;
}

// action: Owned unique action ID forwarded from the currently loaded document.
// Exceptions: std::bad_alloc if action handling cannot allocate its required state.
void App::dispatch_screen_document_action(std::string action)
{
    if (context_ == nullptr) {
        ESP_LOGE(
            kTag,
            "Rejected lazy GUI action '%s': app context is unavailable",
            action.c_str()
        );
        return;
    }
    auto result = on_action(*context_, action);
    if (!result) {
        ESP_LOGE(
            kTag,
            "Lazy GUI action '%s' failed: %s",
            action.c_str(),
            result.error().c_str()
        );
    }
}

// context: Brookesia runtime owning the document and event subscriptions.
// document_id: Loaded document whose callbacks are being connected.
// generation: Current loading generation, retained to reject late callbacks.
// action: Unique GUI action ID within the loaded document.
// handler: Owned event callback, invoked only while the document is active.
// connections: Session-owned subscriptions, all cleared if connection fails.
// Exceptions: std::bad_alloc if callback or connection storage cannot be allocated.
std::expected<void, std::string> App::subscribe_screen_document_action(
    AppContext &context,
    esp_brookesia::gui::DocumentId document_id,
    uint32_t generation,
    std::string_view action,
    esp_brookesia::gui::Runtime::ActionHandler handler,
    std::vector<esp_brookesia::gui::ScopedConnection> &connections
)
{
    auto guarded_handler =
        [this, document_id, generation, handler = std::move(handler)](
            const esp_brookesia::gui::Event &event
        ) mutable {
            // Old invitations expire: a replaced screen cannot receive this event.
            if (standby_ui_state_ != StandbyUiState::Active ||
                !is_current_screen_document(document_id, generation)) {
                return;
            }
            handler(event);
        };
    auto connection = context.gui().subscribe_action(
        document_id, action, std::move(guarded_handler)
    );
    if (!connection.connected()) {
        // A half-connected screen is not ready. Retire every callback from this attempt.
        connections.clear();
        return std::unexpected(
            "Failed to subscribe lazy GUI action '" + std::string(action) + "'"
        );
    }
    connections.push_back(std::move(connection));
    return {};
}

// context: Runtime owning all action connections for this screen.
// document_id: Loaded document to connect before publishing it as active.
// screen: Named route choosing the action catalog and setting handlers.
// generation: Loading generation retained by every callback.
// connections: Owner of all successfully connected callbacks.
// Exceptions: std::bad_alloc if action handlers or connection storage cannot be allocated.
std::expected<void, std::string> App::subscribe_screen_document_actions(
    AppContext &context,
    esp_brookesia::gui::DocumentId document_id,
    Screen screen,
    uint32_t generation,
    std::vector<esp_brookesia::gui::ScopedConnection> &connections
)
{
    // action: Unique action ID from the route's document.
    // handler: Event callback whose lifetime belongs to this loading generation.
    // Exceptions: std::bad_alloc if the subscription cannot allocate its callback.
    const auto connect_action = [&](
        std::string_view action,
        esp_brookesia::gui::Runtime::ActionHandler handler
    ) -> std::expected<void, std::string> {
        return subscribe_screen_document_action(
            context, document_id, generation, action, std::move(handler), connections
        );
    };

    for (const std::string_view action : forwarded_actions_for_route(screen)) {
        auto result = connect_action(
            action,
            [this, action = std::string(action)](const esp_brookesia::gui::Event &) {
                dispatch_screen_document_action(action);
            }
        );
        if (!result) {
            return result;
        }
    }

    if (screen == Screen::Leg) {
        static constexpr std::array<std::string_view, 4> leg_settings_actions = {
            "LUNASHELL.leg.rotation.changed",
            "LUNASHELL.leg.flip.changed",
            "LUNASHELL.leg.sounds.changed",
            "LUNASHELL.leg.wake.changed",
        };
        for (const std::string_view action : leg_settings_actions) {
            auto result = connect_action(
                action,
                [this, action = std::string(action)](const esp_brookesia::gui::Event &event) {
                    handle_leg_setting_event(action, event);
                }
            );
            if (!result) {
                return result;
            }
        }
        return {};
    }

    // These controls have the same event shape, but each keeps its own named handler.
    struct SettingEventBinding {
        std::string_view action;
        void (App::*handler)(const esp_brookesia::gui::Event &event);
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings) {
        static constexpr std::array tof_settings = {
            SettingEventBinding{"LUNASHELL.tof.settings.samples.changed", &App::handle_tof_sample_count_event},
            SettingEventBinding{"LUNASHELL.tof.settings.interval.changed", &App::handle_tof_sample_interval_event},
            SettingEventBinding{"LUNASHELL.tof.settings.reference.preview", &App::handle_tof_reference_preview_event},
            SettingEventBinding{"LUNASHELL.tof.settings.reference.commit", &App::handle_tof_reference_commit_event},
        };
        for (const auto &binding : tof_settings) {
            auto result = connect_action(
                binding.action,
                [this, handler = binding.handler](const esp_brookesia::gui::Event &event) {
                    (this->*handler)(event);
                }
            );
            if (!result) {
                return result;
            }
        }
        return {};
    }
#endif
    if (screen != Screen::Settings) {
        return {};
    }

    struct TimingEventBinding {
        std::string_view action;
        AdaptiveChromeTiming setting;
    };
    static constexpr std::array timing_settings = {
        TimingEventBinding{"LUNASHELL.settings.command_arc.changed", AdaptiveChromeTiming::CommandArc},
        TimingEventBinding{"LUNASHELL.settings.status_arc.changed", AdaptiveChromeTiming::StatusArc},
        TimingEventBinding{"LUNASHELL.settings.display_dim.changed", AdaptiveChromeTiming::DisplayDim},
        TimingEventBinding{"LUNASHELL.settings.screen_idle.changed", AdaptiveChromeTiming::ScreenIdle},
    };
    for (const auto &binding : timing_settings) {
        auto result = connect_action(
            binding.action,
            [this, setting = binding.setting](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(setting, event);
            }
        );
        if (!result) {
            return result;
        }
    }

    static constexpr std::array other_settings = {
        SettingEventBinding{"LUNASHELL.settings.full_snapshot.changed", &App::handle_full_snapshot_period_event},
        SettingEventBinding{"LUNASHELL.settings.diagnostic.changed", &App::handle_diagnostic_period_event},
        SettingEventBinding{"LUNASHELL.settings.imu_smoothing.changed", &App::handle_imu_smoothing_event},
        SettingEventBinding{"LUNASHELL.settings.display.brightness.changed", &App::handle_brightness_event},
        SettingEventBinding{"LUNASHELL.settings.performance.changed", &App::handle_performance_event},
        SettingEventBinding{"LUNASHELL.settings.storage.mirror.changed", &App::handle_storage_mirror_event},
        SettingEventBinding{"LUNASHELL.settings.display.rotation.changed", &App::handle_auto_rotation_event},
    };
    for (const auto &binding : other_settings) {
        auto result = connect_action(
            binding.action,
            [this, handler = binding.handler](const esp_brookesia::gui::Event &event) {
                (this->*handler)(event);
            }
        );
        if (!result) {
            return result;
        }
    }
    return {};
}

}  // namespace LUNASHELL
