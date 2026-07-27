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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/system_core.hpp"
#include "qsee_adaptive_chrome.hpp"
#include "qsee_alert_service.hpp"
#include "qsee_background_asset.hpp"
#include "qsee_edge_navigation.hpp"
#include "qsee_navigation_service.hpp"
#include "qsee_pairing_qr.hpp"
#include "qsee_status_bar.hpp"

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
    using Screen = Route;

    enum class NavigationMode {
        Push,
        Back,
        Forward,
        Replace,
        Clear,
        ReturnTo,
    };

    struct PairingVisualState {
        Screen screen;
        std::string_view title;
        std::string_view step;
        std::string_view message;
        std::string_view detail;
        bool show_qr;

        bool operator==(const PairingVisualState &) const = default;
    };

    static constexpr std::size_t kAudioUiSpectrumBandCount = 16;

    struct CachedText {
        std::array<char, 256> value{};
        std::size_t length = 0;
        bool valid = false;
    };

    struct AudioMeterCache {
        std::array<int32_t, 2> levels{};
        std::array<bool, 2> level_known{};
        std::array<bool, 2> hidden{};
        std::array<bool, 2> hidden_known{};
    };

    enum class SettingsTextSlot : std::size_t {
        Performance,
        FullSnapshot,
        DiagnosticPeriod,
        ImuSmoothing,
        Storage,
        Radios,
        Wifi,
        Bluetooth,
        Gps,
        Brightness,
        Rotation,
        Count,
    };

    struct SettingsControlsState {
        std::array<int32_t, 8> selected_indexes{};
        bool performance_checked = false;
        bool mirror_checked = false;
        bool mirror_disabled = false;
        bool rotation_checked = false;
        bool rotation_disabled = false;

        bool operator==(const SettingsControlsState &) const = default;
    };

    std::expected<void, std::string> open_screen(
        esp_brookesia::system::core::AppContext &context,
        Screen screen,
        std::string_view action,
        NavigationMode mode = NavigationMode::Push
    );
    // Register the route-specific command and status paths with the adaptive controller.
    std::expected<void, std::string> configure_chrome(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh(
        esp_brookesia::system::core::AppContext &context
    );
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
    std::expected<void, std::string> ensure_log_buffers();
    void release_log_buffers();
    std::expected<void, std::string> refresh_hardware(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_hardware_detail(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> show_hardware_details(
        esp_brookesia::system::core::AppContext &context,
        size_t component_index
    );
    std::expected<void, std::string> refresh_audio(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_audio_fft(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_audio_detail(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> set_cached_text(
        esp_brookesia::system::core::AppContext &context,
        CachedText &cache,
        std::string_view path,
        std::string_view text
    );
    void invalidate_audio_screen_cache(Screen screen);
    std::expected<void, std::string> refresh_imu(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_settings(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> set_settings_text(
        esp_brookesia::system::core::AppContext &context,
        SettingsTextSlot slot,
        std::string_view path,
        std::string_view text
    );
    std::expected<void, std::string> refresh_pairing(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> pairing_next(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> navigate_back(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> start_pairing_session(
        esp_brookesia::system::core::AppContext &context,
        NavigationMode mode = NavigationMode::Push
    );
    std::expected<void, std::string> open_app_store_qr(
        esp_brookesia::system::core::AppContext &context,
        NavigationMode mode = NavigationMode::Push
    );
    std::expected<void, std::string> prepare_pairing_qr(
        esp_brookesia::system::core::AppContext &context,
        const char *payload
    );
    std::expected<void, std::string> release_pairing_qr(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> show_pairing_result(
        esp_brookesia::system::core::AppContext &context,
        std::string message,
        bool success
    );
    static bool is_pairing_screen(Screen screen);
    static std::string_view screen_action(Screen screen);
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
    std::expected<void, std::string> sync_refresh_timer(
        esp_brookesia::system::core::AppContext &context,
        DisplayIdleStage stage
    );
    std::expected<void, std::string> note_activity(
        esp_brookesia::system::core::AppContext &context
    );
    void handle_chrome_timing_event(
        AdaptiveChromeTiming timing,
        const esp_brookesia::gui::Event &event
    );
    void handle_full_snapshot_period_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_diagnostic_period_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_imu_smoothing_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_brightness_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_performance_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_storage_mirror_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_auto_rotation_event(
        const esp_brookesia::gui::Event &event
    );
    void finish_settings_event(
        esp_err_t result,
        std::string_view operation
    );

    esp_brookesia::system::core::AppContext *context_ = nullptr;
    esp_brookesia::system::core::TimerId refresh_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    esp_brookesia::system::core::TimerId power_state_sync_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    uint32_t refresh_period_ms_ = 0;
    esp_brookesia::system::core::KeyboardRequestId search_keyboard_ =
        esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    Screen screen_ = Screen::Welcome;
    bool logs_paused_ = false;
    bool logs_follow_latest_ = true;
    bool performance_mode_enabled_ = false;
    bool audio_diagnostic_capture_active_ = false;
    bool audio_detail_palette_applied_ = false;
    CachedText audio_status_cache_;
    CachedText audio_values_cache_;
    AudioMeterCache audio_meter_cache_;
    CachedText audio_fft_status_cache_;
    std::array<int16_t, kAudioUiSpectrumBandCount> audio_fft_heights_{};
    bool audio_fft_heights_valid_ = false;
    bool audio_fft_hidden_ = true;
    bool audio_fft_hidden_known_ = false;
    CachedText audio_detail_status_cache_;
    std::array<
        int16_t,
        kAudioUiSpectrumBandCount * 2U
    > audio_detail_heights_{};
    bool audio_detail_heights_valid_ = false;
    bool audio_detail_hidden_ = true;
    bool audio_detail_hidden_known_ = false;
    bool pairing_qr_registered_ = false;
    bool pairing_result_success_ = false;
    std::optional<PairingVisualState> pairing_visual_state_;
    std::vector<esp_brookesia::gui::ScopedConnection>
        chrome_timing_connections_;
    AlertService alert_service_;
    AdaptiveChromeController adaptive_chrome_;
    bool background_asset_registered_ = false;
    EdgeNavigation edge_navigation_;
    NavigationService navigation_;
    PairingQrAsset pairing_qr_;
    QSeeStatusBar status_bar_;
    uint8_t log_filter_mode_ = 0;
    int64_t diagnostic_activation_sample_at_us_ = 0;
    int64_t pairing_started_at_us_ = 0;
    std::string log_query_;
    std::string pairing_result_;
    std::string hardware_detail_title_;
    std::string hardware_detail_body_;
    std::array<
        std::string,
        static_cast<std::size_t>(SettingsTextSlot::Count)
    > settings_text_cache_;
    bool settings_text_cache_valid_ = false;
    std::optional<SettingsControlsState> settings_controls_state_;
    std::unique_ptr<char[]> log_source_buffer_;
    std::unique_ptr<char[]> log_filtered_buffer_;
};

}  // namespace qsee
