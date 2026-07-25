#include "qsee_navigation.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {
using namespace esp_brookesia::gui;

constexpr uint32_t kSurface = 0x111922;
constexpr uint32_t kNavy = 0xF3F7FA;
constexpr uint32_t kMuted = 0x9FB1C1;
constexpr uint32_t kTeal = 0x0F9D83;
constexpr uint32_t kBlue = 0x3478C8;
constexpr uint32_t kSlate = 0x52697D;
constexpr uint32_t kWarning = 0xD97706;
constexpr uint32_t kError = 0xD64545;
constexpr uint32_t kPart = 0x74828C;
constexpr uint32_t kDialogBorder = 0x3D7780;
constexpr uint32_t kTrack = 0x263744;
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr int kWidth = 272;
constexpr int kRowWidth = 236;
constexpr int kHardwareRowWidth = 248;
constexpr size_t kLogBufferBytes = 4096;
constexpr lv_style_selector_t kPressedSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);

void applySpotlightBackground(lv_obj_t *screen)
{
    static lv_grad_dsc_t gradient;
    static bool initialized = false;
    if (!initialized) {
        const lv_color_t colors[] = {
            lv_color_hex(0x0B2733),
            lv_color_hex(0x010205),
        };
        lv_grad_init_stops(&gradient, colors, nullptr, nullptr, 2);
        lv_grad_radial_init(
            &gradient,
            lv_pct(50),
            lv_pct(34),
            lv_pct(100),
            lv_pct(100),
            LV_GRAD_EXTEND_PAD
        );
        initialized = true;
    }

    lv_obj_set_style_bg_grad(screen, &gradient, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
}

void surface(LvObject &object)
{
    object.setStyleAttribute(
        StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND,
        StyleColor::COLOR_WITH_OPACITY(kSurface, LV_OPA_90)
    );
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
    const uint32_t end_color = color == kTeal ? 0x075E54 :
                               color == kBlue ? 0x183E70 : 0x263746;
    button.setWidth(width);
    button.setHeight(height);
    button.setFont(StyleFont::SIZE(12));
    button.setTextColor(StyleColor::COLOR(kWhite));
    button.setBackgroundGradient(StyleColor::COLOR(color), StyleColor::COLOR(end_color));
    lv_obj_set_style_bg_grad_dir(button.getNativeHandle(), LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_radius(button.getNativeHandle(), height / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(button.getNativeHandle(), 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button.getNativeHandle(), lv_color_hex(0x4D7188), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button.getNativeHandle(), LV_OPA_40, LV_PART_MAIN);
}

void flatButtonBackground(AppButton &button, uint32_t color)
{
    lv_obj_t *native = button.getNativeHandle();
    lv_obj_set_style_bg_color(native, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(native, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(native, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(native, 0, LV_PART_MAIN);
}

void styleArcButton(
    AppButton &button,
    int size,
    int x,
    int y,
    uint32_t color,
    const char *symbol,
    const char *caption
)
{
    button.setText("");
    buttonStyle(button, size, size, color);
    button.align({STYLE_ALIGN_TYPE_TOP_LEFT, x, y});

    lv_obj_t *native = button.getNativeHandle();
    lv_obj_set_style_radius(native, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(native, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        native,
        lv_color_hex(color == kTeal ? 0x2BE7D0 : 0x69B9FF),
        LV_PART_MAIN
    );
    lv_obj_set_style_border_opa(native, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(native, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(native, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_x(native, 0, kPressedSelector);
    lv_obj_set_style_translate_y(native, 0, kPressedSelector);
    lv_obj_set_style_transform_width(native, 0, kPressedSelector);
    lv_obj_set_style_transform_height(native, 0, kPressedSelector);
    lv_obj_clear_flag(native, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(native);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, lv_color_hex(kWhite), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        icon,
        size >= 90 ? &lv_font_montserrat_28 : &lv_font_montserrat_18,
        LV_PART_MAIN
    );
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, size >= 90 ? -13 : -10);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(native);
    lv_label_set_text(label, caption);
    lv_obj_set_width(label, size - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(kWhite), LV_PART_MAIN);
    lv_obj_set_style_text_font(
        label,
        size >= 90 ? &lv_font_montserrat_12 : &lv_font_montserrat_10,
        LV_PART_MAIN
    );
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
}

void transparentPanel(LvContainer &panel, bool scrollable)
{
    lv_obj_t *native = panel.getNativeHandle();
    lv_obj_set_style_bg_opa(native, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(native, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(native, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(native, 0, LV_PART_MAIN);
    if (scrollable) {
        lv_obj_add_flag(native, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(native, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(native, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_width(native, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(native, lv_color_hex(kBlue), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(native, LV_OPA_50, LV_PART_SCROLLBAR);
    } else {
        lv_obj_clear_flag(native, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(native, LV_SCROLLBAR_MODE_OFF);
    }
}

std::unique_ptr<AppLabel> makeLabel(
    const LvObject *parent,
    const char *text,
    int width,
    int size,
    uint32_t color,
    StyleAlignType align,
    int x,
    int y
);

void makeDeviceLine(
    qsee::Navigation::DeviceLine &line,
    const LvObject *parent,
    const char *symbol,
    const char *name,
    int y,
    uint32_t color
)
{
    line.row = std::make_unique<LvContainer>(parent);
    line.row->setStyleAttribute(StyleSize::RECT(kRowWidth, 34));
    line.row->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, y});
    lv_obj_set_style_bg_opa(line.row->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(line.row->getNativeHandle(), 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(line.row->getNativeHandle(), LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(line.row->getNativeHandle(), lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_border_opa(line.row->getNativeHandle(), LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(line.row->getNativeHandle(), 0, LV_PART_MAIN);
    lv_obj_clear_flag(line.row->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(line.row->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);

    line.icon = makeLabel(
        line.row.get(), symbol, 24, 18, color, STYLE_ALIGN_TYPE_LEFT_MID, 4, 0
    );
    line.name = makeLabel(
        line.row.get(), name, 56, 11, kMuted, STYLE_ALIGN_TYPE_LEFT_MID, 32, 0
    );
    line.value = makeLabel(
        line.row.get(), "--", 140, 10, kNavy, STYLE_ALIGN_TYPE_RIGHT_MID, -4, 0
    );
    lv_label_set_long_mode(line.value->getNativeHandle(), LV_LABEL_LONG_MODE_DOTS);
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
    metric.card->setStyleAttribute(StyleSize::RECT(kRowWidth, 38));
    metric.card->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, y});
    metric.card->setStyleAttribute(
        StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND,
        StyleColor::COLOR_WITH_OPACITY(kSurface, LV_OPA_20)
    );
    lv_obj_clear_flag(metric.card->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(metric.card->getNativeHandle(), 8, LV_PART_MAIN);
    metric.label = makeLabel(metric.card.get(), name, 104, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 3);
    metric.value = makeLabel(metric.card.get(), "--", 118, 14, kNavy, STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 1);
    lv_label_set_long_mode(metric.value->getNativeHandle(), LV_LABEL_LONG_MODE_DOTS);
    metric.bar = std::make_unique<AppProgressBar>(metric.card.get());
    metric.bar->setRange(0, 100);
    metric.bar->setValue(0);
    // Keep the bar inside the 236 px card after the 10 px side insets.
    metric.bar->setWidth(kRowWidth - 16);
    metric.bar->setHeight(5);
    metric.bar->setBackgroundColor(StyleColor::COLOR(kTrack));
    lv_obj_set_style_bg_color(metric.bar->getNativeHandle(), lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(metric.bar->getNativeHandle(), LV_OPA_COVER, LV_PART_INDICATOR);
    metric.bar->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 30});
}

std::unique_ptr<AppProgressBar> makeDiagnosticBar(const LvObject *parent, int y, uint32_t top, uint32_t bottom)
{
    auto bar = std::make_unique<AppProgressBar>(parent);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setWidth(kRowWidth);
    bar->setHeight(12);
    bar->setBackgroundColor(StyleColor::COLOR(kTrack));
    (void)bottom;
    lv_obj_set_style_bg_color(bar->getNativeHandle(), lv_color_hex(top), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar->getNativeHandle(), LV_OPA_COVER, LV_PART_INDICATOR);
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

const char *hardwareStateText(
    size_t index,
    const qsee::DeviceSnapshot &snapshot,
    const qsee::DeviceHealth *states
)
{
    if (index == 10U && !snapshot.storage.card_present) return "ABSENT";
    if (index == 10U && storageFormatRequired(snapshot.storage)) return "FORMAT";
    return health(states[index]);
}

uint32_t hardwareStatusColor(
    size_t index,
    const qsee::DeviceSnapshot &snapshot,
    qsee::DeviceHealth state,
    esp_err_t error
)
{
    if (index == 10U &&
        (!snapshot.storage.card_present || storageFormatRequired(snapshot.storage))) {
        return kWarning;
    }
    if (state == qsee::DeviceHealth::Ready) return kTeal;
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE &&
        error != ESP_ERR_NOT_FOUND && error != ESP_ERR_TIMEOUT &&
        error != ESP_ERR_NOT_SUPPORTED) {
        return kError;
    }
    return kWarning;
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
    _log_snapshot_buffer = static_cast<char *>(
        heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    _log_filtered_buffer = static_cast<char *>(
        heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (_log_snapshot_buffer == nullptr) {
        _log_snapshot_buffer = static_cast<char *>(heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_8BIT));
    }
    if (_log_filtered_buffer == nullptr) {
        _log_filtered_buffer = static_cast<char *>(heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_8BIT));
    }
    if (_log_snapshot_buffer != nullptr) _log_snapshot_buffer[0] = '\0';
    if (_log_filtered_buffer != nullptr) _log_filtered_buffer[0] = '\0';
    ESP_LOGI("qsee_navigation", "Log buffers: snapshot=%s filtered=%s",
             _log_snapshot_buffer != nullptr ? "ready" : "unavailable",
             _log_filtered_buffer != nullptr ? "ready" : "unavailable");

    applySpotlightBackground(_screen->getNativeHandle());
    _surface = std::make_unique<LvContainer>(_screen.get());
    _surface->setStyleAttribute(StyleSize::RECT(320, 320));
    _surface->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_CENTER, 0, 0});
    _surface->setStyleAttribute(
        StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND,
        StyleColor::COLOR_WITH_OPACITY(kSurface, LV_OPA_TRANSP)
    );
    lv_obj_clear_flag(_surface->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);

    _title_bar = std::make_unique<LvContainer>(_surface.get());
    _title_bar->setStyleAttribute(StyleSize::RECT(kWidth, 40));
    _title_bar->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 0});
    lv_obj_set_style_bg_opa(_title_bar->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_title_bar->getNativeHandle(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(_title_bar->getNativeHandle(), 0, LV_PART_MAIN);
    lv_obj_clear_flag(_title_bar->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);

    _page_title = makeLabel(_title_bar.get(), "WELCOME", 256, 18, kNavy,
                            STYLE_ALIGN_TYPE_TOP_LEFT, 8, -1);
    _page_subtitle = makeLabel(_title_bar.get(), "QSee device console", 256, 12, kMuted,
                               STYLE_ALIGN_TYPE_TOP_LEFT, 8, 21);

    _welcome = std::make_unique<LvContainer>(_surface.get());
    _welcome->setStyleAttribute(StyleSize::RECT(kWidth, 216));
    _welcome->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_welcome, true);
    lv_obj_set_scrollbar_mode(_welcome->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);
    makeMetric(_ram, _welcome.get(), "INTERNAL RAM", 0, kTeal);
    makeMetric(_sd, _welcome.get(), "SD STORAGE", 40, kBlue);
    makeMetric(_cpu0, _welcome.get(), "CPU CORE 0", 80, kTeal);
    makeMetric(_cpu1, _welcome.get(), "CPU CORE 1", 120, kBlue);
    makeMetric(_psram, _welcome.get(), "PSRAM", 164, kTeal);
    makeMetric(_flash, _welcome.get(), "FLASH APP", 204, kBlue);

    _welcome_devices.resize(4);
    makeDeviceLine(
        _welcome_devices[0], _welcome.get(), LV_SYMBOL_REFRESH, "FUSION", 246, kTeal
    );
    makeDeviceLine(
        _welcome_devices[1], _welcome.get(), LV_SYMBOL_WIFI, "WIFI", 280, kBlue
    );
    makeDeviceLine(
        _welcome_devices[2], _welcome.get(), LV_SYMBOL_BLUETOOTH, "BLE", 314, kBlue
    );
    makeDeviceLine(
        _welcome_devices[3], _welcome.get(), LV_SYMBOL_GPS, "GPS", 348, kTeal
    );

    _sd_action = std::make_unique<AppButton>(_welcome.get(), [this]() { show(Screen::Hardware); });
    _sd_action->setText("");
    _sd_action->setWidth(kRowWidth);
    _sd_action->setHeight(38);
    lv_obj_set_style_bg_opa(_sd_action->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
    _sd_action->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 40});
    lv_obj_set_style_translate_x(_sd_action->getNativeHandle(), 0, kPressedSelector);
    lv_obj_set_style_translate_y(_sd_action->getNativeHandle(), 0, kPressedSelector);

    // The chosen mockup uses a fixed six-action arc in the circular bezel area.
    _open_logs = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Logs); });
    styleArcButton(*_open_logs, 66, 8, 340, kBlue, LV_SYMBOL_LIST, "LIVE LOGS");
    _open_audio = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Audio); });
    styleArcButton(*_open_audio, 78, 66, 350, kBlue, LV_SYMBOL_AUDIO, "AUDIO SCOPE");
    _open_imu = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Imu); });
    styleArcButton(*_open_imu, 90, 136, 356, kTeal, LV_SYMBOL_GPS, "IMU LEVEL");
    _open_hardware = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Hardware); });
    styleArcButton(*_open_hardware, 90, 240, 356, kTeal, LV_SYMBOL_DRIVE, "HARDWARE");
    _open_about = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::About); });
    styleArcButton(*_open_about, 78, 322, 350, kBlue, "i", "ABOUT");
    _open_settings = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Settings); });
    styleArcButton(*_open_settings, 66, 392, 340, kBlue, LV_SYMBOL_SETTINGS, "SETTINGS");

    _logs = std::make_unique<LvContainer>(_surface.get());
    _logs->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _logs->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_logs, false);
    _log_search = std::make_unique<AppTextBox>(_logs.get());
    _log_search->setWidth(164);
    _log_search->setHeight(32);
    _log_search->setOneLine();
    _log_search->setPlaceholder("Search logs");
    _log_search->setFont(StyleFont::SIZE(12));
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
    buttonStyle(*_log_filter, 96, 30, kBlue);
    _log_filter->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -4, 1});
    _log_box = std::make_unique<AppRichText>(_logs.get());
    _log_box->setWidth(kWidth);
    _log_box->setHeight(154);
    _log_box->setFont(StyleFont::SIZE(12));
    _log_box->setTextColor(StyleColor::COLOR(0xEAF1F6));
    _log_box->setBackgroundColor(StyleColor::COLOR(0x12263A));
    _log_box->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 36});

    _logs_pause = std::make_unique<AppButton>(_logs.get(), [this]() {
        _logs_paused = !_logs_paused;
        _logs_pause->setText(_logs_paused ? "RESUME" : "PAUSE");
    });
    _logs_pause->setText("PAUSE");
    buttonStyle(*_logs_pause, 82, 30, kSlate);
    _logs_pause->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 8, -2});
    _logs_clear = std::make_unique<AppButton>(_logs.get(), [this]() {
        qsee_logger_clear();
        _log_box->setText("", false);
    });
    _logs_clear->setText("CLEAR");
    buttonStyle(*_logs_clear, 70, 30, kBlue);
    _logs_clear->align({STYLE_ALIGN_TYPE_BOTTOM_MID, -4, -2});
    _logs_follow = std::make_unique<AppButton>(_logs.get(), [this]() {
        _logs_follow_latest = !_logs_follow_latest;
        _logs_follow->setText(_logs_follow_latest ? "FOLLOW ON" : "FOLLOW OFF");
    });
    _logs_follow->setText("FOLLOW ON");
    buttonStyle(*_logs_follow, 92, 30, kBlue);
    _logs_follow->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, -8, -2});
    _logs_live = makeLabel(_logs.get(), "LIVE", 54, 12, kTeal,
                           STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 40);

    _hardware = std::make_unique<LvContainer>(_surface.get());
    _hardware->setStyleAttribute(StyleSize::RECT(kWidth, 226));
    _hardware->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_hardware, true);
    lv_obj_set_scrollbar_mode(_hardware->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);
    const char *hardware_names[] = {
        "BATTERY", "RTC", "IMU", "GPS", "WIFI", "BLE",
        "DISPLAY", "TOUCH", "SPEAKER", "MIC", "SD CARD"
    };
    const char *hardware_parts[] = {
        "AXP2101", "PCF85063", "QMI8658", "LC76G", "ESP32-S3", "ESP32-S3",
        "CO5300", "CST9217", "ES8311", "ES7210", "SDMMC"
    };
    for (size_t i = 0; i < sizeof(hardware_names) / sizeof(hardware_names[0]); ++i) {
        auto row = std::make_unique<LvContainer>(_hardware.get());
        row->setStyleAttribute(StyleSize::RECT(kHardwareRowWidth, 30));
        row->setStyleAttribute(StyleAlign{
            STYLE_ALIGN_TYPE_TOP_MID, 0, static_cast<int>(i) * 32
        });
        lv_obj_set_style_bg_opa(row->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row->getNativeHandle(), 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(row->getNativeHandle(), LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->getNativeHandle(), lv_color_hex(kTrack), LV_PART_MAIN);
        lv_obj_set_style_border_opa(row->getNativeHandle(), LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row->getNativeHandle(), 0, LV_PART_MAIN);
        lv_obj_clear_flag(row->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(row->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);
        auto dot = std::make_unique<LvContainer>(row.get());
        dot->setStyleAttribute(StyleSize::RECT(10, 10));
        dot->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_LEFT_MID, 6, 0});
        dot->setStyleAttribute(
            StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND,
            StyleColor::COLOR(kWarning)
        );
        lv_obj_set_style_bg_opa(dot->getNativeHandle(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(dot->getNativeHandle(), LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(dot->getNativeHandle(), LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        _hardware_status_dots.push_back(std::move(dot));

        auto item = makeLabel(row.get(), hardware_names[i], 82, 13, kMuted,
                              STYLE_ALIGN_TYPE_LEFT_MID, 20, 0);
        lv_obj_clear_flag(item->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        _hardware_labels.push_back(std::move(item));
        auto part = makeLabel(row.get(), hardware_parts[i], 68, 10, kPart,
                              STYLE_ALIGN_TYPE_LEFT_MID, 104, 0);
        lv_obj_clear_flag(part->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        _hardware_parts.push_back(std::move(part));
        auto state = makeLabel(row.get(), "--", 66, 13, kNavy,
                               STYLE_ALIGN_TYPE_RIGHT_MID, -8, 0);
        lv_obj_clear_flag(state->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        _hardware_labels.push_back(std::move(state));
        auto action = std::make_unique<AppButton>(row.get(), [this, i]() { showComponentDetails(i); });
        action->setText("");
        action->setWidth(kHardwareRowWidth);
        action->setHeight(30);
        lv_obj_set_style_bg_opa(action->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(action->getNativeHandle(), 0, LV_PART_MAIN);
        lv_obj_set_style_translate_x(action->getNativeHandle(), 0, kPressedSelector);
        lv_obj_set_style_translate_y(action->getNativeHandle(), 0, kPressedSelector);
        lv_obj_set_style_transform_width(action->getNativeHandle(), 0, kPressedSelector);
        lv_obj_set_style_transform_height(action->getNativeHandle(), 0, kPressedSelector);
        lv_obj_clear_flag(action->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
        action->align({STYLE_ALIGN_TYPE_CENTER, 0, 0});
        _hardware_actions.push_back(std::move(action));
        _hardware_rows.push_back(std::move(row));
    }
    _hardware_summary = makeLabel(_hardware.get(), "Snapshot -- | SD --", kRowWidth, 12, kMuted,
                                   STYLE_ALIGN_TYPE_TOP_MID, 0, 356);
    lv_obj_clear_flag(_hardware_summary->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(_hardware_summary->getNativeHandle(), LV_LABEL_LONG_MODE_DOTS);

    _audio = std::make_unique<LvContainer>(_surface.get());
    _audio->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _audio->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_audio, true);
    _audio_status = makeLabel(_audio.get(), "MICROPHONES --", kRowWidth, 14, kMuted,
                              STYLE_ALIGN_TYPE_TOP_MID, 0, 6);
    _content_labels.push_back(
        makeLabel(_audio.get(), "CHANNEL 0", 110, 14, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 40)
    );
    _audio_mic0 = makeDiagnosticBar(_audio.get(), 62, kTeal, 0x08745F);
    _content_labels.push_back(
        makeLabel(_audio.get(), "CHANNEL 1", 110, 14, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 96)
    );
    _audio_mic1 = makeDiagnosticBar(_audio.get(), 118, kBlue, 0x2A5E9B);
    _audio_channels = makeLabel(_audio.get(), "PCM -- | -- Hz", kRowWidth, 14, kNavy,
                                STYLE_ALIGN_TYPE_TOP_MID, 0, 166);
    _content_labels.push_back(
        makeLabel(_audio.get(), "Live levels from the canonical device snapshot", kRowWidth, 12,
                  kMuted, STYLE_ALIGN_TYPE_TOP_MID, 0, 204)
    );

    _imu = std::make_unique<LvContainer>(_surface.get());
    _imu->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _imu->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_imu, true);
    _imu_status = makeLabel(_imu.get(), "QMI8658 --", kRowWidth, 14, kMuted,
                            STYLE_ALIGN_TYPE_TOP_MID, 0, 6);
    _imu_accel = makeLabel(_imu.get(), "ACC X -- Y -- Z -- m/s2", kRowWidth, 14, kNavy,
                           STYLE_ALIGN_TYPE_TOP_MID, 0, 52);
    _imu_gyro = makeLabel(_imu.get(), "GYRO X -- Y -- Z -- dps", kRowWidth, 14, kNavy,
                          STYLE_ALIGN_TYPE_TOP_MID, 0, 98);
    _imu_temperature = makeLabel(_imu.get(), "TEMP -- C", kRowWidth, 16, kBlue,
                                 STYLE_ALIGN_TYPE_TOP_MID, 0, 144);
    _imu_calibration = makeLabel(_imu.get(), "CALIBRATION --", kRowWidth, 14, kMuted,
                                 STYLE_ALIGN_TYPE_TOP_MID, 0, 190);

    _format_sd = std::make_unique<AppButton>(_hardware.get(), [this]() { showFormatDialog(); });
    _format_sd->setText("FORMAT SD");
    buttonStyle(*_format_sd, 108, 34, kBlue);
    flatButtonBackground(*_format_sd, kBlue);
    lv_obj_clear_flag(_format_sd->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_grad_dir(_format_sd->getNativeHandle(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_translate_x(_format_sd->getNativeHandle(), 0, kPressedSelector);
    lv_obj_set_style_translate_y(_format_sd->getNativeHandle(), 0, kPressedSelector);
    lv_obj_set_style_transform_width(_format_sd->getNativeHandle(), 0, kPressedSelector);
    lv_obj_set_style_transform_height(_format_sd->getNativeHandle(), 0, kPressedSelector);
    _format_sd->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 386});

    _about = std::make_unique<LvContainer>(_surface.get());
    _about->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _about->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_about, true);
    _about_tagline = makeLabel(_about.get(), "QSEE.AI", kRowWidth, 28, kNavy,
                               STYLE_ALIGN_TYPE_TOP_MID, 0, 8);
    _about_website = makeLabel(_about.get(), "CLEAR SIGNALS.\nCONFIDENT DECISIONS.", kRowWidth, 18, kTeal,
                               STYLE_ALIGN_TYPE_TOP_MID, 0, 58);
    _about_email = makeLabel(
        _about.get(),
        "Round device console\nESP32-S3  |  466 x 466 AMOLED\nBuilt with ESP-Brookesia",
        kRowWidth, 14, kMuted, STYLE_ALIGN_TYPE_TOP_MID, 0, 126
    );
    _about_copyright = makeLabel(
        _about.get(), "qsee.ai  |  hello@qsee.ai\nCopyright 2026 QSee.ai",
        kRowWidth, 12, kBlue, STYLE_ALIGN_TYPE_TOP_MID, 0, 206
    );

    _settings = std::make_unique<LvContainer>(_surface.get());
    _settings->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _settings->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_settings, true);
    _content_labels.push_back(
        makeLabel(_settings.get(), "DIAGNOSTICS", kRowWidth, 18, kTeal,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 8)
    );
    _settings_period = makeLabel(_settings.get(), "Sample interval 300 ms", kRowWidth, 16, kNavy,
                                 STYLE_ALIGN_TYPE_TOP_LEFT, 8, 40);
    _content_labels.push_back(
        makeLabel(_settings.get(), "Active only in Audio Scope and IMU Level",
                  kRowWidth, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 68)
    );
    _settings_faster = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustDiagnosticPeriod(-100); }
    );
    _settings_faster->setText("FASTER");
    buttonStyle(*_settings_faster, 108, 38, kTeal);
    _settings_faster->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 96});
    _settings_slower = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustDiagnosticPeriod(100); }
    );
    _settings_slower->setText("SLOWER");
    buttonStyle(*_settings_slower, 108, 38, kBlue);
    _settings_slower->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 96});
    _settings_default = std::make_unique<AppButton>(
        _settings.get(), [this]() {
            (void)qsee_hardware_set_diagnostic_period_ms(300);
            updateSettings();
        }
    );
    _settings_default->setText("RESET 300 MS");
    buttonStyle(*_settings_default, 148, 38, kSlate);
    _settings_default->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 142});

    _content_labels.push_back(
        makeLabel(_settings.get(), "SENSOR FUSION", kRowWidth, 18, kBlue,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 202)
    );
    _settings_smoothing = makeLabel(
        _settings.get(), "IMU input smoothing 70%", kRowWidth, 16, kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT, 8, 234
    );
    _settings_smoothing_less = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustImuSmoothing(-10); }
    );
    _settings_smoothing_less->setText("LESS FILTER");
    buttonStyle(*_settings_smoothing_less, 108, 38, kSlate);
    _settings_smoothing_less->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 270});
    _settings_smoothing_more = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustImuSmoothing(10); }
    );
    _settings_smoothing_more->setText("MORE FILTER");
    buttonStyle(*_settings_smoothing_more, 108, 38, kBlue);
    _settings_smoothing_more->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 270});
    _content_labels.push_back(
        makeLabel(_settings.get(), "0% raw response  |  90% maximum smoothing",
                  kRowWidth, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 316)
    );

    _content_labels.push_back(
        makeLabel(_settings.get(), "STORAGE", kRowWidth, 18, kTeal,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 366)
    );
    _settings_storage = makeLabel(
        _settings.get(), "SD --  |  log mirror --", kRowWidth, 16, kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT, 8, 398
    );
    _settings_sd_mirror = std::make_unique<AppButton>(
        _settings.get(), [this]() { toggleSdMirror(); }
    );
    _settings_sd_mirror->setText("LOG MIRROR ON");
    buttonStyle(*_settings_sd_mirror, 160, 38, kTeal);
    _settings_sd_mirror->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 436});
    _content_labels.push_back(
        makeLabel(_settings.get(), "Formatting remains an explicit action in Hardware",
                  kRowWidth, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 482)
    );

    _content_labels.push_back(
        makeLabel(_settings.get(), "WI-FI", kRowWidth, 18, kBlue,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 532)
    );
    _settings_wifi = makeLabel(
        _settings.get(), "Radio deferred  |  no connection", kRowWidth, 16, kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT, 8, 564
    );
    _settings_wifi_start = std::make_unique<AppButton>(
        _settings.get(), [this]() { startWifi(); }
    );
    _settings_wifi_start->setText("START WI-FI");
    buttonStyle(*_settings_wifi_start, 148, 38, kBlue);
    _settings_wifi_start->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 602});
    _content_labels.push_back(
        makeLabel(_settings.get(), "Credentials are stored by the Wi-Fi service",
                  kRowWidth, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 648)
    );

    _content_labels.push_back(
        makeLabel(_settings.get(), "DISPLAY", kRowWidth, 18, kTeal,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 698)
    );
    _settings_brightness = makeLabel(
        _settings.get(), "AMOLED brightness 100%", kRowWidth, 16, kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT, 8, 730
    );
    _settings_brightness_down = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustBrightness(-10); }
    );
    _settings_brightness_down->setText("DIMMER");
    buttonStyle(*_settings_brightness_down, 108, 38, kSlate);
    _settings_brightness_down->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 768});
    _settings_brightness_up = std::make_unique<AppButton>(
        _settings.get(), [this]() { adjustBrightness(10); }
    );
    _settings_brightness_up->setText("BRIGHTER");
    buttonStyle(*_settings_brightness_up, 108, 38, kTeal);
    _settings_brightness_up->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 768});
    _content_labels.push_back(
        makeLabel(_settings.get(), "All values above are stored in device NVS",
                  kRowWidth, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 820)
    );

    _dialog_scrim = std::make_unique<LvContainer>(_screen.get());
    // Keep edge-owned Back/OK/Close controls outside the modal hit area.
    _dialog_scrim->setStyleAttribute(StyleSize::RECT(320, 320));
    _dialog_scrim->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_CENTER, 0, 0});
    lv_obj_set_style_bg_color(_dialog_scrim->getNativeHandle(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_dialog_scrim->getNativeHandle(), LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_dialog_scrim->getNativeHandle(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(_dialog_scrim->getNativeHandle(), 0, LV_PART_MAIN);
    lv_obj_clear_flag(_dialog_scrim->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_dialog_scrim->getNativeHandle(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(_dialog_scrim->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);
    setVisible(*_dialog_scrim, false);

    _dialog = std::make_unique<LvContainer>(_screen.get());
    _dialog->setStyleAttribute(StyleSize::RECT(252, 142));
    _dialog->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_CENTER, 0, 0});
    surface(*_dialog);
    lv_obj_set_style_bg_opa(_dialog->getNativeHandle(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_dialog->getNativeHandle(), 14, LV_PART_MAIN);
    lv_obj_set_style_radius(_dialog->getNativeHandle(), 12, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_dialog->getNativeHandle(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_border_width(_dialog->getNativeHandle(), 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(_dialog->getNativeHandle(), lv_color_hex(kDialogBorder), LV_PART_MAIN);
    lv_obj_set_style_border_opa(_dialog->getNativeHandle(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(_dialog->getNativeHandle(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_dialog->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);
    _dialog_title = makeLabel(_dialog.get(), "FORMAT SD?", 220, 16, kNavy,
                              STYLE_ALIGN_TYPE_TOP_MID, 0, 14);
    _dialog_text = makeLabel(_dialog.get(), "This erases the card. Are you sure?", 220, 12, kMuted,
                             STYLE_ALIGN_TYPE_TOP_MID, 0, 46);
    _dialog_progress = std::make_unique<AppProgressBar>(_dialog.get());
    _dialog_progress->setRange(0, 100);
    _dialog_progress->setValue(0);
    _dialog_progress->setWidth(190);
    _dialog_progress->setHeight(8);
    _dialog_progress->setBackgroundColor(StyleColor::COLOR(kTrack));
    lv_obj_set_style_bg_color(_dialog_progress->getNativeHandle(), lv_color_hex(kTeal), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_dialog_progress->getNativeHandle(), LV_OPA_COVER, LV_PART_INDICATOR);
    _dialog_progress->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 76});
    setVisible(*_dialog_progress, false);
    _dialog_confirm = std::make_unique<AppButton>(_dialog.get(), [this]() { requestFormat(); });
    _dialog_confirm->setText("YES");
    buttonStyle(*_dialog_confirm, 78, 30, kTeal);
    flatButtonBackground(*_dialog_confirm, kTeal);
    _dialog_confirm->align({STYLE_ALIGN_TYPE_BOTTOM_LEFT, 28, -14});
    _dialog_cancel = std::make_unique<AppButton>(_dialog.get(), [this]() { closeDialog(); });
    _dialog_cancel->setText("NO");
    buttonStyle(*_dialog_cancel, 78, 30, kSlate);
    flatButtonBackground(*_dialog_cancel, kSlate);
    _dialog_cancel->align({STYLE_ALIGN_TYPE_BOTTOM_RIGHT, -28, -14});

    _edge_navigation = std::make_unique<EdgeNavigation>();
    if (!_edge_navigation->Begin(_screen->getNativeHandle())) {
        ESP_LOGE("qsee_navigation", "Edge navigation creation failed");
        return false;
    }
    _edge_navigation->SetLeftCallback([this]() {
        if (_dialog != nullptr &&
            !lv_obj_has_flag(_dialog->getNativeHandle(), LV_OBJ_FLAG_HIDDEN)) {
            closeDialog();
            return;
        }
        (void)back();
    });
    _edge_navigation->SetRightCallback([this]() {
        if (_dialog_requires_confirmation) {
            requestFormat();
        } else {
            closeDialog();
        }
    });

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
    const char *route = screen == Screen::Welcome ? "welcome" :
                        screen == Screen::Logs ? "logs" :
                        screen == Screen::Hardware ? "hardware" :
                        screen == Screen::Audio ? "audio" :
                        screen == Screen::Imu ? "imu" :
                        screen == Screen::About ? "about" : "settings";
    ESP_LOGI("qsee_navigation", "Opening route: %s", route);
    if (_dialog != nullptr) {
        closeDialog();
    }
    _current = screen;
    const bool diagnostic = screen == Screen::Audio || screen == Screen::Imu;
    const esp_err_t monitor_result = qsee_hardware_set_diagnostic_monitor(diagnostic);
    if (monitor_result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Diagnostic monitor toggle failed for %s (%s)",
                 route, esp_err_to_name(monitor_result));
    }
    const esp_err_t system_monitor_result =
        qsee_hardware_set_system_monitor(screen == Screen::Welcome);
    if (system_monitor_result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "System monitor toggle failed for %s (%s)",
                 route, esp_err_to_name(system_monitor_result));
    }
    setVisible(*_welcome, screen == Screen::Welcome);
    setVisible(*_logs, screen == Screen::Logs);
    setVisible(*_hardware, screen == Screen::Hardware);
    setVisible(*_audio, screen == Screen::Audio);
    setVisible(*_imu, screen == Screen::Imu);
    setVisible(*_settings, screen == Screen::Settings);
    setVisible(*_open_logs, screen == Screen::Welcome);
    setVisible(*_open_hardware, screen == Screen::Welcome);
    setVisible(*_open_audio, screen == Screen::Welcome);
    setVisible(*_open_imu, screen == Screen::Welcome);
    setVisible(*_open_about, screen == Screen::Welcome);
    setVisible(*_open_settings, screen == Screen::Welcome);
    _edge_navigation->SetBackVisible(screen != Screen::Welcome);
    _edge_navigation->SetRightMode(EdgeNavigation::RightMode::None);
    setVisible(*_format_sd, false);
    setVisible(*_dialog_scrim, false);
    setVisible(*_dialog, false);
    setVisible(*_about, screen == Screen::About);
    _page_title->setText(screen == Screen::Welcome ? "WELCOME" : screen == Screen::Logs ? "LIVE LOGS" :
                         screen == Screen::Hardware ? "HARDWARE" : screen == Screen::Audio ? "AUDIO SCOPE" :
                         screen == Screen::Imu ? "IMU LEVEL" : screen == Screen::About ? "ABOUT" : "SETTINGS");
    _page_subtitle->setText(screen == Screen::Welcome ? "QSee device console" :
                            screen == Screen::Logs ? "RAM ring, newest lines" :
                            screen == Screen::Hardware ? "Component inventory" :
                            screen == Screen::Audio ? "ES7210 microphone diagnostics" :
                            screen == Screen::Imu ? "QMI8658 orientation and calibration" :
                            screen == Screen::About ? "About this device" : "Diagnostics and sampling");
    refresh();
    if (screen == Screen::Settings) updateSettings();
    ESP_LOGI("qsee_navigation", "Route ready: %s", route);
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
    (void)qsee_hardware_set_system_monitor(false);
    if (_dialog != nullptr) {
        closeDialog();
    }
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
    _psram.bar.reset();
    _psram.value.reset();
    _psram.label.reset();
    _psram.card.reset();
    _flash.bar.reset();
    _flash.value.reset();
    _flash.label.reset();
    _flash.card.reset();
    _welcome_devices.clear();
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
    _content_labels.clear();
    _logs_pause.reset();
    _logs_clear.reset();
    _logs_follow.reset();
    _log_search.reset();
    _log_filter.reset();
    _logs_live.reset();
    _hardware_status_dots.clear();
    _hardware_parts.clear();
    _hardware_rows.clear();
    _dialog_title.reset();
    _dialog_text.reset();
    _dialog_progress.reset();
    _dialog_confirm.reset();
    _dialog_cancel.reset();
    _dialog_scrim.reset();
    _dialog.reset();
    _page_title.reset();
    _page_subtitle.reset();
    _title_bar.reset();
    _open_logs.reset();
    _open_hardware.reset();
    _open_audio.reset();
    _open_imu.reset();
    _open_about.reset();
    _open_settings.reset();
    if (_edge_navigation != nullptr) {
        _edge_navigation->Shutdown();
        _edge_navigation.reset();
    }
    _welcome.reset();
    _logs.reset();
    _hardware.reset();
    _audio.reset();
    _imu.reset();
    _about_tagline.reset();
    _about_website.reset();
    _about_email.reset();
    _about_copyright.reset();
    _settings_period.reset();
    _settings_faster.reset();
    _settings_slower.reset();
    _settings_default.reset();
    _settings_smoothing.reset();
    _settings_smoothing_less.reset();
    _settings_smoothing_more.reset();
    _settings_storage.reset();
    _settings_sd_mirror.reset();
    _settings_wifi.reset();
    _settings_wifi_start.reset();
    _settings_brightness.reset();
    _settings_brightness_down.reset();
    _settings_brightness_up.reset();
    _settings.reset();
    _about.reset();
    if (_log_snapshot_buffer != nullptr) {
        heap_caps_free(_log_snapshot_buffer);
        _log_snapshot_buffer = nullptr;
    }
    if (_log_filtered_buffer != nullptr) {
        heap_caps_free(_log_filtered_buffer);
        _log_filtered_buffer = nullptr;
    }
    _surface.reset();
    _screen.reset();
}

void Navigation::updateMetric(Metric &metric, const char *value, int progress)
{
    metric.value->setText(value);
    metric.bar->setValue(progress < 0 ? 0 : progress > 100 ? 100 : progress, false);
}

void Navigation::updateWelcomeDevices(const DeviceSnapshot &snapshot)
{
    if (_welcome_devices.size() < 4U) return;

    const auto setLine = [this](size_t index, const char *text, uint32_t color) {
        _welcome_devices[index].value->setText(text);
        _welcome_devices[index].value->setTextColor(StyleColor::COLOR(color));
        _welcome_devices[index].icon->setTextColor(StyleColor::COLOR(color));
    };

    char text[96];
    if (snapshot.imu.available) {
        std::snprintf(
            text,
            sizeof(text),
            "%s  %.1f C  %u samples",
            snapshot.imu.calibrated ? "CALIBRATED" : "CAL PENDING",
            static_cast<double>(snapshot.imu.temperature_c),
            static_cast<unsigned>(snapshot.imu.calibration_samples)
        );
        setLine(0, text, snapshot.imu.calibrated ? kTeal : kWarning);
    } else {
        setLine(0, "QMI8658 unavailable", kWarning);
    }

    const WifiSnapshot &wifi = snapshot.radios.wifi;
    if (wifi.connected) {
        std::snprintf(
            text,
            sizeof(text),
            "%s  %d dBm  %s",
            wifi.ssid[0] != '\0' ? wifi.ssid : "CONNECTED",
            static_cast<int>(wifi.rssi),
            wifi.ip_acquired && wifi.ip_address[0] != '\0' ? wifi.ip_address : "IP pending"
        );
        setLine(1, text, kTeal);
    } else if (wifi.started) {
        std::snprintf(
            text,
            sizeof(text),
            "%s  attempt %lu",
            wifi.reconnect_pending ? "RECONNECTING" : "SEARCHING",
            static_cast<unsigned long>(wifi.connect_attempt_count)
        );
        setLine(1, text, kWarning);
    } else {
        setLine(1, "OFF - enable in Settings", kWarning);
    }

    const BleSnapshot &ble = snapshot.radios.ble;
    if (ble.connected) {
        std::snprintf(
            text,
            sizeof(text),
            "CONNECTED  total %lu",
            static_cast<unsigned long>(ble.connection_count)
        );
        setLine(2, text, kTeal);
    } else if (ble.provisioning) {
        std::snprintf(
            text,
            sizeof(text),
            "PROVISIONING  %s",
            ble.device_name[0] != '\0' ? ble.device_name : "QSee"
        );
        setLine(2, text, kWarning);
    } else if (ble.advertising) {
        std::snprintf(
            text,
            sizeof(text),
            "ADVERTISING  %s",
            ble.device_name[0] != '\0' ? ble.device_name : "QSee"
        );
        setLine(2, text, kTeal);
    } else {
        setLine(2, "OFF - enable in Settings", kWarning);
    }

    if (snapshot.gps.available && snapshot.gps.has_fix) {
        std::snprintf(
            text,
            sizeof(text),
            "FIX  %u sat  HDOP %.1f",
            static_cast<unsigned>(snapshot.gps.satellites),
            static_cast<double>(snapshot.gps.hdop)
        );
        setLine(3, text, kTeal);
    } else if (snapshot.gps.available) {
        std::snprintf(
            text,
            sizeof(text),
            "NO FIX  %u sat  %lu B",
            static_cast<unsigned>(snapshot.gps.satellites),
            static_cast<unsigned long>(snapshot.gps.pending_nmea_bytes)
        );
        setLine(3, text, kWarning);
    } else {
        setLine(3, "LC76G unavailable", kWarning);
    }
}

void Navigation::refresh()
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    char value[64];
    if (snapshot.memory.internal_heap_available) {
        std::snprintf(
            value,
            sizeof(value),
            "%lu/%lu KB  %u%%",
            static_cast<unsigned long>(snapshot.memory.internal_used_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.internal_total_bytes / 1024U),
            static_cast<unsigned>(snapshot.memory.internal_used_percent)
        );
        updateMetric(_ram, value, snapshot.memory.internal_used_percent);
    } else {
        updateMetric(_ram, "unavailable", -1);
    }

    if (snapshot.memory.psram_heap_available) {
        std::snprintf(
            value,
            sizeof(value),
            "%lu/%lu KB  %u%%",
            static_cast<unsigned long>(snapshot.memory.psram_used_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.psram_heap_total_bytes / 1024U),
            static_cast<unsigned>(snapshot.memory.psram_used_percent)
        );
        updateMetric(_psram, value, snapshot.memory.psram_used_percent);
    } else {
        updateMetric(_psram, "unavailable", -1);
    }

    if (snapshot.memory.firmware_usage_available) {
        std::snprintf(
            value,
            sizeof(value),
            "%lu/%lu KB  %u%%",
            static_cast<unsigned long>(snapshot.memory.firmware_used_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.firmware_total_bytes / 1024U),
            static_cast<unsigned>(snapshot.memory.firmware_used_percent)
        );
        updateMetric(_flash, value, snapshot.memory.firmware_used_percent);
    } else {
        updateMetric(_flash, "unavailable", -1);
    }

    if (snapshot.storage.health == DeviceHealth::Ready && snapshot.storage.capacity_bytes > 0) {
        const uint64_t used_bytes = snapshot.storage.capacity_bytes > snapshot.storage.free_bytes
            ? snapshot.storage.capacity_bytes - snapshot.storage.free_bytes : 0;
        const int used_percent = static_cast<int>((used_bytes * 100ULL) /
                                                   snapshot.storage.capacity_bytes);
        std::snprintf(value, sizeof(value), "%llu/%llu MB  %d%%",
                      static_cast<unsigned long long>(used_bytes / (1024ULL * 1024ULL)),
                      static_cast<unsigned long long>(snapshot.storage.capacity_bytes / (1024ULL * 1024ULL)),
                      used_percent);
        updateMetric(_sd, value, used_percent);
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
    Metric *cpu_metrics[] = {&_cpu0, &_cpu1};
    for (size_t core = 0; core < 2U; ++core) {
        if (core < snapshot.processor.core_count &&
            snapshot.processor.cores[core].utilization_available) {
            std::snprintf(
                value,
                sizeof(value),
                "%u%%  %s",
                static_cast<unsigned>(snapshot.processor.cores[core].utilization_percent),
                snapshot.processor.cores[core].current_task
            );
            updateMetric(
                *cpu_metrics[core],
                value,
                snapshot.processor.cores[core].utilization_percent
            );
            cpu_metrics[core]->value->setTextColor(StyleColor::COLOR(kNavy));
        } else {
            updateMetric(*cpu_metrics[core], "sampling...", -1);
            cpu_metrics[core]->value->setTextColor(StyleColor::COLOR(kMuted));
        }
    }
    updateWelcomeDevices(snapshot);
    if (_current == Screen::Logs && !_logs_paused) {
        updateLogView();
    } else if (_current == Screen::Hardware) {
        updateHardwareText();
        const bool needs_format = storageFormatRequired(snapshot.storage);
        setVisible(*_format_sd, needs_format);
    } else if (_current == Screen::Settings) {
        updateSettings();
    }
    updateDiagnosticViews();
}

void Navigation::updateDiagnosticViews()
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    char text[160];
    const int level0 = std::min(100, static_cast<int>(snapshot.audio.channel_mean_level[0] / 328U));
    const int level1 = std::min(100, static_cast<int>(snapshot.audio.channel_mean_level[1] / 328U));
    std::snprintf(text, sizeof(text), "MICROPHONES %s", health(snapshot.audio.microphone.health));
    _audio_status->setText(text);
    _audio_mic0->setValue(level0, true);
    _audio_mic1->setValue(level1, true);
    std::snprintf(text, sizeof(text), "PCM %u ch  |  %lu Hz  |  peak %lu / %lu",
                  static_cast<unsigned>(snapshot.audio.channel_count),
                  static_cast<unsigned long>(snapshot.audio.sample_rate_hz),
                  static_cast<unsigned long>(snapshot.audio.channel_peak_level[0]),
                  static_cast<unsigned long>(snapshot.audio.channel_peak_level[1]));
    _audio_channels->setText(text);

    std::snprintf(text, sizeof(text), "QMI8658 %s", health(snapshot.imu.health));
    _imu_status->setText(text);
    std::snprintf(text, sizeof(text), "ACC X %+.2f Y %+.2f Z %+.2f m/s2",
                  snapshot.imu.acceleration_mps2[0], snapshot.imu.acceleration_mps2[1],
                  snapshot.imu.acceleration_mps2[2]);
    _imu_accel->setText(text);
    std::snprintf(text, sizeof(text), "GYRO X %+.2f Y %+.2f Z %+.2f dps",
                  snapshot.imu.angular_velocity_dps[0], snapshot.imu.angular_velocity_dps[1],
                  snapshot.imu.angular_velocity_dps[2]);
    _imu_gyro->setText(text);
    std::snprintf(text, sizeof(text), "TEMP %.1f C", snapshot.imu.temperature_c);
    _imu_temperature->setText(text);
    std::snprintf(text, sizeof(text), "CALIBRATION %s  |  %u samples",
                  snapshot.imu.calibrated ? "READY" : "PENDING",
                  static_cast<unsigned>(snapshot.imu.calibration_samples));
    _imu_calibration->setText(text);
}

void Navigation::showAudio()
{
    show(Screen::Audio);
}

void Navigation::showImu()
{
    show(Screen::Imu);
}

void Navigation::adjustDiagnosticPeriod(int delta_ms)
{
    const int current = static_cast<int>(qsee_hardware_get_diagnostic_period_ms());
    const int requested = std::max(100, std::min(2000, current + delta_ms));
    if (qsee_hardware_set_diagnostic_period_ms(static_cast<uint32_t>(requested)) == ESP_OK) {
        updateSettings();
    }
}

void Navigation::adjustImuSmoothing(int delta_percent)
{
    const int current =
        static_cast<int>(qsee_hardware_get_imu_smoothing_percent());
    const int requested = std::max(0, std::min(90, current + delta_percent));
    if (qsee_hardware_set_imu_smoothing_percent(
            static_cast<uint32_t>(requested)
        ) == ESP_OK) {
        updateSettings();
    }
}

void Navigation::toggleSdMirror()
{
    const bool enabled = !qsee_storage_get_logger_mirror_preference();
    const esp_err_t result = qsee_storage_set_logger_mirror(enabled);
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "SD mirror update failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::startWifi()
{
    const esp_err_t result = qsee_wifi_init();
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Wi-Fi start failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::adjustBrightness(int delta_percent)
{
    const int current =
        static_cast<int>(qsee_display_get_brightness_percent());
    const int requested = std::max(10, std::min(100, current + delta_percent));
    const esp_err_t result =
        qsee_display_set_brightness_percent(static_cast<uint32_t>(requested));
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Brightness update failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::updateSettings()
{
    if (_settings_period == nullptr || _settings_smoothing == nullptr ||
        _settings_storage == nullptr || _settings_wifi == nullptr ||
        _settings_brightness == nullptr) {
        return;
    }
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    char text[96];
    std::snprintf(
        text,
        sizeof(text),
        "Sample interval %lu ms",
        static_cast<unsigned long>(qsee_hardware_get_diagnostic_period_ms())
    );
    _settings_period->setText(text);
    std::snprintf(
        text,
        sizeof(text),
        "IMU input smoothing %lu%%",
        static_cast<unsigned long>(qsee_hardware_get_imu_smoothing_percent())
    );
    _settings_smoothing->setText(text);

    const bool mirror = qsee_storage_get_logger_mirror_preference();
    std::snprintf(
        text,
        sizeof(text),
        "SD %s  |  log mirror %s",
        snapshot.storage.health == DeviceHealth::Ready ? "READY" :
        snapshot.storage.card_present ? "NOT MOUNTED" : "ABSENT",
        mirror ? "ON" : "OFF"
    );
    _settings_storage->setText(text);
    _settings_sd_mirror->setText(mirror ? "LOG MIRROR ON" : "LOG MIRROR OFF");

    std::snprintf(
        text,
        sizeof(text),
        "Wi-Fi %s  |  %s",
        snapshot.radios.wifi.started ? "STARTED" : "DEFERRED",
        snapshot.radios.wifi.ip_acquired ? snapshot.radios.wifi.ip_address :
        snapshot.radios.wifi.credentials_configured ? "credentials saved" :
        "not configured"
    );
    _settings_wifi->setText(text);
    _settings_wifi_start->setText(
        snapshot.radios.wifi.started ? "WI-FI STARTED" : "START WI-FI"
    );

    std::snprintf(
        text,
        sizeof(text),
        "AMOLED brightness %lu%%",
        static_cast<unsigned long>(qsee_display_get_brightness_percent())
    );
    _settings_brightness->setText(text);
}

void Navigation::updateLogView()
{
    if (_log_box == nullptr || _log_snapshot_buffer == nullptr || _log_filtered_buffer == nullptr) {
        if (_log_box != nullptr) {
            _log_box->setText("Log buffers unavailable", false);
        }
        return;
    }
    qsee_logger_snapshot(_log_snapshot_buffer, kLogBufferBytes, 32);
    const char *query = _log_search != nullptr ? _log_search->getText() : "";
    filterLogs(_log_snapshot_buffer, query, _log_filter_mode,
               _log_filtered_buffer, kLogBufferBytes);
    _log_box->setText(
        _log_filtered_buffer,
        _logs_follow_latest && _log_box->isAtBottom()
    );
}

void Navigation::showComponentDetails(size_t index)
{
    if (_current != Screen::Hardware || index >= _hardware_rows.size()) return;
    static constexpr const char *names[] = {
        "BATTERY", "RTC", "IMU", "GPS", "WIFI", "BLE",
        "DISPLAY", "TOUCH", "SPEAKER", "MICROPHONE", "SD CARD"
    };
    static constexpr const char *parts[] = {
        "AXP2101", "PCF85063", "QMI8658", "LC76G", "ESP32-S3", "ESP32-S3",
        "CO5300", "CST9217", "ES8311", "ES7210", "SDMMC"
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
    char title[64];
    char details[180];
    const char *state = hardwareStateText(index, snapshot, states);
    const uint32_t color = hardwareStatusColor(index, snapshot, states[index], errors[index]);
    std::snprintf(title, sizeof(title), "%s  %s", names[index], parts[index]);
    std::snprintf(details, sizeof(details), "State: %s\nError: %s\nUse edge CLOSE to return.",
                  state,
                  esp_err_to_name(errors[index]));
    setVisible(*_dialog_scrim, true);
    setVisible(*_dialog, true);
    _dialog_title->setText(title);
    _dialog_text->setText(details);
    setVisible(*_dialog_progress, false);
    setVisible(*_dialog_confirm, false);
    setVisible(*_dialog_cancel, false);
    _dialog_requires_confirmation = false;
    _edge_navigation->SetRightMode(EdgeNavigation::RightMode::Close);
    _dialog_text->setTextColor(StyleColor::COLOR(color));
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
    const esp_err_t errors[] = {
        snapshot.battery.last_error, snapshot.rtc.last_error, snapshot.imu.last_error,
        snapshot.gps.last_error, snapshot.radios.wifi.last_error, snapshot.radios.ble.last_error,
        snapshot.ui.display.last_error, snapshot.ui.touch.last_error,
        snapshot.audio.speaker.last_error, snapshot.audio.microphone.last_error,
        snapshot.storage.last_error
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        auto &state = _hardware_labels[i * 2 + 1];
        const uint32_t color = hardwareStatusColor(i, snapshot, states[i], errors[i]);
        state->setText(hardwareStateText(i, snapshot, states));
        state->setTextColor(StyleColor::COLOR(color));
        if (i < _hardware_status_dots.size()) {
            lv_obj_set_style_bg_color(
                _hardware_status_dots[i]->getNativeHandle(), lv_color_hex(color), LV_PART_MAIN
            );
        }
    }
    char summary[96];
    std::snprintf(summary, sizeof(summary), "SD %llu/%llu MB | MIRROR %s",
                  static_cast<unsigned long long>(snapshot.storage.free_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(snapshot.storage.capacity_bytes / (1024ULL * 1024ULL)),
                  snapshot.storage.logger_mirror_enabled ? "ON" : "OFF");
    _hardware_summary->setText(summary);
}

void Navigation::showFormatDialog()
{
    if (_current != Screen::Hardware) return;
    setVisible(*_dialog_scrim, true);
    setVisible(*_dialog, true);
    _dialog_title->setText("FORMAT SD CARD?");
    _dialog_text->setText("This erases the card. Are you sure?");
    setVisible(*_dialog_confirm, false);
    setVisible(*_dialog_cancel, false);
    setVisible(*_dialog_progress, false);
    _edge_navigation->SetRightMode(EdgeNavigation::RightMode::Ok);
    _dialog_requires_confirmation = true;
    _dialog_text->setTextColor(StyleColor::COLOR(kMuted));
}

void Navigation::closeDialog()
{
    if (_dialog_scrim != nullptr) {
        setVisible(*_dialog_scrim, false);
    }
    if (_dialog != nullptr) {
        setVisible(*_dialog, false);
    }
    _dialog_requires_confirmation = false;
    if (_edge_navigation != nullptr) {
        _edge_navigation->SetRightMode(EdgeNavigation::RightMode::None);
    }
}

void Navigation::requestFormat()
{
    setVisible(*_dialog_confirm, false);
    setVisible(*_dialog_cancel, false);
    setVisible(*_dialog_progress, true);
    _dialog_requires_confirmation = false;
    _dialog_progress->setValue(10, false);
    _dialog_title->setText("FORMATTING SD");
    _dialog_text->setText("Storage service is preparing the operation...");
    const esp_err_t result = qsee_storage_request_format();
    if (result != ESP_OK) {
        _dialog_progress->setValue(0, false);
        _dialog_title->setText("FORMAT UNAVAILABLE");
        _dialog_text->setText("The hardware storage service does not expose formatting yet.");
        _edge_navigation->SetRightMode(EdgeNavigation::RightMode::Close);
    } else {
        _edge_navigation->SetRightMode(EdgeNavigation::RightMode::None);
    }
}

}  // namespace qsee
