#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gui/lvgl/esp_brookesia_app_widgets.hpp"

namespace qsee {

class Navigation final {
public:
<<<<<<< HEAD
    enum class Screen { Welcome, Logs, Hardware, Audio, Imu, About };

    struct Metric {
        std::unique_ptr<esp_brookesia::gui::LvContainer> card;
        std::unique_ptr<esp_brookesia::gui::AppLabel> label;
        std::unique_ptr<esp_brookesia::gui::AppLabel> value;
        std::unique_ptr<esp_brookesia::gui::AppProgressBar> bar;
=======
    enum class Screen {
        Home,
        Confirmation,
        Result,
        AudioDemo,
        ImuDemo,
>>>>>>> b9e2aa7 (Add audio scope and IMU calibration demo)
    };

    bool begin();
    bool back();
    void shutdown();

private:
    void show(Screen screen);
    void showWelcome();
    void showLogs();
    void showHardware();
    void showAudio();
    void showImu();
    void showAbout();
    void refresh();
    void setVisible(esp_brookesia::gui::LvObject &object, bool visible);
    void updateMetric(Metric &metric, const char *value, int progress);
    void updateHardwareText();
    void updateLogView();
    void showComponentDetails(size_t index);
    void showFormatDialog();
    void closeDialog();
    void requestFormat();
    void updateDiagnosticViews();

<<<<<<< HEAD
    std::unique_ptr<esp_brookesia::gui::AppScreen> _screen;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _surface;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _page_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _page_subtitle;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _welcome;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _logs;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _hardware;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _audio;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _imu;
    std::vector<std::unique_ptr<esp_brookesia::gui::LvContainer>> _hardware_cards;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppButton>> _hardware_actions;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_logs;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_hardware;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_audio;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_imu;
    std::unique_ptr<esp_brookesia::gui::AppButton> _open_about;
    std::unique_ptr<esp_brookesia::gui::AppButton> _back;
    std::unique_ptr<esp_brookesia::gui::AppButton> _format_sd;
    std::unique_ptr<esp_brookesia::gui::AppButton> _sd_action;
    std::unique_ptr<esp_brookesia::gui::AppButton> _logs_pause;
    std::unique_ptr<esp_brookesia::gui::AppButton> _logs_clear;
    std::unique_ptr<esp_brookesia::gui::AppButton> _logs_follow;
    std::unique_ptr<esp_brookesia::gui::AppTextBox> _log_search;
    std::unique_ptr<esp_brookesia::gui::AppButton> _log_filter;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _logs_live;
    std::unique_ptr<esp_brookesia::gui::AppRichText> _log_box;
    std::vector<std::unique_ptr<esp_brookesia::gui::AppLabel>> _hardware_labels;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _hardware_summary;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _about;
    std::unique_ptr<esp_brookesia::gui::AppImage> _about_logo;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_tagline;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_website;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_email;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _about_copyright;
    std::unique_ptr<esp_brookesia::gui::LvContainer> _dialog;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _dialog_title;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _dialog_text;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _dialog_progress;
    std::unique_ptr<esp_brookesia::gui::AppButton> _dialog_confirm;
    std::unique_ptr<esp_brookesia::gui::AppButton> _dialog_cancel;
    Metric _ram;
    Metric _sd;
    Metric _cpu0;
    Metric _cpu1;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _radio_status;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _audio_status;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _audio_channels;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _audio_mic0;
    std::unique_ptr<esp_brookesia::gui::AppProgressBar> _audio_mic1;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_status;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_accel;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_gyro;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_temperature;
    std::unique_ptr<esp_brookesia::gui::AppLabel> _imu_calibration;
    std::unique_ptr<esp_brookesia::gui::AppTimer> _refresh_timer;
    bool _logs_paused = false;
    bool _logs_follow_latest = true;
    uint8_t _log_filter_mode = 0;
    size_t _selected_component = 0;
    Screen _current{Screen::Welcome};
=======
    static void primaryEvent(lv_event_t *event);
    static void confirmEvent(lv_event_t *event);
    static void cancelEvent(lv_event_t *event);
    static void statusTimer(lv_timer_t *timer);
    static void audioTimer(lv_timer_t *timer);
    static void imuTimer(lv_timer_t *timer);
    static void imuEvent(lv_event_t *event);

    static const char *screenName(Screen screen);
    void navigate(Route route, bool push_current);
    void render();
    void renderStatus();
    void showHome();
    void showConfirmation();
    void showResult(const char *message);
    void showAudioDemo();
    void renderAudioDemo();
    void showImuDemo();
    void renderImuDemo();
    void setActionButtons(bool visible);
    lv_obj_t *makeActionButton(const char *text, int x, lv_event_cb_t callback);

    lv_obj_t *_root{};
    lv_obj_t *_safe_area{};
    lv_obj_t *_status_bar{};
    lv_obj_t *_bluetooth_status{};
    lv_obj_t *_wifi_status{};
    lv_obj_t *_gps_status{};
    lv_obj_t *_battery_status{};
    lv_obj_t *_title{};
    lv_obj_t *_description{};
    lv_obj_t *_primary{};
    lv_obj_t *_cancel{};
    lv_obj_t *_confirm{};
    Route _current;
    std::vector<Route> _history;
    lv_timer_t *_status_timer{};
    lv_timer_t *_audio_timer{};
    lv_obj_t *_audio_view{};
    lv_obj_t *_audio_mic1{};
    lv_obj_t *_audio_mic2{};
    lv_obj_t *_audio_cpu0{};
    lv_obj_t *_audio_cpu1{};
    lv_obj_t *_audio_imu_button{};
    uint32_t _audio_tick{};
    lv_timer_t *_imu_timer{};
    lv_obj_t *_imu_view{};
    lv_obj_t *_imu_bubble{};
    lv_obj_t *_imu_accel{};
    lv_obj_t *_imu_gyro{};
    lv_obj_t *_imu_temp{};
    lv_obj_t *_imu_status{};
    lv_obj_t *_imu_compass{};
>>>>>>> b9e2aa7 (Add audio scope and IMU calibration demo)
};

}  // namespace qsee
