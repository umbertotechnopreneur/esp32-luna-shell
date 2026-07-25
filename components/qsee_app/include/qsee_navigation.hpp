#pragma once

// Navigation surface for the QSee phone-style app shell.
// Declares the screen router, back handling, and UI refresh hooks.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gui/lvgl/esp_brookesia_app_widgets.hpp"
#include "qsee_adaptive_command_bar.hpp"
#include "qsee_edge_navigation.hpp"

namespace qsee {

struct DeviceSnapshot;

class Navigation final {
public:
    enum class Screen {
        Welcome,
        Logs,
        Hardware,
        Audio,
        Imu,
        About,
        Settings,
        WifiCredentials,
        PairIntro,
        PairBluetooth,
        PairApp,
        PairQr,
        AppStoreQr,
        PairResult,
    };

    struct Metric {
        std::unique_ptr<esp_brookesia::gui::LvContainer> card;
        std::unique_ptr<esp_brookesia::gui::AppLabel> label;
        std::unique_ptr<esp_brookesia::gui::AppLabel> value;
        std::unique_ptr<esp_brookesia::gui::AppProgressBar> bar;
    };

    struct DeviceLine {
        std::unique_ptr<esp_brookesia::gui::LvContainer> row;
        std::unique_ptr<esp_brookesia::gui::AppLabel> icon;
        std::unique_ptr<esp_brookesia::gui::AppLabel> name;
        std::unique_ptr<esp_brookesia::gui::AppLabel> value;
    };

    bool begin();
    bool back();
    void shutdown();

private:
    /** Build one vertically scrollable Settings surface with native controls. */
    void buildSettingsUi();
    /** Route screens and stop provisioning only when leaving its full flow. */
    void show(Screen screen);
    void showAudio();
    void showImu();
    void refresh();
    void setVisible(esp_brookesia::gui::LvObject &object, bool visible);
    void updateMetric(Metric &metric, const char *value, int progress);
    void updateWelcomeDevices(const qsee::DeviceSnapshot &snapshot);
    void updateHardwareText();
    void updateLogView();
    void showComponentDetails(size_t index);
    void showFormatDialog();
    void closeDialog();
    void requestFormat();
    void updateDiagnosticViews();
    /** Smooth live dBFS levels and maintain each peak-hold marker. */
    void updateAudioMeters(bool reset);
    /** Refresh fast animations while throttling the heavier full snapshot. */
    void onRefreshTick();
    /** Keep the full-screen wake guard above controls while the panel is dark. */
    void updateDisplayWakeGuard();
    /** Consume the first dark-screen touch and request asynchronous recovery. */
    static void displayWakeGuardEvent(lv_event_t *event);
    /** Apply a user selection from the unified Settings controls. */
    static void settingsControlChangedEvent(lv_event_t *event);
    /** Persist the dropdown's lower-command visibility timeout. */
    static void commandBarTimeoutChangedEvent(lv_event_t *event);
    /** Refresh snapshot status and synchronize persisted Settings controls. */
    void updateSettings();
    /** Load only the SSID; a blank password preserves the saved secret. */
    void prepareWifiEditor();
    void saveWifiCredentials();
    /** Scrub temporary password text before leaving the editor. */
    void clearWifiPassword();
    static void wifiFieldFocusEvent(lv_event_t *event);
    /** Drop scan metadata when the user edits a preselected SSID. */
    static void wifiSsidChangedEvent(lv_event_t *event);
    /** Mark only user edits as password replacements. */
    static void wifiPasswordChangedEvent(lv_event_t *event);
    void startPairing();
    void showPairingBluetooth();
    void showPairingApp();
    /** Start BLE provisioning only when entering the device QR stage. */
    void showPairingQr();
    void showAppStoreQr();
    void showPairingResult(const char *message, bool success);
    void pairingPrimaryAction();
    void pairingSecondaryAction();
    /** Render the current pairing stage without changing transport state. */
    void renderPairing();
    /** Generate a QR into the reusable LVGL canvas buffer. */
    bool renderQr(const char *payload);
    /** Translate provisioning events into progress or result screens. */
    void updatePairingState();
    /** Rasterize the QR library callback into the active canvas. */
    static void qrDisplayCallback(const uint8_t *qrcode);

    std::unique_ptr<esp_brookesia::gui::AppScreen> _screen;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _surface;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _title_bar;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _page_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _page_subtitle;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _welcome;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _logs;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _hardware;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _audio;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _imu;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _settings;
    std::vector<std::unique_ptr<esp_brookesia::gui::LvContainer>> _hardware_rows;
    std::vector<std::unique_ptr<esp_brookesia::gui::LvContainer>> _hardware_status_dots;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppButton>> _hardware_actions;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppLabel>> _content_labels;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_logs;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_hardware;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_audio;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_imu;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_about;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_settings;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _welcome_ip;
    std::vector<DeviceLine> _welcome_devices;
    std::unique_ptr<EdgeNavigation> _edge_navigation;
    std::unique_ptr<esp_brookesia::gui::AppButton> _format_sd;
    std::unique_ptr<esp_brookesia::gui::AppButton> _sd_action;
    std::unique_ptr<esp_brookesia::gui::AppButton> _logs_pause;
    std::unique_ptr<esp_brookesia::gui::AppButton> _logs_clear;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _logs_live;
    std::unique_ptr<esp_brookesia::gui::AppRichText> _log_box;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppLabel>> _hardware_labels;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppLabel>> _hardware_parts;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _hardware_summary;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _about;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_tagline;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_website;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_email;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_copyright;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_period;
    lv_obj_t *_settings_period_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_smoothing;
    lv_obj_t *_settings_smoothing_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_storage;
    lv_obj_t *_settings_sd_mirror_switch = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_wifi;
    lv_obj_t *_settings_wifi_action_dropdown = nullptr;
    lv_obj_t *_settings_wifi_networks_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_ble;
    lv_obj_t *_settings_ble_switch = nullptr;
    lv_obj_t *_settings_ble_action_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_gps;
    lv_obj_t *_settings_gps_switch = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_brightness;
    lv_obj_t *_settings_brightness_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_dim_timeout;
    lv_obj_t *_settings_dim_timeout_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_panel_off_delay;
    lv_obj_t *_settings_panel_off_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_command_bar_timeout;
    lv_obj_t *_settings_command_bar_timeout_dropdown = nullptr;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _settings_rotation;
    lv_obj_t *_settings_auto_rotation_switch = nullptr;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _wifi_editor;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _wifi_editor_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _wifi_editor_message;
    std::unique_ptr<esp_brookesia::gui::AppTextBox> _wifi_ssid;
    std::unique_ptr<esp_brookesia::gui::AppTextBox> _wifi_password;
    std::unique_ptr<esp_brookesia::gui::AppButton> _wifi_password_visibility;
    std::unique_ptr<esp_brookesia::gui::AppButton> _wifi_editor_cancel;
    std::unique_ptr<esp_brookesia::gui::AppButton> _wifi_editor_save;
    lv_obj_t *_wifi_keyboard = nullptr;
    bool _wifi_editor_loading = false;
    bool _wifi_password_dirty = false;
    bool _wifi_password_visible = false;
    bool _wifi_selected_network_open = false;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _pairing;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _pairing_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _pairing_message;
    std::unique_ptr<esp_brookesia::gui::AppButton> _pairing_secondary;
    std::unique_ptr<esp_brookesia::gui::AppButton> _pairing_primary;
    lv_obj_t *_pairing_qr_canvas = nullptr;
    uint8_t *_pairing_qr_buffer = nullptr;
    int64_t _pairing_started_at_us = 0;
    bool _pairing_result_success = false;
    char _pairing_result[160] = {};
    uint32_t _settings_wifi_scan_sequence = 0xFFFFFFFFU;
    bool _settings_wifi_scanning = false;
    static Navigation *_qr_owner;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _dialog_scrim;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _dialog;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _display_wake_guard;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _dialog_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _dialog_text;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _dialog_progress;
    std::unique_ptr<esp_brookesia::gui::AppButton> _dialog_confirm;
    std::unique_ptr<esp_brookesia::gui::AppButton> _dialog_cancel;
    Metric _ram;
    Metric _sd;
    Metric _cpu0;
    Metric _cpu1;
    Metric _psram;
    Metric _flash;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _audio_status;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _audio_channels;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _audio_mic0;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _audio_mic1;
    // Per channel: [0] displayed level, [1] peak-hold marker (0..100 dBFS scale).
    float _audio_meter_visual[2][2] = {};
    int64_t _audio_peak_hold_until_us[2] = {};
    int64_t _audio_meter_updated_at_us = 0;
    uint16_t _refresh_tick_count = 0;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_status;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_accel_x;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_accel_y;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_accel_z;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_gyro_x;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_gyro_y;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_gyro_z;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_temperature;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_calibration;
    lv_obj_t *_imu_gyro_arcs[3] = {};
    std::unique_ptr<esp_brookesia::gui::AppTimer> _refresh_timer;
    std::unique_ptr<AdaptiveCommandBar> _adaptive_command_bar;
    bool _logs_paused = false;
    bool _dialog_requires_confirmation = false;
    size_t _selected_component = 0;
    char *_log_snapshot_buffer = nullptr;
    Screen _current{Screen::Welcome};
};

}  // namespace qsee
