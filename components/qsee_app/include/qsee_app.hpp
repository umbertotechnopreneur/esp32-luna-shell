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
        std::string_view accent_color;
        bool show_qr;
        int32_t progress_value;
        bool progress_hidden;

        bool operator==(const PairingVisualState &) const = default;
    };

    static constexpr std::size_t kAudioUiSpectrumBandCount = 16;

    template <std::size_t Capacity>
    struct FixedTextCache {
        std::array<char, Capacity> value{};
        std::size_t length = 0;
        bool valid = false;
    };

    using CachedText = FixedTextCache<256>;

    struct AudioMeterCache {
        std::array<int32_t, 2> levels{};
        std::array<bool, 2> level_known{};
        std::array<bool, 2> hidden{};
        std::array<bool, 2> hidden_known{};
    };

#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::size_t kHardwareUiComponentCount = 13;
#else
    static constexpr std::size_t kHardwareUiComponentCount = 12;
#endif

    struct HardwareUiCache {
        std::array<
            FixedTextCache<48>,
            kHardwareUiComponentCount
        > statuses{};
        std::array<
            FixedTextCache<8>,
            kHardwareUiComponentCount
        > colors{};
        FixedTextCache<64> summary;
    };

    struct ImuUiCache {
        FixedTextCache<160> status;
        FixedTextCache<96> calibration;
        std::array<FixedTextCache<32>, 3> acceleration{};
        std::array<FixedTextCache<32>, 3> angular_velocity{};
        std::array<FixedTextCache<4>, 3> arc_opacity{};
        std::array<bool, 3> arc_hidden{};
        std::array<bool, 3> arc_hidden_known{};
        FixedTextCache<32> temperature;
    };

    struct StorageUiCache {
        FixedTextCache<32> status;
        FixedTextCache<8> status_color;
        FixedTextCache<192> detail;
        FixedTextCache<64> mount;
        FixedTextCache<32> total;
        FixedTextCache<32> free;
        FixedTextCache<64> usage;
        FixedTextCache<32> format_label;
        int32_t usage_percent = 0;
        bool usage_percent_known = false;
        bool usage_hidden = true;
        bool usage_hidden_known = false;
        bool format_disabled = true;
        bool format_disabled_known = false;
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

#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    struct TofSettingsControlsState {
        int32_t sample_count_index = -1;
        int32_t sample_interval_index = -1;
        int32_t reference_distance_mm = -1;
        bool controls_disabled = true;

        bool operator==(const TofSettingsControlsState &) const = default;
    };
#endif

    struct ContentViewportState {
        Screen screen;
        bool command_arc_visible;
        bool status_arc_visible;

        bool operator==(const ContentViewportState &) const = default;
    };

    struct LazyScreenDescriptor {
        std::string_view document_path;
        std::string_view screen_path;
    };

    static std::optional<LazyScreenDescriptor> lazy_screen_descriptor(
        Screen screen
    );
    static bool shares_lazy_document(Screen left, Screen right);
    std::expected<void, std::string> activate_screen_document(
        esp_brookesia::system::core::AppContext &context,
        Screen screen
    );
    std::expected<void, std::string> restore_welcome_document(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> unload_active_screen_document(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> subscribe_screen_document_actions(
        esp_brookesia::system::core::AppContext &context,
        esp_brookesia::gui::DocumentId document_id,
        Screen screen,
        uint32_t generation,
        std::vector<esp_brookesia::gui::ScopedConnection> &connections
    );
    bool is_current_screen_document(
        esp_brookesia::gui::DocumentId document_id,
        uint32_t generation
    ) const;
    void dispatch_screen_document_action(std::string action);
    void invalidate_screen_document_state(Screen screen);
    void log_screen_document_heap(
        std::string_view operation,
        Screen screen,
        uint32_t loaded_document_count
    ) const;
    std::expected<void, std::string> set_text(
        esp_brookesia::system::core::AppContext &context,
        std::string_view path,
        std::string_view text
    );
    std::expected<void, std::string> set_value(
        esp_brookesia::system::core::AppContext &context,
        std::string_view path,
        int32_t value
    );
    std::expected<void, std::string> set_hidden(
        esp_brookesia::system::core::AppContext &context,
        std::string_view path,
        bool hidden
    );
    std::expected<void, std::string> set_binding_text(
        esp_brookesia::system::core::AppContext &context,
        std::string_view path,
        std::string_view binding,
        std::string_view value
    );
    std::expected<void, std::string> set_binding_values(
        esp_brookesia::system::core::AppContext &context,
        const std::vector<esp_brookesia::gui::BindingValueUpdate> &updates
    );
    std::expected<void, std::string> scroll_to(
        esp_brookesia::system::core::AppContext &context,
        std::string_view path,
        int32_t x,
        int32_t y,
        bool animated
    );
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
    std::expected<void, std::string> sync_content_viewport(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_welcome(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_battery(
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
    template <std::size_t Capacity>
    std::expected<void, std::string> set_cached_text(
        esp_brookesia::system::core::AppContext &context,
        FixedTextCache<Capacity> &cache,
        std::string_view path,
        std::string_view text
    );
    template <std::size_t Capacity>
    std::expected<void, std::string> set_cached_binding_text(
        esp_brookesia::system::core::AppContext &context,
        FixedTextCache<Capacity> &cache,
        std::string_view path,
        std::string_view binding,
        std::string_view value
    );
    void invalidate_audio_screen_cache(Screen screen);
    void invalidate_hardware_screen_cache();
    void invalidate_imu_screen_cache();
    void invalidate_storage_screen_cache();
    std::expected<void, std::string> refresh_imu(
        esp_brookesia::system::core::AppContext &context
    );
#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    std::expected<void, std::string> refresh_tof(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_tof_settings(
        esp_brookesia::system::core::AppContext &context
    );
#endif
    std::expected<void, std::string> refresh_settings(
        esp_brookesia::system::core::AppContext &context
    );
    std::expected<void, std::string> refresh_storage(
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
#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    void handle_tof_sample_count_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_tof_sample_interval_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_tof_reference_preview_event(
        const esp_brookesia::gui::Event &event
    );
    void handle_tof_reference_commit_event(
        const esp_brookesia::gui::Event &event
    );
#endif
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
#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    void finish_tof_settings_event(
        esp_err_t result,
        std::string_view operation
    );
#endif

    enum class StandbyUiState : uint8_t {
        Active,
        Quiescing,
        Quiesced,
    };

    esp_brookesia::system::core::AppContext *context_ = nullptr;
    esp_brookesia::system::core::TimerId refresh_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    esp_brookesia::system::core::TimerId power_state_sync_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    uint32_t refresh_period_ms_ = 0;
    StandbyUiState standby_ui_state_ = StandbyUiState::Active;
    uint32_t standby_ui_generation_ = 0;
    uint32_t standby_ui_transition_sequence_ = 0;
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
    HardwareUiCache hardware_ui_cache_;
    ImuUiCache imu_ui_cache_;
    StorageUiCache storage_ui_cache_;
    bool storage_format_request_pending_ = false;
    uint32_t storage_format_request_attempt_ = 0;
    bool pairing_qr_registered_ = false;
    bool pairing_result_success_ = false;
    std::optional<PairingVisualState> pairing_visual_state_;
    std::optional<esp_brookesia::gui::DocumentId>
        active_screen_document_id_;
    std::vector<esp_brookesia::gui::ScopedConnection>
        screen_document_connections_;
    uint32_t screen_document_generation_ = 0;
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
#if CONFIG_QSEE_BUILD_ENABLE_DISTANCE && CONFIG_QSEE_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    std::optional<TofSettingsControlsState> tof_settings_controls_state_;
    std::optional<int32_t> tof_reference_pending_mm_;
    bool tof_reference_editing_ = false;
#endif
    std::optional<ContentViewportState> content_viewport_state_;
    std::unique_ptr<char[]> log_source_buffer_;
    std::unique_ptr<char[]> log_filtered_buffer_;
};

}  // namespace qsee
