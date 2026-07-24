#include "qsee_navigation.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {
using namespace esp_brookesia::gui;

extern const uint8_t qsee_boot_rgb565_start[] asm("_binary_qsee_boot_rgb565_start");
extern const uint8_t qsee_boot_rgb565_end[] asm("_binary_qsee_boot_rgb565_end");

constexpr uint32_t kBackground = 0xF4F8FC;
constexpr uint32_t kSurface = 0xFFFFFF;
constexpr uint32_t kNavy = 0x102A43;
constexpr uint32_t kMuted = 0x627D98;
constexpr uint32_t kTeal = 0x0F9D83;
constexpr uint32_t kBlue = 0x3478C8;
constexpr uint32_t kSlate = 0x52697D;
constexpr uint32_t kError = 0xD64545;
constexpr uint32_t kTrack = 0xDCE7F0;
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr int kWidth = 272;
constexpr int kRowWidth = 236;

void surface(LvObject &object)
{
    object.setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, StyleColor::COLOR(kSurface));
}

void labelStyle(AppLabel &label, int width, int size, uint32_t color, StyleAlignType align, int x, int y)
{
    label.setWidth(width);
    label.setFont(StyleFont::SIZE(size));
    label.setTextColor(StyleColor::COLOR(color));
    label.align({align, x, y});
}

void buttonStyle(AppButton &button, int width, int height, uint32_t color)
{
    button.setWidth(width);
    button.setHeight(height);
    button.setFont(StyleFont::SIZE(10));
    button.setTextColor(StyleColor::COLOR(kWhite));
    button.setBackgroundGradient(StyleColor::COLOR(color), StyleColor::COLOR(color == kTeal ? 0x08745F : 0x2A5E9B));
}

std::unique_ptr<AppLabel> makeLabel(const LvObject *parent, const char *text, int width, int size,
                                    uint32_t color, StyleAlignType align, int x, int y)
{
    auto label = std::make_unique<AppLabel>(parent);
    label->setText(text);
    labelStyle(*label, width, size, color, align, x, y);
    return label;
}

void makeMetric(qsee::Navigation::Metric &metric, const LvObject *parent, const char *name, int y,
                uint32_t color)
{
    metric.card = std::make_unique<LvContainer>(parent);
    metric.card->setStyleAttribute(StyleSize::RECT(kRowWidth, 40));
    metric.card->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, y});
    metric.card->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, StyleColor::COLOR(kSurface));
    metric.card->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BORDER, StyleColor::COLOR(kTrack));
    metric.label = makeLabel(metric.card.get(), name, 100, 8, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 10, 3);
    metric.value = makeLabel(metric.card.get(), "--", 116, 12, kNavy, STYLE_ALIGN_TYPE_TOP_RIGHT, -10, 1);
    metric.bar = std::make_unique<AppProgressBar>(metric.card.get());
    metric.bar->setRange(0, 100);
    metric.bar->setValue(0);
    metric.bar->setWidth(kRowWidth);
    metric.bar->setHeight(8);
    metric.bar->setBackgroundColor(StyleColor::COLOR(kTrack));
    metric.bar->setIndicatorGradient(StyleColor::COLOR(color), StyleColor::COLOR(color == kTeal ? 0x08745F : 0x2A5E9B));
    metric.bar->align({STYLE_ALIGN_TYPE_TOP_LEFT, 10, 27});
}

std::unique_ptr<AppProgressBar> makeDiagnosticBar(const LvObject *parent, int y, uint32_t top, uint32_t bottom)
{
    auto bar = std::make_unique<AppProgressBar>(parent);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setWidth(kRowWidth);
    bar->setHeight(12);
    bar->setBackgroundColor(StyleColor::COLOR(kTrack));
    bar->setIndicatorGradient(StyleColor::COLOR(top), StyleColor::COLOR(bottom));
    bar->align({STYLE_ALIGN_TYPE_TOP_MID, 0, y});
    return bar;
}

const char *health(qsee::DeviceHealth value)
{
    return qsee::deviceHealthName(value);
}

uint32_t healthColor(qsee::DeviceHealth value, esp_err_t error = ESP_OK)
{
    switch (value) {
    case qsee::DeviceHealth::Ready: return kTeal;
    case qsee::DeviceHealth::Degraded:
        return (error == ESP_FAIL || error == ESP_ERR_INVALID_STATE || error == ESP_ERR_NO_MEM)
            ? kError : 0xD97706;
    case qsee::DeviceHealth::Missing: return kSlate;
    case qsee::DeviceHealth::Unknown:
    default: return kMuted;
    }
}

bool storageFormatRequired(const qsee::StorageSnapshot &storage)
{
    return storage.card_present && !storage.formatted &&
           (storage.last_error == ESP_FAIL || storage.format_attempted);
}

const char *logFilterName(uint8_t mode)
{
    switch (mode) {
    case 1: return "WARN";
    case 2: return "ERROR";
    case 3: return "INFO";
    default: return "ALL";
    }
}

bool logLineMatches(const char *line, const char *query, uint8_t filter_mode)
{
    const char *first = line;
    while (*first == ' ' || *first == '\t') ++first;
    if (filter_mode == 1 && (*first != 'W' && *first != 'w')) return false;
    if (filter_mode == 2 && (*first != 'E' && *first != 'e')) return false;
    if (filter_mode == 3 && (*first != 'I' && *first != 'i')) return false;
    return query == nullptr || query[0] == '\0' || std::strstr(line, query) != nullptr;
}

void filterLogs(const char *source, const char *query, uint8_t filter_mode,
                char *output, size_t output_size)
{
    if (output == nullptr || output_size == 0) return;
    output[0] = '\0';
    size_t used = 0;
    const char *cursor = source != nullptr ? source : "";
    while (*cursor != '\0' && used + 1 < output_size) {
        const char *line_end = std::strchr(cursor, '\n');
        const size_t raw_length = line_end != nullptr
            ? static_cast<size_t>(line_end - cursor) : std::strlen(cursor);
        const size_t line_length = raw_length < 255U ? raw_length : 255U;
        char line[256];
        std::memcpy(line, cursor, line_length);
        line[line_length] = '\0';
        if (logLineMatches(line, query, filter_mode)) {
            const size_t copy_length = line_length < output_size - used - 1U
                ? line_length : output_size - used - 1U;
            std::memcpy(output + used, line, copy_length);
            used += copy_length;
            if (used + 1U < output_size) output[used++] = '\n';
            output[used] = '\0';
        }
        cursor = line_end != nullptr ? line_end + 1 : cursor + raw_length;
    }
}

}

namespace qsee {

bool Navigation::begin()
{
    _screen = std::make_unique<AppScreen>(AppScreen::active());
    if (_screen == nullptr || !_screen->isValid()) return false;

    _screen->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, StyleColor::COLOR(kBackground));
    _surface = std::make_unique<LvContainer>(_screen.get());
    _surface->setStyleAttribute(StyleSize::RECT(320, 320));
    _surface->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_CENTER, 0, 0});
    surface(*_surface);

    _page_title = makeLabel(_surface.get(), "WELCOME", kWidth, 20, kNavy, STYLE_ALIGN_TYPE_TOP_MID, 0, 10);
    _page_subtitle = makeLabel(_surface.get(), "QSee device console", kWidth, 10, kMuted, STYLE_ALIGN_TYPE_TOP_MID, 0, 34);

    _welcome = std::make_unique<LvContainer>(_surface.get());
    _welcome->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _welcome->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 54});
    makeMetric(_ram, _welcome.get(), "RAM FREE", 0, kTeal);
    makeMetric(_sd, _welcome.get(), "SD STORAGE", 43, kBlue);
    makeMetric(_cpu0, _welcome.get(), "CPU CORE 0", 86, kTeal);
    makeMetric(_cpu1, _welcome.get(), "CPU CORE 1", 129, kBlue);
    _sd_action = std::make_unique<AppButton>(_welcome.get(), [this]() { show(Screen::Hardware); });
    _sd_action->setText("");
    _sd_action->setWidth(kRowWidth);
    _sd_action->setHeight(40);
    _sd_action->setBackgroundGradient(StyleColor::COLOR_WITH_OPACITY(kSurface, 0),
                                      StyleColor::COLOR_WITH_OPACITY(kSurface, 0));
    _sd_action->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 43});
    _radio_status = makeLabel(_welcome.get(), "WiFi --   BLE --   GPS --", kRowWidth, 10, kNavy,
                              STYLE_ALIGN_TYPE_TOP_MID, 0, 177);
    _open_audio = std::make_unique<AppButton>(_welcome.get(), [this]() { show(Screen::Audio); });
    _open_audio->setText("AUDIO SCOPE");
    buttonStyle(*_open_audio, 112, 28, kBlue);
    _open_audio->align({STYLE_ALIGN_TYPE_TOP_LEFT, 4, 202});
    _open_imu = std::make_unique<AppButton>(_welcome.get(), [this]() { show(Screen::Imu); });
    _open_imu->setText("IMU LEVEL");
    buttonStyle(*_open_imu, 112, 28, kTeal);
    _open_imu->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -4, 202});

    _open_logs = std::make_unique<AppButton>(_surface.get(), [this]() { show(Screen::Logs); });
    _open_logs->setText("LIVE LOGS");
    buttonStyle(*_open_logs, 112, 34, kSlate);
    _open_logs->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 18, -14});
    _open_hardware = std::make_unique<AppButton>(_surface.get(), [this]() { show(Screen::Hardware); });
    _open_hardware->setText("HARDWARE");
    buttonStyle(*_open_hardware, 86, 34, kTeal);
    _open_hardware->align({STYLE_ALIGN_TYPE_BOTTOM_MID, 0, -14});
    _open_logs->setWidth(86);
    _open_logs->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 8, -14});
    _open_about = std::make_unique<AppButton>(_surface.get(), [this]() { show(Screen::About); });
    _open_about->setText("ABOUT");
    buttonStyle(*_open_about, 86, 34, kBlue);
    _open_about->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, -8, -14});

    _logs = std::make_unique<LvContainer>(_surface.get());
    _logs->setStyleAttribute(StyleSize::RECT(kWidth, 280));
    _logs->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 34});
    _log_search = std::make_unique<AppTextBox>(_logs.get());
    _log_search->setWidth(164);
    _log_search->setHeight(28);
    _log_search->setOneLine();
    _log_search->setPlaceholder("Search logs");
    _log_search->setFont(StyleFont::SIZE(8));
    _log_search->setTextColor(StyleColor::COLOR(kNavy));
    _log_search->setBackgroundColor(StyleColor::COLOR(kSurface));
    _log_search->align({STYLE_ALIGN_TYPE_TOP_LEFT, 4, 0});
    _log_filter = std::make_unique<AppButton>(_logs.get(), [this]() {
        _log_filter_mode = static_cast<uint8_t>((_log_filter_mode + 1U) % 4U);
        char filter[24];
        std::snprintf(filter, sizeof(filter), "FILTER %s", logFilterName(_log_filter_mode));
        _log_filter->setText(filter);
        updateLogView();
    });
    _log_filter->setText("FILTER ALL");
    buttonStyle(*_log_filter, 96, 26, kBlue);
    _log_filter->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -4, 1});
    _log_box = std::make_unique<AppRichText>(_logs.get());
    _log_box->setWidth(kWidth);
    _log_box->setHeight(180);
    _log_box->setFont(StyleFont::SIZE(10));
    _log_box->setTextColor(StyleColor::COLOR(0xEAF1F6));
    _log_box->setBackgroundColor(StyleColor::COLOR(0x12263A));
    _log_box->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 32});

    _logs_pause = std::make_unique<AppButton>(_logs.get(), [this]() {
        _logs_paused = !_logs_paused;
        _logs_pause->setText(_logs_paused ? "RESUME" : "PAUSE");
    });
    _logs_pause->setText("PAUSE");
    buttonStyle(*_logs_pause, 82, 26, kSlate);
    _logs_pause->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 8, -2});
    _logs_clear = std::make_unique<AppButton>(_logs.get(), [this]() {
        qsee_logger_clear();
        _log_box->setText("", false);
    });
    _logs_clear->setText("CLEAR");
    buttonStyle(*_logs_clear, 70, 26, kBlue);
    _logs_clear->align({STYLE_ALIGN_TYPE_BOTTOM_MID, -4, -2});
    _logs_follow = std::make_unique<AppButton>(_logs.get(), [this]() {
        _logs_follow_latest = !_logs_follow_latest;
        _logs_follow->setText(_logs_follow_latest ? "FOLLOW ON" : "FOLLOW OFF");
    });
    _logs_follow->setText("FOLLOW ON");
    buttonStyle(*_logs_follow, 92, 26, kBlue);
    _logs_follow->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, -8, -2});
    _logs_live = makeLabel(_logs.get(), "● LIVE", 70, 9, kTeal,
                           STYLE_ALIGN_TYPE_TOP_RIGHT, -4, -28);

    _hardware = std::make_unique<LvContainer>(_surface.get());
    _hardware->setStyleAttribute(StyleSize::RECT(kWidth, 280));
    _hardware->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 34});
    const char *hardware_names[] = {
        "[B] BATTERY", "[R] RTC", "[M] IMU", "[G] GPS", "[W] WIFI", "[B] BLE",
        "[D] DISPLAY", "[T] TOUCH", "[S] SPEAKER", "[M] MIC", "[S] SD CARD"
    };
    for (size_t i = 0; i < sizeof(hardware_names) / sizeof(hardware_names[0]); ++i) {
        auto card = std::make_unique<LvContainer>(_hardware.get());
        card->setStyleAttribute(StyleSize::RECT(128, 32));
        const bool left = (i % 2U) == 0U;
        const int row = static_cast<int>(i / 2U);
        card->setStyleAttribute(StyleAlign{
            left ? STYLE_ALIGN_TYPE_TOP_LEFT : STYLE_ALIGN_TYPE_TOP_RIGHT,
            left ? 4 : -4,
            row * 35
        });
        card->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, StyleColor::COLOR(kSurface));
        card->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BORDER, StyleColor::COLOR(kTrack));
        auto item = makeLabel(card.get(), hardware_names[i], 112, 8, kMuted,
                              (i % 2 == 0) ? STYLE_ALIGN_TYPE_TOP_LEFT : STYLE_ALIGN_TYPE_TOP_RIGHT,
                              (i % 2 == 0) ? 8 : -8, 3);
        _hardware_labels.push_back(std::move(item));
        auto state = makeLabel(card.get(), "--", 112, 10, kNavy,
                               (i % 2 == 0) ? STYLE_ALIGN_TYPE_TOP_LEFT : STYLE_ALIGN_TYPE_TOP_RIGHT,
                               (i % 2 == 0) ? 8 : -8, 14);
        _hardware_labels.push_back(std::move(state));
        auto action = std::make_unique<AppButton>(card.get(), [this, i]() { showComponentDetails(i); });
        action->setText("");
        action->setWidth(128);
        action->setHeight(32);
        action->setBackgroundGradient(StyleColor::COLOR_WITH_OPACITY(kSurface, 0),
                                       StyleColor::COLOR_WITH_OPACITY(kSurface, 0));
        action->align({STYLE_ALIGN_TYPE_CENTER, 0, 0});
        _hardware_actions.push_back(std::move(action));
        _hardware_cards.push_back(std::move(card));
    }
    _hardware_summary = makeLabel(_hardware.get(), "Snapshot -- | SD --", kRowWidth, 9, kMuted,
                                  STYLE_ALIGN_TYPE_TOP_MID, 0, 184);

    _audio = std::make_unique<LvContainer>(_surface.get());
    _audio->setStyleAttribute(StyleSize::RECT(kWidth, 270));
    _audio->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 38});
    _audio_status = makeLabel(_audio.get(), "MICROPHONES --", kRowWidth, 11, kMuted,
                              STYLE_ALIGN_TYPE_TOP_MID, 0, 8);
    makeLabel(_audio.get(), "CHANNEL 0", 100, 9, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 44);
    _audio_mic0 = makeDiagnosticBar(_audio.get(), 62, kTeal, 0x08745F);
    makeLabel(_audio.get(), "CHANNEL 1", 100, 9, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 92);
    _audio_mic1 = makeDiagnosticBar(_audio.get(), 110, kBlue, 0x2A5E9B);
    _audio_channels = makeLabel(_audio.get(), "PCM -- | -- Hz", kRowWidth, 10, kNavy,
                                STYLE_ALIGN_TYPE_TOP_MID, 0, 146);
    makeLabel(_audio.get(), "Canonical hardware snapshot", kRowWidth, 8, kMuted,
              STYLE_ALIGN_TYPE_TOP_MID, 0, 174);

    _imu = std::make_unique<LvContainer>(_surface.get());
    _imu->setStyleAttribute(StyleSize::RECT(kWidth, 270));
    _imu->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 38});
    _imu_status = makeLabel(_imu.get(), "QMI8658 --", kRowWidth, 11, kMuted,
                            STYLE_ALIGN_TYPE_TOP_MID, 0, 8);
    _imu_accel = makeLabel(_imu.get(), "ACC X -- Y -- Z -- m/s2", kRowWidth, 10, kNavy,
                           STYLE_ALIGN_TYPE_TOP_MID, 0, 52);
    _imu_gyro = makeLabel(_imu.get(), "GYRO X -- Y -- Z -- dps", kRowWidth, 10, kNavy,
                          STYLE_ALIGN_TYPE_TOP_MID, 0, 86);
    _imu_temperature = makeLabel(_imu.get(), "TEMP -- C", kRowWidth, 10, kBlue,
                                 STYLE_ALIGN_TYPE_TOP_MID, 0, 120);
    _imu_calibration = makeLabel(_imu.get(), "CALIBRATION --", kRowWidth, 9, kMuted,
                                 STYLE_ALIGN_TYPE_TOP_MID, 0, 158);

    _format_sd = std::make_unique<AppButton>(_hardware.get(), [this]() { showFormatDialog(); });
    _format_sd->setText("FORMAT SD");
    buttonStyle(*_format_sd, 108, 30, kBlue);
    _format_sd->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, 0, -2});

    _about = std::make_unique<LvContainer>(_surface.get());
    _about->setStyleAttribute(StyleSize::RECT(kWidth, 260));
    _about->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    _about_logo = std::make_unique<AppImage>(_about.get());
    _about_logo->setRawRgb565(
        qsee_boot_rgb565_start,
        466,
        466,
        static_cast<size_t>(qsee_boot_rgb565_end - qsee_boot_rgb565_start)
    );
    _about_logo->setWidth(118);
    _about_logo->setHeight(118);
    _about_logo->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 0});
    _about_tagline = makeLabel(_about.get(), "QSee.ai device console", kWidth, 12, kNavy,
                               STYLE_ALIGN_TYPE_TOP_MID, 0, 126);
    _about_website = makeLabel(_about.get(), "qsee.ai", kWidth, 10, kBlue,
                               STYLE_ALIGN_TYPE_TOP_MID, 0, 148);
    _about_email = makeLabel(_about.get(), "hello@qsee.ai", kWidth, 10, kBlue,
                             STYLE_ALIGN_TYPE_TOP_MID, 0, 170);
    _about_copyright = makeLabel(_about.get(), "Copyright (c) QSee.ai — all rights reserved", kWidth, 9, kMuted,
                                 STYLE_ALIGN_TYPE_TOP_MID, 0, 196);

    _back = std::make_unique<AppButton>(_surface.get(), [this]() { (void)back(); });
    _back->setText("BACK");
    buttonStyle(*_back, 90, 32, kSlate);
    _back->align({STYLE_ALIGN_TYPE_BOTTOM_MID, 0, -8});

    _dialog = std::make_unique<LvContainer>(_surface.get());
    _dialog->setStyleAttribute(StyleSize::RECT(252, 142));
    _dialog->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_CENTER, 0, 0});
    surface(*_dialog);
    _dialog_title = makeLabel(_dialog.get(), "FORMAT SD?", 220, 16, kNavy,
                              STYLE_ALIGN_TYPE_TOP_MID, 0, 14);
    _dialog_text = makeLabel(_dialog.get(), "This erases the card. Are you sure?", 220, 10, kMuted,
                             STYLE_ALIGN_TYPE_TOP_MID, 0, 46);
    _dialog_progress = std::make_unique<AppProgressBar>(_dialog.get());
    _dialog_progress->setRange(0, 100);
    _dialog_progress->setValue(0);
    _dialog_progress->setWidth(190);
    _dialog_progress->setHeight(8);
    _dialog_progress->setBackgroundColor(StyleColor::COLOR(kTrack));
    _dialog_progress->setIndicatorGradient(StyleColor::COLOR(kTeal), StyleColor::COLOR(0x08745F));
    _dialog_progress->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 76});
    setVisible(*_dialog_progress, false);
    _dialog_confirm = std::make_unique<AppButton>(_dialog.get(), [this]() { requestFormat(); });
    _dialog_confirm->setText("YES");
    buttonStyle(*_dialog_confirm, 78, 30, kTeal);
    _dialog_confirm->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 28, -14});
    _dialog_cancel = std::make_unique<AppButton>(_dialog.get(), [this]() { closeDialog(); });
    _dialog_cancel->setText("NO");
    buttonStyle(*_dialog_cancel, 78, 30, kSlate);
    _dialog_cancel->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, -28, -14});

    _refresh_timer = std::make_unique<AppTimer>(1000, [this]() { refresh(); });
    show(Screen::Welcome);
    return true;
}

void Navigation::setVisible(LvObject &object, bool visible)
{
    object.setStyleAttribute(StyleFlag::STYLE_FLAG_HIDDEN, !visible);
}

void Navigation::show(Screen screen)
{
    _current = screen;
    const bool diagnostic = screen == Screen::Audio || screen == Screen::Imu;
    (void)qsee_hardware_set_diagnostic_monitor(diagnostic);
    setVisible(*_welcome, screen == Screen::Welcome);
    setVisible(*_logs, screen == Screen::Logs);
    setVisible(*_hardware, screen == Screen::Hardware);
    setVisible(*_audio, screen == Screen::Audio);
    setVisible(*_imu, screen == Screen::Imu);
    setVisible(*_open_logs, screen == Screen::Welcome);
    setVisible(*_open_hardware, screen == Screen::Welcome);
    setVisible(*_open_about, screen == Screen::Welcome);
    setVisible(*_back, screen != Screen::Welcome);
    setVisible(*_format_sd, false);
    setVisible(*_dialog, false);
    setVisible(*_about, screen == Screen::About);
    _page_title->setText(screen == Screen::Welcome ? "WELCOME" : screen == Screen::Logs ? "LIVE LOGS" :
                         screen == Screen::Hardware ? "HARDWARE" : screen == Screen::Audio ? "AUDIO SCOPE" :
                         screen == Screen::Imu ? "IMU LEVEL" : "ABOUT");
    _page_subtitle->setText(screen == Screen::Welcome ? "QSee device console" :
                            screen == Screen::Logs ? "RAM ring, newest lines" :
                            screen == Screen::Hardware ? "Component inventory" :
                            screen == Screen::Audio ? "ES7210 microphone diagnostics" :
                            screen == Screen::Imu ? "QMI8658 orientation and calibration" : "About this device");
    refresh();
}

bool Navigation::back()
{
    if (_current == Screen::Welcome) return false;
    show(Screen::Welcome);
    return true;
}

void Navigation::shutdown()
{
    // Stop callbacks before Brookesia releases the app screen and its widgets.
    _refresh_timer.reset();
    (void)qsee_hardware_set_diagnostic_monitor(false);
    _ram.bar.reset();
    _ram.value.reset();
    _ram.label.reset();
    _ram.card.reset();
    _sd.bar.reset();
    _sd.value.reset();
    _sd.label.reset();
    _sd.card.reset();
    _cpu0.bar.reset();
    _cpu0.value.reset();
    _cpu0.label.reset();
    _cpu0.card.reset();
    _cpu1.bar.reset();
    _cpu1.value.reset();
    _cpu1.label.reset();
    _cpu1.card.reset();
    _log_box.reset();
    _audio_mic0.reset();
    _audio_mic1.reset();
    _audio_status.reset();
    _audio_channels.reset();
    _imu_status.reset();
    _imu_accel.reset();
    _imu_gyro.reset();
    _imu_temperature.reset();
    _imu_calibration.reset();
    _hardware_labels.clear();
    _hardware_summary.reset();
    _format_sd.reset();
    _sd_action.reset();
    _hardware_actions.clear();
    _logs_pause.reset();
    _logs_clear.reset();
    _logs_follow.reset();
    _log_search.reset();
    _log_filter.reset();
    _logs_live.reset();
    _hardware_cards.clear();
    _dialog_title.reset();
    _dialog_text.reset();
    _dialog_progress.reset();
    _dialog_confirm.reset();
    _dialog_cancel.reset();
    _dialog.reset();
    _page_title.reset();
    _page_subtitle.reset();
    _open_logs.reset();
    _open_hardware.reset();
    _open_audio.reset();
    _open_imu.reset();
    _open_about.reset();
    _back.reset();
    _radio_status.reset();
    _welcome.reset();
    _logs.reset();
    _hardware.reset();
    _audio.reset();
    _imu.reset();
    _about_logo.reset();
    _about_tagline.reset();
    _about_website.reset();
    _about_email.reset();
    _about_copyright.reset();
    _about.reset();
    _surface.reset();
    _screen.reset();
}

void Navigation::updateMetric(Metric &metric, const char *value, int progress)
{
    metric.value->setText(value);
    metric.bar->setValue(progress < 0 ? 0 : progress > 100 ? 100 : progress, true);
}

void Navigation::refresh()
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    char value[64];
    const int ram_progress = snapshot.memory.heap_free_bytes > 0
        ? static_cast<int>((snapshot.memory.heap_free_bytes / 10000U) > 100U ? 100U : snapshot.memory.heap_free_bytes / 10000U)
        : 0;
    std::snprintf(value, sizeof(value), "%lu KB  %d%%", static_cast<unsigned long>(snapshot.memory.heap_free_bytes / 1024U), ram_progress);
    updateMetric(_ram, value, ram_progress);

    if (snapshot.storage.health == DeviceHealth::Ready && snapshot.storage.capacity_bytes > 0) {
        const uint64_t used_bytes = snapshot.storage.capacity_bytes > snapshot.storage.free_bytes
            ? snapshot.storage.capacity_bytes - snapshot.storage.free_bytes : 0;
        const int used_percent = static_cast<int>((used_bytes * 100ULL) /
                                                   snapshot.storage.capacity_bytes);
        std::snprintf(value, sizeof(value), "%llu / %llu MB  %s",
                      static_cast<unsigned long long>(snapshot.storage.free_bytes / (1024ULL * 1024ULL)),
                      static_cast<unsigned long long>(snapshot.storage.capacity_bytes / (1024ULL * 1024ULL)),
                      snapshot.storage.logger_mirror_enabled ? "MIRROR" : "NO MIRROR");
        updateMetric(_sd, value, 100 - used_percent);
    } else if (storageFormatRequired(snapshot.storage)) {
        updateMetric(_sd, "format required", 0);
    } else if (!snapshot.storage.card_present) {
        updateMetric(_sd, "card absent", 0);
    } else {
        updateMetric(_sd, "not mounted", 0);
    }
    const uint32_t sd_color = storageFormatRequired(snapshot.storage) ? 0xD97706 :
                              !snapshot.storage.card_present ? kMuted :
                              healthColor(snapshot.storage.health, snapshot.storage.last_error);
    _sd.value->setTextColor(StyleColor::COLOR(sd_color));
    _sd.card->setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BORDER, StyleColor::COLOR(sd_color));
    updateMetric(_cpu0, "CPU data unavailable", -1);
    updateMetric(_cpu1, "CPU data unavailable", -1);
    _cpu0.value->setTextColor(StyleColor::COLOR(kMuted));
    _cpu1.value->setTextColor(StyleColor::COLOR(kMuted));

    std::snprintf(value, sizeof(value), "WiFi %s   BLE %s   GPS %s",
                  health(snapshot.radios.wifi.health), health(snapshot.radios.ble.health), health(snapshot.gps.health));
    _radio_status->setText(value);
    if (_current == Screen::Logs && !_logs_paused) {
        updateLogView();
    } else if (_current == Screen::Hardware) {
        updateHardwareText();
        const bool needs_format = storageFormatRequired(snapshot.storage);
        setVisible(*_format_sd, needs_format);
    }
}

void Navigation::updateLogView()
{
    if (_log_box == nullptr) return;
    char logs[8192];
    char filtered[8192];
    qsee_logger_snapshot(logs, sizeof(logs), 64);
    const char *query = _log_search != nullptr ? _log_search->getText() : "";
    filterLogs(logs, query, _log_filter_mode, filtered, sizeof(filtered));
    _log_box->setText(filtered, _logs_follow_latest && _log_box->isAtBottom());
}

void Navigation::showComponentDetails(size_t index)
{
    if (_current != Screen::Hardware || index >= _hardware_cards.size()) return;
    static constexpr const char *names[] = {
        "BATTERY", "RTC", "IMU", "GPS", "WIFI", "BLE",
        "DISPLAY", "TOUCH", "SPEAKER", "MICROPHONE", "SD CARD"
    };
    if (index >= sizeof(names) / sizeof(names[0])) return;
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    const DeviceHealth states[] = {
        snapshot.battery.health, snapshot.rtc.health, snapshot.imu.health,
        snapshot.gps.health, snapshot.radios.wifi.health, snapshot.radios.ble.health,
        snapshot.ui.display.health, snapshot.ui.touch.health, snapshot.audio.speaker.health,
        snapshot.audio.microphone.health, snapshot.storage.health
    };
    const esp_err_t errors[] = {
        snapshot.battery.last_error, snapshot.rtc.last_error, snapshot.imu.last_error,
        snapshot.gps.last_error, snapshot.radios.wifi.last_error, snapshot.radios.ble.last_error,
        snapshot.ui.display.last_error, snapshot.ui.touch.last_error,
        snapshot.audio.speaker.last_error, snapshot.audio.microphone.last_error,
        snapshot.storage.last_error
    };
    _selected_component = index;
    char title[40];
    char details[180];
    std::snprintf(title, sizeof(title), "%s STATUS", names[index]);
    std::snprintf(details, sizeof(details), "State: %s\nError: %s\nTap CLOSE to return.",
                  index == 10U && storageFormatRequired(snapshot.storage)
                      ? "FORMAT REQUIRED" : health(states[index]),
                  esp_err_to_name(errors[index]));
    setVisible(*_dialog, true);
    _dialog_title->setText(title);
    _dialog_text->setText(details);
    setVisible(*_dialog_progress, false);
    setVisible(*_dialog_confirm, false);
    setVisible(*_dialog_cancel, true);
    _dialog_cancel->setText("CLOSE");
    _dialog_text->setTextColor(StyleColor::COLOR(healthColor(states[index], errors[index])));
}

void Navigation::updateHardwareText()
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    const DeviceHealth states[] = {
        snapshot.battery.health, snapshot.rtc.health, snapshot.imu.health,
        snapshot.gps.health, snapshot.radios.wifi.health, snapshot.radios.ble.health,
        snapshot.ui.display.health, snapshot.ui.touch.health, snapshot.audio.speaker.health,
        snapshot.audio.microphone.health, snapshot.storage.health
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        auto &state = _hardware_labels[i * 2 + 1];
        const bool format_required = i == 10U && storageFormatRequired(snapshot.storage);
        state->setText(format_required ? "FORMAT" : health(states[i]));
        const esp_err_t errors[] = {
            snapshot.battery.last_error, snapshot.rtc.last_error, snapshot.imu.last_error,
            snapshot.gps.last_error, snapshot.radios.wifi.last_error, snapshot.radios.ble.last_error,
            snapshot.ui.display.last_error, snapshot.ui.touch.last_error,
            snapshot.audio.speaker.last_error, snapshot.audio.microphone.last_error,
            snapshot.storage.last_error
        };
        state->setTextColor(StyleColor::COLOR(format_required ? 0xD97706 : healthColor(states[i], errors[i])));
        _hardware_cards[i]->setStyleAttribute(
            StyleColorItem::STYLE_COLOR_ITEM_BORDER,
            StyleColor::COLOR(format_required ? 0xD97706 : healthColor(states[i], errors[i]))
        );
    }
    char summary[96];
    std::snprintf(summary, sizeof(summary), "Snapshot #%lu | SD %llu / %llu MB | %s",
                  static_cast<unsigned long>(snapshot.sequence),
                  static_cast<unsigned long long>(snapshot.storage.free_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(snapshot.storage.capacity_bytes / (1024ULL * 1024ULL)),
                  snapshot.storage.logger_mirror_enabled ? "MIRROR ON" : "MIRROR OFF");
    _hardware_summary->setText(summary);
}

void Navigation::showFormatDialog()
{
    if (_current != Screen::Hardware) return;
    setVisible(*_dialog, true);
    _dialog_title->setText("FORMAT SD?");
    _dialog_text->setText("This erases the card. Are you sure?");
    setVisible(*_dialog_confirm, true);
    setVisible(*_dialog_cancel, true);
    setVisible(*_dialog_progress, false);
    _dialog_cancel->setText("NO");
    _dialog_text->setTextColor(StyleColor::COLOR(kMuted));
}

void Navigation::closeDialog()
{
    setVisible(*_dialog, false);
}

void Navigation::requestFormat()
{
    setVisible(*_dialog_confirm, false);
    setVisible(*_dialog_cancel, false);
    setVisible(*_dialog_progress, true);
    _dialog_progress->setValue(10);
    _dialog_title->setText("FORMATTING SD");
    _dialog_text->setText("Storage service is preparing the operation...");
    const esp_err_t result = qsee_storage_request_format();
    if (result != ESP_OK) {
        _dialog_progress->setValue(0);
        _dialog_title->setText("FORMAT UNAVAILABLE");
        _dialog_text->setText("The hardware storage service does not expose formatting yet.");
    }
}

}  // namespace qsee
