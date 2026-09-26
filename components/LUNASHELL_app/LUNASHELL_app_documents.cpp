/*
 * ESP32 Luna Shell
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 ESP32 Luna Shell. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Author website: https://umbertogiacobbi.biz
 *
 */

#include "LUNASHELL_app.hpp"
#include "LUNASHELL_app_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "LUNASHELL_platform.h"

namespace {

using esp_brookesia::system::core::AppContext;
using LUNASHELL::app_detail::kTag;

constexpr char kGuiRoot[] = "res/root.json";
constexpr char kBackgroundFlow[] = "LUNASHELL_background_flow";
constexpr char kScreenFlow[] = "LUNASHELL_content";
constexpr char kReleaseContentAction[] = "LUNASHELL.content.release";
constexpr char kChromeFlow[] = "LUNASHELL_chrome_flow";

}  // namespace

namespace LUNASHELL {

std::optional<App::LazyScreenDescriptor> App::lazy_screen_descriptor(
    Screen screen
)
{
    switch (screen) {
    case Screen::Welcome:
        return std::nullopt;
    case Screen::Battery:
        return LazyScreenDescriptor{
            .document_path = "res/screen_battery.json",
            .screen_path = "/LUNASHELL_battery",
        };
    case Screen::Logs:
        return LazyScreenDescriptor{
            .document_path = "res/screen_logs.json",
            .screen_path = "/LUNASHELL_logs",
        };
    case Screen::Hardware:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware.json",
            .screen_path = "/LUNASHELL_hardware",
        };
    case Screen::HardwareDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware_detail.json",
            .screen_path = "/LUNASHELL_hardware_detail",
        };
    case Screen::Audio:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio.json",
            .screen_path = "/LUNASHELL_audio",
        };
    case Screen::AudioFft:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_fft.json",
            .screen_path = "/LUNASHELL_audio_fft",
        };
    case Screen::AudioDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_detail.json",
            .screen_path = "/LUNASHELL_audio_detail",
        };
    case Screen::Imu:
        return LazyScreenDescriptor{
            .document_path = "res/screen_imu.json",
            .screen_path = "/LUNASHELL_imu",
        };
    case Screen::Leg:
        return LazyScreenDescriptor{
            .document_path = "res/screen_leg.json",
            .screen_path = "/LUNASHELL_leg",
        };
    case Screen::Gps:
        return LazyScreenDescriptor{
            .document_path = "res/screen_gps.json",
            .screen_path = "/LUNASHELL_gps",
        };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::Tof:
        return LazyScreenDescriptor{
            .document_path = "res/screen_tof.json",
            .screen_path = "/LUNASHELL_tof",
        };
    case Screen::TofSettings:
        return LazyScreenDescriptor{
            .document_path = "res/screen_tof_settings.json",
            .screen_path = "/LUNASHELL_tof_settings",
        };
#endif
    case Screen::Settings:
        return LazyScreenDescriptor{
            .document_path = "res/screen_settings.json",
            .screen_path = "/LUNASHELL_settings",
        };
    case Screen::Storage:
        return LazyScreenDescriptor{
            .document_path = "res/screen_storage.json",
            .screen_path = "/LUNASHELL_storage",
        };
    case Screen::About:
        return LazyScreenDescriptor{
            .document_path = "res/screen_about.json",
            .screen_path = "/LUNASHELL_about",
        };
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return LazyScreenDescriptor{
            .document_path = "res/screen_pairing.json",
            .screen_path = "/LUNASHELL_pairing",
        };
    }
    return std::nullopt;
}

bool App::shares_lazy_document(Screen left, Screen right)
{
    const auto left_descriptor = lazy_screen_descriptor(left);
    const auto right_descriptor = lazy_screen_descriptor(right);
    return left_descriptor.has_value() &&
           right_descriptor.has_value() &&
           left_descriptor->document_path == right_descriptor->document_path &&
           left_descriptor->screen_path == right_descriptor->screen_path;
}

bool App::is_current_screen_document(
    esp_brookesia::gui::DocumentId document_id,
    uint32_t generation
) const
{
    return active_screen_document_id_.has_value() &&
           *active_screen_document_id_ == document_id &&
           screen_document_generation_ == generation;
}

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

// Route every lazy document callback through the standby producer gate.
std::expected<void, std::string> App::subscribe_screen_document_actions(
    AppContext &context,
    esp_brookesia::gui::DocumentId document_id,
    Screen screen,
    uint32_t generation,
    std::vector<esp_brookesia::gui::ScopedConnection> &connections
)
{
    const auto add_connection = [&](
        std::string_view action,
        esp_brookesia::gui::Runtime::ActionHandler handler
    ) -> std::expected<void, std::string> {
        auto guarded_handler =
            [this, handler = std::move(handler)](
                const esp_brookesia::gui::Event &event
            ) mutable {
                if (standby_ui_state_ != StandbyUiState::Active) {
                    return;
                }
                handler(event);
            };
        auto connection = context.gui().subscribe_action(
            document_id,
            action,
            std::move(guarded_handler)
        );
        if (!connection.connected()) {
            return std::unexpected(
                "Failed to subscribe lazy GUI action '" + std::string(action) + "'"
            );
        }
        connections.push_back(std::move(connection));
        return {};
    };

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
    switch (screen) {
    case Screen::Battery:
        forwarded_actions = kBatteryActions;
        break;
    case Screen::Gps:
        forwarded_actions = kGpsActions;
        break;
    case Screen::Leg:
        forwarded_actions = kLegActions;
        break;
    case Screen::Logs:
        forwarded_actions = kLogActions;
        break;
    case Screen::Hardware:
        forwarded_actions = kHardwareActions;
        break;
    case Screen::HardwareDetail:
        forwarded_actions = kHardwareDetailActions;
        break;
    case Screen::Settings:
        forwarded_actions = kSettingsActions;
        break;
    case Screen::Storage:
        forwarded_actions = kStorageActions;
        break;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::TofSettings:
        forwarded_actions = kTofSettingsActions;
        break;
#endif
    default:
        break;
    }

    for (const std::string_view action : forwarded_actions) {
        auto result = add_connection(
            action,
            [this,
             document_id,
             generation,
             action = std::string(action)](const esp_brookesia::gui::Event &) {
                if (!is_current_screen_document(document_id, generation)) {
                    return;
                }
                dispatch_screen_document_action(action);
            }
        );
        if (!result) {
            connections.clear();
            return result;
        }
    }

    if (screen == Screen::Leg) {
        static constexpr std::array<std::string_view, 4> kLegSettingsActions = {
            "LUNASHELL.leg.rotation.changed",
            "LUNASHELL.leg.flip.changed",
            "LUNASHELL.leg.sounds.changed",
            "LUNASHELL.leg.wake.changed",
        };
        for (const std::string_view action : kLegSettingsActions) {
            auto result = add_connection(
                action,
                [this, document_id, generation,
                 action = std::string(action)](
                    const esp_brookesia::gui::Event &event
                ) {
                    if (!is_current_screen_document(document_id, generation)) {
                        return;
                    }
                    handle_leg_setting_event(action, event);
                }
            );
            if (!result) {
                connections.clear();
                return result;
            }
        }
        return {};
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings) {
        const auto add_tof_settings_connection = [&](
            std::string_view action,
            auto handler
        ) -> std::expected<void, std::string> {
            return add_connection(
                action,
                [this,
                 document_id,
                 generation,
                 handler = std::move(handler)](
                    const esp_brookesia::gui::Event &event
                ) mutable {
                    if (!is_current_screen_document(document_id, generation)) {
                        return;
                    }
                    handler(event);
                }
            );
        };

        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.samples.changed",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_sample_count_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.interval.changed",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_sample_interval_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.reference.preview",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_reference_preview_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.reference.commit",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_reference_commit_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        return {};
    }
#endif

    if (screen != Screen::Settings) {
        return {};
    }

    const auto add_settings_connection = [&](
        std::string_view action,
        auto handler
    ) -> std::expected<void, std::string> {
        return add_connection(
            action,
            [this,
             document_id,
             generation,
             handler = std::move(handler)](
                const esp_brookesia::gui::Event &event
            ) mutable {
                if (!is_current_screen_document(document_id, generation)) {
                    return;
                }
                handler(event);
            }
        );
    };

    if (auto result = add_settings_connection(
            "LUNASHELL.settings.command_arc.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::CommandArc,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.status_arc.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::StatusArc,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display_dim.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::DisplayDim,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.screen_sleep.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::ScreenSleep,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.full_snapshot.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_full_snapshot_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.diagnostic.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_diagnostic_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.imu_smoothing.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_imu_smoothing_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display.brightness.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_brightness_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.performance.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_performance_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.storage.mirror.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_storage_mirror_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display.rotation.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_auto_rotation_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    return {};
}

void App::invalidate_screen_document_state(Screen screen)
{
    content_viewport_state_.reset();
    invalidate_audio_screen_cache(screen);
    if (screen == Screen::Hardware) {
        invalidate_hardware_screen_cache();
    }
    if (screen == Screen::Imu) {
        invalidate_imu_screen_cache();
    }
    if (screen == Screen::Leg) {
        invalidate_leg_screen_cache();
    }
    if (screen == Screen::Gps) {
        invalidate_gps_screen_cache();
    }
    if (screen == Screen::AudioDetail) {
        audio_detail_palette_applied_ = false;
    }
    if (screen == Screen::Settings) {
        settings_text_cache_valid_ = false;
        settings_controls_state_.reset();
    }
    if (screen == Screen::Storage) {
        invalidate_storage_screen_cache();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings) {
        tof_settings_controls_state_.reset();
        tof_reference_pending_mm_.reset();
        tof_reference_editing_ = false;
    }
#endif
    if (is_pairing_screen(screen)) {
        pairing_visual_state_.reset();
    }
}

void App::log_screen_document_heap(
    std::string_view operation,
    Screen screen,
    uint32_t loaded_document_count
) const
{
    const auto descriptor = lazy_screen_descriptor(screen);
    const std::string_view document_path = descriptor.has_value()
        ? descriptor->document_path
        : std::string_view(kGuiRoot);
    const uint32_t internal_free = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    const uint32_t internal_largest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    const uint32_t psram_free = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    const uint32_t psram_largest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    ESP_LOGI(
        kTag,
        "Lazy GUI %.*s: document(%.*s) loaded_page_documents(%u) "
        "internal_free(%lu) internal_largest(%lu) "
        "psram_free(%lu) psram_largest(%lu)",
        static_cast<int>(operation.size()),
        operation.data(),
        static_cast<int>(document_path.size()),
        document_path.data(),
        static_cast<unsigned>(loaded_document_count),
        static_cast<unsigned long>(internal_free),
        static_cast<unsigned long>(internal_largest),
        static_cast<unsigned long>(psram_free),
        static_cast<unsigned long>(psram_largest)
    );
}

std::expected<void, std::string> App::unload_active_screen_document(
    AppContext &context
)
{
    if (!active_screen_document_id_.has_value()) {
        return {};
    }

    const auto document_id = *active_screen_document_id_;
    ++screen_document_generation_;
    screen_document_connections_.clear();
    if (!context.gui().unload(document_id)) {
        return std::unexpected("Failed to unload active LUNASHELL screen document");
    }
    active_screen_document_id_.reset();
    log_screen_document_heap("unloaded", screen_, 0U);
    return {};
}

std::expected<void, std::string> App::activate_screen_document(
    AppContext &context,
    Screen screen
)
{
    if (screen == Screen::Welcome &&
        !active_screen_document_id_.has_value()) {
        auto result = context.gui().trigger_screen_flow(
            kScreenFlow,
            "LUNASHELL.open.welcome"
        );
        if (!result) {
            return std::unexpected(
                "Failed to mount resident Welcome screen: " + result.error()
            );
        }
        return {};
    }
    if (active_screen_document_id_.has_value() &&
        shares_lazy_document(screen_, screen)) {
        return {};
    }

    auto release_result = context.gui().trigger_screen_flow(
        kScreenFlow,
        kReleaseContentAction
    );
    if (!release_result) {
        return std::unexpected(
            "Failed to release current LUNASHELL content: " + release_result.error()
        );
    }
    log_screen_document_heap(
        "released_to_idle",
        screen_,
        active_screen_document_id_.has_value() ? 1U : 0U
    );

    if (auto result = unload_active_screen_document(context); !result) {
        return result;
    }

    if (screen == Screen::Welcome) {
        auto welcome_result = context.gui().trigger_screen_flow(
            kScreenFlow,
            "LUNASHELL.open.welcome"
        );
        if (!welcome_result) {
            return std::unexpected(
                "Failed to restore resident Welcome screen: " +
                welcome_result.error()
            );
        }
        log_screen_document_heap("mounted", screen, 0U);
        return {};
    }

    const auto descriptor = lazy_screen_descriptor(screen);
    if (!descriptor.has_value()) {
        return std::unexpected("No lazy GUI document is registered for route");
    }

    auto load_result = context.gui().load_file(descriptor->document_path);
    if (!load_result) {
        return std::unexpected(
            "Failed to load LUNASHELL screen document '" +
            std::string(descriptor->document_path) + "': " +
            load_result.error()
        );
    }
    const auto document_id = *load_result;
    log_screen_document_heap("loaded", screen, 1U);

    const uint32_t generation = screen_document_generation_ + 1U;
    const auto retain_document_for_cleanup_retry = [&]() {
        active_screen_document_id_ = document_id;
        screen_document_generation_ = generation;
        log_screen_document_heap("cleanup_deferred", screen, 1U);
    };
    std::vector<esp_brookesia::gui::ScopedConnection> connections;
    auto subscription_result = subscribe_screen_document_actions(
        context,
        document_id,
        screen,
        generation,
        connections
    );
    if (!subscription_result) {
        connections.clear();
        const bool unloaded = context.gui().unload(document_id);
        if (!unloaded) {
            retain_document_for_cleanup_retry();
        }
        return std::unexpected(
            subscription_result.error() +
            (unloaded ? "" : "; document cleanup also failed")
        );
    }

    auto mount_result = context.gui().mount_screen(
        document_id,
        descriptor->screen_path
    );
    if (!mount_result) {
        connections.clear();
        const bool unloaded = context.gui().unload(document_id);
        if (!unloaded) {
            retain_document_for_cleanup_retry();
        }
        return std::unexpected(
            "Failed to mount LUNASHELL screen '" +
            std::string(descriptor->screen_path) + "': " +
            mount_result.error() +
            (unloaded ? "" : "; document cleanup also failed")
        );
    }

    active_screen_document_id_ = document_id;
    screen_document_generation_ = generation;
    screen_document_connections_ = std::move(connections);
    invalidate_screen_document_state(screen);
    log_screen_document_heap("mounted", screen, 1U);
    return {};
}

std::expected<void, std::string> App::restore_welcome_document(
    AppContext &context
)
{
    std::string rollback_error;
    const auto remember_failure = [&](std::string_view operation,
                                      std::string_view error) {
        ESP_LOGE(
            kTag,
            "Welcome rollback %.*s failed: %.*s",
            static_cast<int>(operation.size()),
            operation.data(),
            static_cast<int>(error.size()),
            error.data()
        );
        if (!rollback_error.empty()) {
            rollback_error += "; ";
        }
        rollback_error += std::string(operation) + " failed: " +
                          std::string(error);
    };

    auto welcome_result = context.gui().trigger_screen_flow(
        kScreenFlow,
        "LUNASHELL.open.welcome"
    );
    if (!welcome_result) {
        remember_failure(
            "resident Welcome restore",
            welcome_result.error()
        );
    }
    if (auto unload_result = unload_active_screen_document(context);
        !unload_result) {
        remember_failure("lazy document unload", unload_result.error());
    }
    if (!welcome_result) {
        return std::unexpected(std::move(rollback_error));
    }

    screen_ = Screen::Welcome;
#if CONFIG_LUNASHELL_BUILD_PROFILE_LEG_EDITION && CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    const esp_err_t leg_policy_result =
        LUNASHELL_power_manager_set_leg_mode(false, leg_wake_enabled_);
    if (leg_policy_result != ESP_OK) {
        remember_failure(
            "Leg power policy rollback",
            esp_err_to_name(leg_policy_result)
        );
    }
#endif
    navigation_.reset(Screen::Welcome);
    pairing_visual_state_.reset();
    content_viewport_state_.reset();
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
    invalidate_hardware_screen_cache();
    invalidate_imu_screen_cache();
    invalidate_leg_screen_cache();
    invalidate_storage_screen_cache();

    if (auto result = apply_diagnostic_monitor(context, Screen::Welcome);
        !result) {
        remember_failure("diagnostic policy", result.error());
    }
    if (auto result = configure_chrome(context); !result) {
        remember_failure("chrome configuration", result.error());
    }
    if (auto result = refresh(context); !result) {
        remember_failure("Welcome refresh", result.error());
    }
    if (!rollback_error.empty()) {
        return std::unexpected(std::move(rollback_error));
    }
    return {};
}

std::expected<void, std::string> App::set_text(
    AppContext &context,
    std::string_view path,
    std::string_view text
)
{
    const bool use_screen_document =
        screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value();
    auto result = use_screen_document
        ? context.gui().set_text(*active_screen_document_id_, path, text)
        : context.gui().set_text(path, text);
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL view '" + std::string(path) + "': " +
            result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::set_binding_values(
    AppContext &context,
    const std::vector<esp_brookesia::gui::BindingValueUpdate> &updates
)
{
    const bool use_screen_document =
        screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value();
    auto result = use_screen_document
        ? context.gui().set_binding_values(
              *active_screen_document_id_,
              updates
          )
        : context.gui().set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL bindings: " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::set_value(
    AppContext &context,
    std::string_view path,
    int32_t value
)
{
    if (value < 0 || value > 100) {
        return std::unexpected(
            "Rejected out-of-range LUNASHELL value '" + std::string(path) +
            "': " + std::to_string(value)
        );
    }
    if (screen_ == Screen::Welcome ||
        !active_screen_document_id_.has_value()) {
        auto result = context.gui().set_value(path, value);
        if (!result) {
            return std::unexpected(
                "Failed to update LUNASHELL value '" + std::string(path) +
                "': " + result.error()
            );
        }
        return {};
    }
    return set_binding_values(
        context,
        {
            {
                .absolute_path = std::string(path),
                .key = "value",
                .value = std::to_string(value),
            },
        }
    );
}

std::expected<void, std::string> App::set_hidden(
    AppContext &context,
    std::string_view path,
    bool hidden
)
{
    if (screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value()) {
        return set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(path),
                    .key = "hidden",
                    .value = hidden ? "true" : "false",
                },
            }
        );
    }
    auto result = context.gui().set_binding_value(
        path,
        "hidden",
        hidden ? "true" : "false"
    );
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL visibility '" + std::string(path) +
            "': " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::set_binding_text(
    AppContext &context,
    std::string_view path,
    std::string_view binding,
    std::string_view value
)
{
    if (screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value()) {
        return set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(path),
                    .key = std::string(binding),
                    .value = std::string(value),
                },
            }
        );
    }
    auto result = context.gui().set_binding_value(
        path,
        binding,
        std::string(value)
    );
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL binding '" + std::string(path) + "/" +
            std::string(binding) + "': " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::scroll_to(
    AppContext &context,
    std::string_view path,
    int32_t x,
    int32_t y,
    bool animated
)
{
    return screen_ != Screen::Welcome &&
           active_screen_document_id_.has_value()
        ? context.gui().scroll_to(
              *active_screen_document_id_,
              path,
              x,
              y,
              animated
          )
        : context.gui().scroll_to(path, x, y, animated);
}

esp_brookesia::system::core::AppManifest App::get_manifest() const
{
    return {
        .id = std::string(kAppManifestId),
        .name = "ESP32 Luna Shell",
        .localized_names = {
            {"en", "ESP32 Luna Shell"},
            {"it", "ESP32 Luna Shell"},
        },
        .version = "0.8.0",
        .kind = esp_brookesia::system::core::AppKind::Native,
        .visible = true,
        .preload_dom = false,
        .icon_id = "",
        .supported_systems = {},
        .icon_path = "",
        .runtime_type = esp_brookesia::runtime::BackendType::Unknown,
        .app_path = "",
        .entry = "",
        .resource_dir = std::string(kAppManifestId),
        .arguments = {},
    };
}

esp_brookesia::system::core::AppGuiDescriptor App::get_gui_descriptor() const
{
    return {
        .root_kind = esp_brookesia::system::core::GuiRootKind::File,
        .root = kGuiRoot,
        .resources = {},
        .screen_flows = {
            {
                .screen_flow = kBackgroundFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::SystemBottom,
            },
            {
                .screen_flow = kScreenFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::AppDefault,
            },
            {
                .screen_flow = kChromeFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::AppTop,
            },
        },
    };
}

}  // namespace LUNASHELL
