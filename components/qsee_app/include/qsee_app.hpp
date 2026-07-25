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

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"
#include "qsee_adaptive_command_bar.hpp"

namespace qsee {

inline constexpr std::string_view kAppManifestId = "ai.qsee.device-console";

struct DeviceSnapshot;

class App final : public esp_brookesia::system::core::IApp {
public:
    esp_brookesia::system::core::AppManifest get_manifest() const override;
    esp_brookesia::system::core::AppGuiDescriptor get_gui_descriptor() const override;

    std::expected<void, std::string> on_start(
        esp_brookesia::system::core::AppContext &context
    ) override;
    std::expected<void, std::string> on_pause(
        esp_brookesia::system::core::AppContext &context
    ) override;
    std::expected<void, std::string> on_resume(
        esp_brookesia::system::core::AppContext &context
    ) override;
    std::expected<void, std::string> on_stop(
        esp_brookesia::system::core::AppContext &context
    ) override;
    std::expected<void, std::string> on_action(
        esp_brookesia::system::core::AppContext &context,
        std::string_view action
    ) override;
    std::expected<void, std::string> on_timer(
        esp_brookesia::system::core::AppContext &context,
        esp_brookesia::system::core::TimerId timer_id,
        std::string_view name
    ) override;

private:
    enum class Screen {
        Welcome,
        Logs,
        Hardware,
        Audio,
        AudioFft,
        Imu,
        Settings,
        About,
    };

    std::expected<void, std::string> open_screen(
        esp_brookesia::system::core::AppContext &context,
        Screen screen,
        std::string_view action
    );
    // Register the route-specific command and status paths with the adaptive controller.
    std::expected<void, std::string> configure_adaptive_chrome(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh(
        esp_brookesia::system::core::AppContext &context
    );
    // Register and bind the PSRAM background only after its worker publishes it.
    std::expected<void, std::string> refresh_background(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_chrome(
        esp_brookesia::system::core::AppContext &context,
        const DeviceSnapshot &snapshot
    );
    std::expected<void, std::string> refresh_welcome(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_logs(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_hardware(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_audio(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_audio_fft(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_imu(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_settings(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> request_log_search(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> request_sd_format(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> show_error_dialog(
        esp_brookesia::system::core::AppContext &context,
        std::string_view title,
        std::string_view details
    );
    std::expected<void, std::string> apply_diagnostic_monitor(
        esp_brookesia::system::core::AppContext &context,
        Screen screen
    );

    esp_brookesia::system::core::AppContext *context_ = nullptr;
    esp_brookesia::system::core::TimerId refresh_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    esp_brookesia::system::core::KeyboardRequestId search_keyboard_ =
        esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    esp_brookesia::system::core::MessageDialogRequestId format_dialog_ =
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    esp_brookesia::system::core::MessageDialogRequestId error_dialog_ =
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    Screen screen_ = Screen::Welcome;
    bool logs_paused_ = false;
    bool logs_follow_latest_ = true;
    bool background_registered_ = false;
    bool background_bound_ = false;
    AdaptiveCommandBar adaptive_command_bar_;
    uint8_t log_filter_mode_ = 0;
    int64_t diagnostic_activation_sample_at_us_ = 0;
    std::string log_query_;
    std::unique_ptr<char[]> log_source_buffer_;
    std::unique_ptr<char[]> log_filtered_buffer_;
};

}  // namespace qsee
