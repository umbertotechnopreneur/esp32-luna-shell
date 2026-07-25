#include "qsee_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "qrcode.h"
#include "qsee_background_asset.hpp"
#include "qsee_device_snapshot.hpp"
#include "qsee_http_server.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"
#include "qsee_provisioning.hpp"

// Navigation controller for the QSee.ai shell and screen transitions.
// Centralizes stack management so app screens stay self-contained.

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
constexpr double kPcm16FullScale = 32768.0;
constexpr double kAudioMeterFloorDbfs = -60.0;
constexpr uint32_t kUiTickMs = 100;
constexpr uint32_t kFullRefreshTicks = 1000 / kUiTickMs;
constexpr int64_t kAudioPeakHoldUs = 1000000;
constexpr float kAudioMeterReleasePerSecond = 36.0F;
constexpr float kAudioPeakReleasePerSecond = 14.0F;
constexpr float kImuGyroVisualRangeDps = 128.0F;
constexpr int kQrCanvasSize = 216;
constexpr size_t kWifiOptionsBufferBytes = 1024;
constexpr int64_t kPairingTimeoutUs = 5LL * 60LL * 1000000LL;
constexpr char kEspressifAppStoreUrl[] =
    "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr lv_style_selector_t kPressedSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
constexpr uint32_t kBrightnessLowPercent = 35;
constexpr uint32_t kBrightnessDefaultPercent = 70;
constexpr uint32_t kBrightnessHighPercent = 100;
constexpr uint32_t kDiagnosticPeriodOptions[] = {
    100, 200, 300, 500, 750, 1000, 1500, 2000
};
constexpr char kDiagnosticPeriodLabels[] =
    "100 ms\n200 ms\n300 ms\n500 ms\n750 ms\n1 second\n"
    "1.5 seconds\n2 seconds";
constexpr uint32_t kImuSmoothingOptions[] = {0, 25, 50, 70, 80, 90};
constexpr char kImuSmoothingLabels[] =
    "Off\n25%\n50%\n70% - balanced\n80%\n90% - smoothest";
constexpr uint32_t kBrightnessOptions[] = {
    kBrightnessLowPercent,
    kBrightnessDefaultPercent,
    kBrightnessHighPercent
};
constexpr char kBrightnessLabels[] =
    "Low - 35%\nBalanced - 70%\nMaximum - 100%";
constexpr uint32_t kDimTimeoutOptions[] = {15, 30, 60, 120, 0};
constexpr char kDimTimeoutLabels[] =
    "15 seconds\n30 seconds\n1 minute\n2 minutes\nNever";
constexpr uint32_t kPanelOffDelayOptions[] = {15, 30, 60, 120, 300, 0};
constexpr char kPanelOffDelayLabels[] =
    "15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\nNever";
constexpr uint32_t kCommandBarTimeoutOptions[] = {
    10, 15, 30, 60, 120, 300, 600
};
constexpr char kCommandBarTimeoutLabels[] =
    "10 seconds\n"
    "15 seconds\n"
    "30 seconds\n"
    "1 minute\n"
    "2 minutes\n"
    "5 minutes\n"
    "10 minutes";
constexpr char kWifiActionLabels[] =
    "Choose an action\n"
    "Start Wi-Fi radio\n"
    "Scan nearby networks\n"
    "Edit SSID and password";
constexpr char kBleActionLabels[] =
    "Choose an action\n"
    "Pair with Espressif app";

// Return the exact preset, or the closest displayable value for old settings.
template <size_t Count>
size_t presetIndex(uint32_t current, const uint32_t (&options)[Count])
{
    size_t best = 0;
    uint32_t best_distance =
        current > options[0] ? current - options[0] : options[0] - current;
    for (size_t index = 0; index < Count; ++index) {
        if (options[index] == current) {
            return index;
        }
        const uint32_t distance =
            current > options[index]
                ? current - options[index]
                : options[index] - current;
        if (distance < best_distance) {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

// Identify the single route that owns every ordinary device setting.
bool isSettingsScreen(qsee::Navigation::Screen screen)
{
    using Screen = qsee::Navigation::Screen;
    return screen == Screen::Settings;
}

// Keep provisioning lifecycle active across all wizard stages.
bool isPairingScreen(qsee::Navigation::Screen screen)
{
    using Screen = qsee::Navigation::Screen;
    return screen == Screen::PairIntro ||
           screen == Screen::PairBluetooth ||
           screen == Screen::PairApp ||
           screen == Screen::PairQr ||
           screen == Screen::AppStoreQr ||
           screen == Screen::PairResult;
}

// Identify routes whose lower command arc should react to inactivity.
bool hasAdaptiveCommandBar(qsee::Navigation::Screen screen)
{
    using Screen = qsee::Navigation::Screen;
    return screen == Screen::Welcome ||
           screen == Screen::Logs ||
           screen == Screen::Audio ||
           screen == Screen::WifiCredentials ||
           isPairingScreen(screen);
}

// Map a persisted timeout to the dropdown's ordered preset list.
size_t commandBarTimeoutIndex(uint32_t seconds)
{
    return presetIndex(seconds, kCommandBarTimeoutOptions);
}

// Unwind the full snapshot frame before a network-selection callback navigates.
bool copyWifiNetworkSnapshot(
    uint32_t selected,
    qsee::WifiNetworkSnapshot *network
)
{
    if (selected == 0U || network == nullptr) {
        return false;
    }
    const qsee::DeviceSnapshot snapshot =
        qsee::DeviceSnapshotService::instance().snapshot();
    if (selected > snapshot.radios.wifi.network_count) {
        return false;
    }
    *network = snapshot.radios.wifi.networks[selected - 1U];
    return true;
}

void wipeBytes(void *data, size_t size)
{
    auto *cursor = static_cast<volatile uint8_t *>(data);
    while (cursor != nullptr && size-- > 0U) {
        *cursor++ = 0U;
    }
}

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

    lv_obj_set_style_bg_image_src(screen, nullptr, LV_PART_MAIN);
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
    qsee::AdaptiveCommandBar *command_bar
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
        size >= 52 ? &lv_font_montserrat_28 : &lv_font_montserrat_18,
        LV_PART_MAIN
    );
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    if (command_bar != nullptr) {
        command_bar->registerControl(native);
    }
}

// Update the last icon label created by styleArcButton without exposing it as screen state.
void setArcButtonIcon(AppButton &button, const char *symbol)
{
    lv_obj_t *native = button.getNativeHandle();
    lv_obj_t *icon = native != nullptr ? lv_obj_get_child(native, -1) : nullptr;
    if (icon != nullptr && lv_obj_check_type(icon, &lv_label_class)) {
        lv_label_set_text(icon, symbol);
    }
}

void styleBottomArcCommand(
    AppButton &button,
    int x,
    int y,
    uint32_t color,
    qsee::AdaptiveCommandBar *command_bar
)
{
    buttonStyle(button, 104, 44, color);
    button.align({STYLE_ALIGN_TYPE_TOP_LEFT, x, y});

    lv_obj_t *native = button.getNativeHandle();
    lv_obj_set_style_border_width(native, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        native,
        lv_color_hex(color == kTeal ? 0x2BE7D0 : 0x69B9FF),
        LV_PART_MAIN
    );
    lv_obj_set_style_border_opa(native, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(native, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_x(native, 0, kPressedSelector);
    lv_obj_set_style_translate_y(native, 0, kPressedSelector);
    lv_obj_set_style_transform_width(native, 0, kPressedSelector);
    lv_obj_set_style_transform_height(native, 0, kPressedSelector);
    if (command_bar != nullptr) {
        command_bar->registerControl(native);
    }
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
    metric.label = makeLabel(metric.card.get(), name, 92, 12, kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 3);
    metric.value = makeLabel(metric.card.get(), "--", 136, 12, kNavy, STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 3);
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
    bar->setWidth(kRowWidth);
    bar->setHeight(12);
    bar->setBackgroundColor(StyleColor::COLOR(kTrack));
    (void)top;
    (void)bottom;
    // The draw callback paints absolute dBFS threshold bands. A gradient on the
    // indicator itself would incorrectly squeeze green/yellow/red into any
    // short fill because LVGL scales it to the current indicator width.
    lv_obj_set_style_bg_opa(bar->getNativeHandle(), LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar->getNativeHandle(), 6, LV_PART_MAIN);
    bar->align({STYLE_ALIGN_TYPE_TOP_MID, 0, y});
    return bar;
}

// Paint fixed dBFS bands and the independent peak-hold marker.
void audioMeterDrawEvent(lv_event_t *event)
{
    lv_obj_t *bar = lv_event_get_target_obj(event);
    auto *levels = static_cast<float *>(lv_event_get_user_data(event));
    lv_layer_t *layer = lv_event_get_layer(event);
    if (bar == nullptr || levels == nullptr || layer == nullptr) {
        return;
    }

    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    area.x1 += 1;
    area.x2 -= 1;
    area.y1 += 2;
    area.y2 -= 2;
    const int width = lv_area_get_width(&area);
    const float display_level = std::clamp(levels[0], 0.0F, 100.0F);
    const float hold_level = std::clamp(levels[1], 0.0F, 100.0F);

    struct MeterBand {
        float start;
        float end;
        uint32_t color;
    };
    // -60..0 dBFS maps to 0..100: -12 dBFS=80 and -3 dBFS=95.
    static constexpr MeterBand bands[] = {
        {0.0F, 80.0F, 0x18C97A},
        {80.0F, 95.0F, 0xF4C542},
        {95.0F, 100.0F, 0xED4245},
    };
    for (const MeterBand &band : bands) {
        const float visible_end = std::min(display_level, band.end);
        if (visible_end <= band.start) {
            continue;
        }
        lv_area_t segment = area;
        segment.x1 = area.x1 + static_cast<int>(
            (width - 1) * band.start / 100.0F + 0.5F
        );
        segment.x2 = area.x1 + static_cast<int>(
            (width - 1) * visible_end / 100.0F + 0.5F
        );
        lv_draw_rect_dsc_t fill;
        lv_draw_rect_dsc_init(&fill);
        fill.bg_opa = LV_OPA_COVER;
        fill.bg_color = lv_color_hex(band.color);
        fill.radius = 3;
        lv_draw_rect(layer, &fill, &segment);
    }

    if (hold_level <= 0.0F) {
        return;
    }
    const int32_t marker_x = area.x1 + static_cast<int32_t>(
        (width - 1) * hold_level / 100.0F + 0.5F
    );
    lv_area_t marker_border = area;
    marker_border.x1 = std::max(area.x1, marker_x - 2);
    marker_border.x2 = std::min(area.x2, marker_x + 2);
    lv_draw_rect_dsc_t marker;
    lv_draw_rect_dsc_init(&marker);
    marker.bg_opa = LV_OPA_COVER;
    marker.bg_color = lv_color_hex(0x071018);
    marker.radius = 1;
    lv_draw_rect(layer, &marker, &marker_border);

    lv_area_t marker_core = marker_border;
    marker_core.x1 = std::max(area.x1, marker_x - 1);
    marker_core.x2 = std::min(area.x2, marker_x + 1);
    marker.bg_color = lv_color_white();
    lv_draw_rect(layer, &marker, &marker_core);
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

// Convert an unsigned PCM16 peak to a finite dBFS floor.
double audioPeakDbfs(uint32_t peak)
{
    if (peak == 0U) {
        return kAudioMeterFloorDbfs;
    }
    const double normalized = std::min(1.0, static_cast<double>(peak) / kPcm16FullScale);
    return 20.0 * std::log10(normalized);
}

// Map PCM amplitude onto the meter's logarithmic -60..0 dBFS scale.
int audioPeakMeterValue(uint32_t peak)
{
    if (peak == 0U) {
        return 0;
    }
    // A logarithmic dBFS scale keeps real low-level PCM input visible.
    const double dbfs = std::max(kAudioMeterFloorDbfs, audioPeakDbfs(peak));
    return static_cast<int>(((dbfs - kAudioMeterFloorDbfs) * 100.0 /
                             -kAudioMeterFloorDbfs) + 0.5);
}

// Format one peak with both percent-full-scale and dBFS units.
void formatAudioPeak(char *text, size_t text_size, unsigned channel, uint32_t peak)
{
    const double percent_full_scale =
        std::min(100.0, static_cast<double>(peak) * 100.0 / kPcm16FullScale);
    if (peak == 0U) {
        std::snprintf(
            text, text_size, "PEAK CH%u  %.2f %%FS | -inf dBFS",
            channel, percent_full_scale
        );
        return;
    }
    std::snprintf(
        text, text_size, "PEAK CH%u  %.2f %%FS | %.1f dBFS",
        channel, percent_full_scale, audioPeakDbfs(peak)
    );
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
    if (index == 10U && !snapshot.storage.card_present) return "N/A";
    if (index == 10U && storageFormatRequired(snapshot.storage)) return "FORMAT";
    switch (states[index]) {
    case qsee::DeviceHealth::Ready: return "OK";
    case qsee::DeviceHealth::Degraded: return "WARN";
    case qsee::DeviceHealth::Missing: return "MISS";
    case qsee::DeviceHealth::Unknown:
    default: return "UNK";
    }
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

}

namespace qsee {

Navigation *Navigation::_qr_owner = nullptr;

bool Navigation::begin()
{
    _screen = std::make_unique<AppScreen>(AppScreen::active());
    if (_screen == nullptr || !_screen->isValid()) return false;
    _adaptive_command_bar = std::make_unique<AdaptiveCommandBar>();
    const esp_err_t command_bar_result = _adaptive_command_bar->begin();
    if (command_bar_result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "Adaptive-interface timeout uses 10 s default (%s)",
            esp_err_to_name(command_bar_result)
        );
    }
    _log_snapshot_buffer = static_cast<char *>(
        heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (_log_snapshot_buffer == nullptr) {
        _log_snapshot_buffer = static_cast<char *>(heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_8BIT));
    }
    if (_log_snapshot_buffer != nullptr) _log_snapshot_buffer[0] = '\0';
    ESP_LOGI("qsee_navigation", "Log snapshot buffer: %s",
             _log_snapshot_buffer != nullptr ? "ready" : "unavailable");

    _background_asset_attached = false;
    applySpotlightBackground(_screen->getNativeHandle());
    tryAttachBackgroundAsset();
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
    _welcome_ip = makeLabel(
        _welcome.get(),
        "IP ADDRESS  |  OFFLINE",
        kRowWidth,
        13,
        kMuted,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        0,
        0
    );
    makeMetric(_ram, _welcome.get(), "RAM", 34, kTeal);
    makeMetric(_sd, _welcome.get(), "SD STORAGE", 74, kBlue);
    makeMetric(_cpu0, _welcome.get(), "CPU CORE 0", 114, kTeal);
    makeMetric(_cpu1, _welcome.get(), "CPU CORE 1", 154, kBlue);
    makeMetric(_psram, _welcome.get(), "PSRAM", 198, kTeal);
    makeMetric(_flash, _welcome.get(), "FLASH APP", 238, kBlue);

    _welcome_devices.resize(4);
    makeDeviceLine(
        _welcome_devices[0], _welcome.get(), LV_SYMBOL_REFRESH, "FUSION", 280, kTeal
    );
    makeDeviceLine(
        _welcome_devices[1], _welcome.get(), LV_SYMBOL_WIFI, "WIFI", 314, kBlue
    );
    makeDeviceLine(
        _welcome_devices[2], _welcome.get(), LV_SYMBOL_BLUETOOTH, "BLE", 348, kBlue
    );
    makeDeviceLine(
        _welcome_devices[3], _welcome.get(), LV_SYMBOL_GPS, "GPS", 382, kTeal
    );

    _sd_action = std::make_unique<AppButton>(_welcome.get(), [this]() { show(Screen::Hardware); });
    _sd_action->setText("");
    _sd_action->setWidth(kRowWidth);
    _sd_action->setHeight(38);
    lv_obj_set_style_bg_opa(_sd_action->getNativeHandle(), LV_OPA_TRANSP, LV_PART_MAIN);
    _sd_action->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 74});
    lv_obj_set_style_translate_x(_sd_action->getNativeHandle(), 0, kPressedSelector);
    lv_obj_set_style_translate_y(_sd_action->getNativeHandle(), 0, kPressedSelector);

    // Icon-only actions keep the six destinations legible inside the circular bezel.
    _open_logs = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Logs); });
    styleArcButton(
        *_open_logs, 54, 56, 330, kBlue, LV_SYMBOL_LIST,
        _adaptive_command_bar.get()
    );
    _open_audio = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Audio); });
    styleArcButton(
        *_open_audio, 54, 114, 344, kBlue, LV_SYMBOL_AUDIO,
        _adaptive_command_bar.get()
    );
    _open_imu = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Imu); });
    styleArcButton(
        *_open_imu, 54, 172, 360, kTeal, LV_SYMBOL_GPS,
        _adaptive_command_bar.get()
    );
    _open_hardware = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Hardware); });
    styleArcButton(
        *_open_hardware, 54, 230, 360, kTeal, LV_SYMBOL_DRIVE,
        _adaptive_command_bar.get()
    );
    _open_about = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::About); });
    styleArcButton(
        *_open_about, 54, 288, 344, kBlue, "i",
        _adaptive_command_bar.get()
    );
    _open_settings = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Settings); });
    styleArcButton(
        *_open_settings, 54, 346, 330, kBlue, LV_SYMBOL_SETTINGS,
        _adaptive_command_bar.get()
    );

    _logs = std::make_unique<LvContainer>(_screen.get());
    _logs->setStyleAttribute(StyleSize::RECT(320, 374));
    _logs->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 82});
    transparentPanel(*_logs, false);
    _logs_live = makeLabel(_logs.get(), "LIVE", 46, 10, kTeal,
                           STYLE_ALIGN_TYPE_TOP_LEFT, 10, 0);
    _log_box = std::make_unique<AppRichText>(_logs.get());
    _log_box->setWidth(300);
    _log_box->setHeight(280);
    _log_box->setFont(StyleFont::SIZE(8));
    _log_box->setTextColor(StyleColor::COLOR(0xEAF1F6));
    _log_box->setBackgroundColor(StyleColor::COLOR(0x12263A));
    _log_box->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 18});
    lv_obj_set_scrollbar_mode(_log_box->getNativeHandle(), LV_SCROLLBAR_MODE_OFF);

    _logs_pause = std::make_unique<AppButton>(_logs.get(), [this]() {
        _logs_paused = !_logs_paused;
        setArcButtonIcon(*_logs_pause, _logs_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    });
    // Equal icon-only commands follow the lower circle with the home-screen treatment.
    styleArcButton(
        *_logs_pause, 56, 72, 278, kTeal, LV_SYMBOL_PAUSE,
        _adaptive_command_bar.get()
    );
    _logs_clear = std::make_unique<AppButton>(_logs.get(), [this]() {
        qsee_logger_clear();
        _log_box->setText("", false);
    });
    styleArcButton(
        *_logs_clear, 56, 192, 278, kBlue, LV_SYMBOL_TRASH,
        _adaptive_command_bar.get()
    );

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
        makeLabel(_audio.get(), "CHANNEL 0 PEAK  |  -60..0 dBFS", kRowWidth, 12, kMuted,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 34)
    );
    _audio_mic0 = makeDiagnosticBar(_audio.get(), 54, kTeal, 0x08745F);
    lv_obj_add_event_cb(
        _audio_mic0->getNativeHandle(),
        audioMeterDrawEvent,
        LV_EVENT_DRAW_MAIN_END,
        _audio_meter_visual[0]
    );
    _content_labels.push_back(
        makeLabel(_audio.get(), "CHANNEL 1 PEAK  |  -60..0 dBFS", kRowWidth, 12, kMuted,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 78)
    );
    _audio_mic1 = makeDiagnosticBar(_audio.get(), 98, kBlue, 0x2A5E9B);
    lv_obj_add_event_cb(
        _audio_mic1->getNativeHandle(),
        audioMeterDrawEvent,
        LV_EVENT_DRAW_MAIN_END,
        _audio_meter_visual[1]
    );
    _audio_channels = makeLabel(
        _audio.get(),
        "FORMAT     SIGNED PCM / 16-bit\n"
        "RATE       -- kHz\n"
        "CHANNELS   --\n"
        "PEAK CH0   -- %FS | -- dBFS\n"
        "PEAK CH1   -- %FS | -- dBFS",
        kRowWidth, 12, kNavy, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 124
    );
    _content_labels.push_back(
        makeLabel(_audio.get(), "dBFS peak  |  1 s hold  |  canonical snapshot", kRowWidth, 11,
                  kMuted, STYLE_ALIGN_TYPE_TOP_LEFT, 8, 218)
    );
    _audio_fft_action = std::make_unique<AppButton>(
        _screen.get(), [this]() { show(Screen::AudioFft); }
    );
    _audio_fft_action->setText("SHOW FFT");
    styleBottomArcCommand(
        *_audio_fft_action, 181, 392, kTeal, _adaptive_command_bar.get()
    );

    _audio_fft = std::make_unique<LvContainer>(_surface.get());
    _audio_fft->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _audio_fft->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_audio_fft, false);
    _audio_fft_status = makeLabel(
        _audio_fft.get(), "SPECTRUM --", kRowWidth, 14, kMuted,
        STYLE_ALIGN_TYPE_TOP_MID, 0, 6
    );
    _content_labels.push_back(
        makeLabel(_audio_fft.get(), "16 SELECTED DFT BINS  |  0 TO 11.0 kHz", kRowWidth, 11,
                  kMuted, STYLE_ALIGN_TYPE_TOP_MID, 0, 30)
    );
    _content_labels.push_back(
        makeLabel(_audio_fft.get(), "MIC 1", kRowWidth, 12, kBlue,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 50)
    );
    _content_labels.push_back(
        makeLabel(_audio_fft.get(), "MIC 2", kRowWidth, 12, kWarning,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 112)
    );
    _audio_fft_bands = lv_obj_create(_audio_fft->getNativeHandle());
    if (_audio_fft_bands != nullptr) {
        lv_obj_remove_style_all(_audio_fft_bands);
        lv_obj_set_pos(_audio_fft_bands, 18, 68);
        lv_obj_set_size(_audio_fft_bands, 236, 98);
        lv_obj_clear_flag(_audio_fft_bands, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_audio_fft_bands, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            _audio_fft_bands,
            audioSpectrumDrawEvent,
            LV_EVENT_DRAW_MAIN,
            this
        );
    }
    _content_labels.push_back(
        makeLabel(_audio_fft.get(), "Live selected-bin spectrum  |  snapshot-driven", kRowWidth, 11,
                  kMuted, STYLE_ALIGN_TYPE_TOP_MID, 0, 188)
    );

    _imu = std::make_unique<LvContainer>(_surface.get());
    _imu->setStyleAttribute(StyleSize::RECT(kWidth, 232));
    _imu->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
    transparentPanel(*_imu, false);
    _imu_status = makeLabel(_imu.get(), "QMI8658 --", 122, 12, kMuted,
                            STYLE_ALIGN_TYPE_TOP_LEFT, 8, 2);
    _imu_calibration = makeLabel(_imu.get(), "CALIB -- | 0", 132, 10, kMuted,
                                 STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 3);
    _content_labels.push_back(
        makeLabel(_imu.get(), "ACCEL  m/s2", 126, 11, kTeal,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 8, 27)
    );
    _content_labels.push_back(
        makeLabel(_imu.get(), "GYRO  dps", 116, 11, kBlue,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 148, 27)
    );

    static constexpr lv_point_precise_t kImuXAxis[] = {
        {0, 4}, {54, 4}, {47, 0}, {54, 4}, {47, 8},
    };
    static constexpr lv_point_precise_t kImuYAxis[] = {
        {4, 56}, {4, 2}, {0, 9}, {4, 2}, {8, 9},
    };
    static constexpr lv_point_precise_t kImuZAxis[] = {
        {39, 0}, {2, 37}, {3, 27}, {2, 37}, {12, 36},
    };
    const auto make_imu_axis = [this](
        const lv_point_precise_t *points,
        uint32_t point_count,
        int x,
        int y,
        int width,
        int height,
        uint32_t color
    ) {
        lv_obj_t *axis = lv_line_create(_imu->getNativeHandle());
        if (axis == nullptr) return;
        lv_obj_remove_style_all(axis);
        lv_obj_set_pos(axis, x, y);
        lv_obj_set_size(axis, width, height);
        lv_line_set_points(axis, points, point_count);
        lv_obj_set_style_line_width(axis, 3, LV_PART_MAIN);
        lv_obj_set_style_line_color(axis, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(axis, true, LV_PART_MAIN);
        lv_obj_clear_flag(axis, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(axis, LV_OBJ_FLAG_SCROLLABLE);
    };
    make_imu_axis(kImuXAxis, 5, 66, 103, 55, 9, kTeal);
    make_imu_axis(kImuYAxis, 5, 62, 51, 9, 57, kBlue);
    make_imu_axis(kImuZAxis, 5, 27, 107, 40, 40, kWarning);

    _imu_accel_x = makeLabel(_imu.get(), "X --", 68, 11, kTeal,
                             STYLE_ALIGN_TYPE_TOP_LEFT, 76, 87);
    _imu_accel_y = makeLabel(_imu.get(), "Y --", 68, 11, kBlue,
                             STYLE_ALIGN_TYPE_TOP_LEFT, 73, 51);
    _imu_accel_z = makeLabel(_imu.get(), "Z --", 76, 11, kWarning,
                             STYLE_ALIGN_TYPE_TOP_LEFT, 8, 151);

    lv_obj_t *imu_divider = lv_obj_create(_imu->getNativeHandle());
    if (imu_divider != nullptr) {
        lv_obj_remove_style_all(imu_divider);
        lv_obj_set_pos(imu_divider, 136, 31);
        lv_obj_set_size(imu_divider, 1, 151);
        lv_obj_set_style_bg_color(imu_divider, lv_color_hex(kTrack), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(imu_divider, LV_OPA_80, LV_PART_MAIN);
        lv_obj_clear_flag(imu_divider, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(imu_divider, LV_OBJ_FLAG_SCROLLABLE);
    }

    static constexpr int kGyroArcAngles[3][2] = {
        {205, 315},
        {325, 75},
        {85, 195},
    };
    static constexpr uint32_t kAxisColors[3] = {kTeal, kBlue, kWarning};
    for (size_t axis = 0; axis < 3U; ++axis) {
        _imu_gyro_arcs[axis] = lv_arc_create(_imu->getNativeHandle());
        if (_imu_gyro_arcs[axis] == nullptr) continue;
        lv_obj_remove_style_all(_imu_gyro_arcs[axis]);
        lv_obj_set_pos(_imu_gyro_arcs[axis], 169, 48);
        lv_obj_set_size(_imu_gyro_arcs[axis], 74, 74);
        lv_arc_set_bg_angles(
            _imu_gyro_arcs[axis],
            kGyroArcAngles[axis][0],
            kGyroArcAngles[axis][1]
        );
        lv_obj_set_style_arc_width(_imu_gyro_arcs[axis], 5, LV_PART_MAIN);
        lv_obj_set_style_arc_color(
            _imu_gyro_arcs[axis],
            lv_color_hex(kAxisColors[axis]),
            LV_PART_MAIN
        );
        lv_obj_set_style_arc_rounded(_imu_gyro_arcs[axis], true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(_imu_gyro_arcs[axis], LV_OPA_30, LV_PART_MAIN);
        lv_obj_clear_flag(_imu_gyro_arcs[axis], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_imu_gyro_arcs[axis], LV_OBJ_FLAG_SCROLLABLE);
    }
    _content_labels.push_back(
        makeLabel(_imu.get(), LV_SYMBOL_REFRESH, 28, 18, kMuted,
                  STYLE_ALIGN_TYPE_TOP_LEFT, 192, 74)
    );
    _imu_gyro_x = makeLabel(_imu.get(), "X --", 112, 11, kTeal,
                            STYLE_ALIGN_TYPE_TOP_LEFT, 150, 130);
    _imu_gyro_y = makeLabel(_imu.get(), "Y --", 112, 11, kBlue,
                            STYLE_ALIGN_TYPE_TOP_LEFT, 150, 148);
    _imu_gyro_z = makeLabel(_imu.get(), "Z --", 112, 11, kWarning,
                            STYLE_ALIGN_TYPE_TOP_LEFT, 150, 166);

    lv_obj_t *thermometer_stem = lv_obj_create(_imu->getNativeHandle());
    if (thermometer_stem != nullptr) {
        lv_obj_remove_style_all(thermometer_stem);
        lv_obj_set_pos(thermometer_stem, 18, 198);
        lv_obj_set_size(thermometer_stem, 6, 19);
        lv_obj_set_style_radius(thermometer_stem, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(thermometer_stem, lv_color_hex(kBlue), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(thermometer_stem, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(thermometer_stem, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(thermometer_stem, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t *thermometer_bulb = lv_obj_create(_imu->getNativeHandle());
    if (thermometer_bulb != nullptr) {
        lv_obj_remove_style_all(thermometer_bulb);
        lv_obj_set_pos(thermometer_bulb, 14, 212);
        lv_obj_set_size(thermometer_bulb, 14, 14);
        lv_obj_set_style_radius(thermometer_bulb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(thermometer_bulb, lv_color_hex(kBlue), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(thermometer_bulb, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(thermometer_bulb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(thermometer_bulb, LV_OBJ_FLAG_SCROLLABLE);
    }
    _imu_temperature = makeLabel(_imu.get(), "-- C", 92, 15, kBlue,
                                 STYLE_ALIGN_TYPE_TOP_LEFT, 36, 201);

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

    buildSettingsUi();

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

    // Consume the first touch while AMOLED output is off.
    _display_wake_guard = std::make_unique<LvContainer>(_screen.get());
    lv_obj_t *wake_guard = _display_wake_guard->getNativeHandle();
    lv_obj_remove_style_all(wake_guard);
    lv_obj_set_size(wake_guard, 466, 466);
    lv_obj_center(wake_guard);
    lv_obj_add_flag(wake_guard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wake_guard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        wake_guard,
        displayWakeGuardEvent,
        LV_EVENT_PRESSED,
        this
    );
    setVisible(*_display_wake_guard, false);

    _refresh_timer = std::make_unique<AppTimer>(kUiTickMs, [this]() { onRefreshTick(); });
    show(Screen::Welcome);
    return true;
}

// Bridge shell-owned chrome into the controller already used by route commands.
void Navigation::registerAdaptiveVisual(lv_obj_t *visual)
{
    if (_adaptive_command_bar == nullptr || visual == nullptr) {
        return;
    }
    _adaptive_command_bar->registerVisual(visual);
    updateUiMotionMonitor(qsee_power_display_input_guard_required());
}

// Build one spacious, scrollable Settings page for the circular viewport.
void Navigation::buildSettingsUi()
{
    const auto make_page = [this]() {
        auto page = std::make_unique<LvContainer>(_surface.get());
        page->setStyleAttribute(StyleSize::RECT(kWidth, 232));
        page->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
        transparentPanel(*page, true);
        return page;
    };

    _settings = make_page();
    lv_obj_t *settings_page = _settings->getNativeHandle();
    lv_obj_set_scrollbar_mode(settings_page, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_bottom(settings_page, 28, LV_PART_MAIN);

    const auto add_text =
        [this](const char *text, int y, int size, uint32_t color) {
            auto label = makeLabel(
                _settings.get(),
                text,
                kRowWidth,
                size,
                color,
                STYLE_ALIGN_TYPE_TOP_LEFT,
                8,
                y
            );
            lv_label_set_long_mode(
                label->getNativeHandle(),
                LV_LABEL_LONG_MODE_WRAP
            );
            _content_labels.push_back(std::move(label));
        };
    const auto add_section =
        [this, &add_text](const char *title, const char *description, int y) {
            if (y > 0) {
                lv_obj_t *divider =
                    lv_obj_create(_settings->getNativeHandle());
                lv_obj_remove_style_all(divider);
                lv_obj_set_size(divider, kRowWidth, 1);
                lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, y - 14);
                lv_obj_set_style_bg_color(
                    divider,
                    lv_color_hex(kTrack),
                    LV_PART_MAIN
                );
                lv_obj_set_style_bg_opa(divider, LV_OPA_80, LV_PART_MAIN);
                lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
            }
            add_text(title, y, 15, kTeal);
            add_text(description, y + 22, 10, kMuted);
        };
    const auto make_dropdown =
        [this](int y, const char *options, lv_event_cb_t callback) {
            lv_obj_t *dropdown =
                lv_dropdown_create(_settings->getNativeHandle());
            if (dropdown == nullptr) {
                return static_cast<lv_obj_t *>(nullptr);
            }
            lv_dropdown_set_options(dropdown, options);
            lv_obj_set_size(dropdown, kRowWidth, 40);
            lv_obj_align(dropdown, LV_ALIGN_TOP_MID, 0, y);
            lv_obj_set_style_bg_color(
                dropdown,
                lv_color_hex(0x142532),
                LV_PART_MAIN
            );
            lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(
                dropdown,
                lv_color_hex(kNavy),
                LV_PART_MAIN
            );
            lv_obj_set_style_text_font(
                dropdown,
                &lv_font_montserrat_12,
                LV_PART_MAIN
            );
            lv_obj_set_style_border_color(
                dropdown,
                lv_color_hex(0x3A6C83),
                LV_PART_MAIN
            );
            lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(dropdown, 10, LV_PART_MAIN);
            lv_obj_set_style_pad_left(dropdown, 12, LV_PART_MAIN);
            lv_obj_set_style_pad_right(dropdown, 12, LV_PART_MAIN);
            lv_obj_set_style_translate_x(dropdown, 0, kPressedSelector);
            lv_obj_set_style_translate_y(dropdown, 0, kPressedSelector);
            lv_obj_t *list = lv_dropdown_get_list(dropdown);
            if (list != nullptr) {
                lv_obj_set_style_bg_color(
                    list,
                    lv_color_hex(0x0D1A24),
                    LV_PART_MAIN
                );
                lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_text_color(
                    list,
                    lv_color_hex(kNavy),
                    LV_PART_MAIN
                );
                lv_obj_set_style_text_font(
                    list,
                    &lv_font_montserrat_12,
                    LV_PART_MAIN
                );
                lv_obj_set_style_border_color(
                    list,
                    lv_color_hex(kBlue),
                    LV_PART_MAIN
                );
                lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
                lv_obj_set_style_radius(list, 10, LV_PART_MAIN);
                lv_obj_set_style_bg_color(
                    list,
                    lv_color_hex(kTeal),
                    LV_PART_SELECTED
                );
                lv_obj_set_style_bg_opa(
                    list,
                    LV_OPA_70,
                    LV_PART_SELECTED
                );
            }
            lv_obj_add_event_cb(
                dropdown,
                callback,
                LV_EVENT_VALUE_CHANGED,
                this
            );
            return dropdown;
        };
    const auto make_switch = [this](int x, int y) {
        lv_obj_t *toggle = lv_switch_create(_settings->getNativeHandle());
        if (toggle == nullptr) {
            return static_cast<lv_obj_t *>(nullptr);
        }
        lv_obj_set_size(toggle, 52, 28);
        lv_obj_align(toggle, LV_ALIGN_TOP_RIGHT, x, y);
        lv_obj_set_style_bg_color(
            toggle,
            lv_color_hex(kTrack),
            LV_PART_MAIN
        );
        lv_obj_set_style_bg_color(
            toggle,
            lv_color_hex(kTeal),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_CHECKED)
        );
        lv_obj_set_style_bg_color(
            toggle,
            lv_color_hex(kWhite),
            LV_PART_KNOB
        );
        lv_obj_set_style_translate_x(toggle, 0, kPressedSelector);
        lv_obj_set_style_translate_y(toggle, 0, kPressedSelector);
        lv_obj_add_event_cb(
            toggle,
            settingsControlChangedEvent,
            LV_EVENT_VALUE_CHANGED,
            this
        );
        return toggle;
    };
    const auto make_value_row =
        [this](const char *name, int y) {
            auto row_name = makeLabel(
                _settings.get(),
                name,
                92,
                10,
                kMuted,
                STYLE_ALIGN_TYPE_TOP_LEFT,
                8,
                y
            );
            auto row_value = makeLabel(
                _settings.get(),
                "--",
                136,
                11,
                kNavy,
                STYLE_ALIGN_TYPE_TOP_RIGHT,
                -8,
                y - 1
            );
            lv_obj_set_style_text_align(
                row_value->getNativeHandle(),
                LV_TEXT_ALIGN_RIGHT,
                LV_PART_MAIN
            );
            lv_label_set_long_mode(
                row_value->getNativeHandle(),
                LV_LABEL_LONG_MODE_DOTS
            );
            _content_labels.push_back(std::move(row_name));
            return row_value;
        };

    add_section(
        "DIAGNOSTICS",
        "Sampling cadence and motion filtering",
        0
    );
    _settings_period = makeLabel(
        _settings.get(),
        "Sampling interval",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        50
    );
    _settings_period_dropdown = make_dropdown(
        74,
        kDiagnosticPeriodLabels,
        settingsControlChangedEvent
    );
    _settings_smoothing = makeLabel(
        _settings.get(),
        "IMU input smoothing",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        130
    );
    _settings_smoothing_dropdown = make_dropdown(
        154,
        kImuSmoothingLabels,
        settingsControlChangedEvent
    );
    add_section(
        "STORAGE",
        "Card state and persistent log mirroring",
        224
    );
    _settings_storage = make_value_row("SD card", 274);
    _settings_storage_mirror = make_value_row("Log destination", 300);
    add_text("Mirror logs to SD", 336, 12, kNavy);
    _settings_sd_mirror_switch = make_switch(-8, 330);
    add_text(
        "Off keeps serial and the in-memory log available.",
        368,
        10,
        kMuted
    );

    add_section(
        "WI-FI",
        "Connection, saved credentials and nearby networks",
        420
    );
    _settings_wifi = make_value_row("Radio", 470);
    _settings_wifi_ssid = make_value_row("Network", 496);
    _settings_wifi_ip = make_value_row("IP address", 522);
    _settings_wifi_credentials = make_value_row("Credentials", 548);
    add_text("Network action", 584, 12, kNavy);
    _settings_wifi_action_dropdown = make_dropdown(
        608,
        kWifiActionLabels,
        settingsControlChangedEvent
    );
    add_text("Available networks", 664, 12, kNavy);
    _settings_wifi_networks_dropdown = make_dropdown(
        688,
        "Scan to find networks",
        settingsControlChangedEvent
    );

    add_section(
        "BACKGROUND SERVICES",
        "Optional network work that can be stopped",
        760
    );
    _settings_http = make_value_row("HTTP API", 810);
    _settings_http_port = make_value_row("Endpoint", 836);
    add_text("Enable read-only HTTP API", 872, 12, kNavy);
    _settings_http_switch = make_switch(-8, 866);
    add_text(
        "Off stops HTTP requests and its 5-second refresh.",
        904,
        10,
        kMuted
    );

    add_section(
        "BLUETOOTH",
        "Radio state and Espressif provisioning",
        952
    );
    _settings_ble = make_value_row("Radio", 1002);
    _settings_ble_device = make_value_row("Device", 1028);
    add_text("Bluetooth radio", 1064, 12, kNavy);
    _settings_ble_switch = make_switch(-8, 1058);
    add_text("Espressif pairing", 1100, 12, kNavy);
    _settings_ble_action_dropdown = make_dropdown(
        1124,
        kBleActionLabels,
        settingsControlChangedEvent
    );

    add_section(
        "GPS",
        "LC76G receiver and current fix state",
        1196
    );
    _settings_gps = make_value_row("Receiver", 1246);
    _settings_gps_satellites = make_value_row("Satellites", 1272);
    _settings_gps_precision = make_value_row("HDOP", 1298);
    add_text("GPS receiver", 1334, 12, kNavy);
    _settings_gps_switch = make_switch(-8, 1328);

    add_section(
        "DISPLAY & MOTION",
        "AMOLED output, cardinal rotation and command visibility",
        1396
    );
    _settings_brightness = makeLabel(
        _settings.get(),
        "AMOLED brightness",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        1446
    );
    _settings_brightness_dropdown = make_dropdown(
        1470,
        kBrightnessLabels,
        settingsControlChangedEvent
    );
    add_text("Automatic cardinal rotation", 1530, 12, kNavy);
    _settings_auto_rotation_switch = make_switch(-8, 1524);
    _settings_rotation = make_value_row("Physical", 1566);
    _settings_rendering_rotation = make_value_row("Rendering", 1592);
    _settings_rotation_source = make_value_row("Source", 1618);
    _settings_command_bar_timeout = makeLabel(
        _settings.get(),
        "Adaptive interface",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        1654
    );
    add_text(
        "Status and commands after touch or movement",
        1676,
        10,
        kMuted
    );
    _settings_command_bar_timeout_dropdown = make_dropdown(
        1700,
        kCommandBarTimeoutLabels,
        commandBarTimeoutChangedEvent
    );

    add_section(
        "POWER",
        "Display inactivity timers; BOOT wakes the device",
        1772
    );
    _settings_dim_timeout = makeLabel(
        _settings.get(),
        "Dim display to 50%",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        1822
    );
    _settings_dim_timeout_dropdown = make_dropdown(
        1846,
        kDimTimeoutLabels,
        settingsControlChangedEvent
    );
    _settings_panel_off_delay = makeLabel(
        _settings.get(),
        "Turn panel off after dimming",
        kRowWidth,
        12,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        1902
    );
    _settings_panel_off_dropdown = make_dropdown(
        1926,
        kPanelOffDelayLabels,
        settingsControlChangedEvent
    );
    add_text(
        "Changes are saved automatically.",
        1980,
        10,
        kMuted
    );

    _wifi_editor = std::make_unique<LvContainer>(_screen.get());
    _wifi_editor->setStyleAttribute(StyleSize::RECT(320, 374));
    _wifi_editor->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 82});
    transparentPanel(*_wifi_editor, false);
    _wifi_editor_title = makeLabel(
        _wifi_editor.get(),
        "WI-FI CREDENTIALS",
        280,
        17,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_MID,
        0,
        0
    );
    lv_obj_set_style_text_align(
        _wifi_editor_title->getNativeHandle(),
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );
    _wifi_ssid = std::make_unique<AppTextBox>(_wifi_editor.get());
    _wifi_ssid->setWidth(280);
    _wifi_ssid->setHeight(38);
    _wifi_ssid->setOneLine();
    _wifi_ssid->setPlaceholder("Network name (SSID)");
    _wifi_ssid->setFont(StyleFont::SIZE(12));
    _wifi_ssid->setTextColor(StyleColor::COLOR(kNavy));
    _wifi_ssid->setBackgroundColor(StyleColor::COLOR(kSurface));
    _wifi_ssid->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 30});
    lv_textarea_set_max_length(
        _wifi_ssid->getNativeHandle(),
        QSEE_WIFI_SSID_MAX_LEN
    );
    lv_obj_set_style_radius(_wifi_ssid->getNativeHandle(), 8, LV_PART_MAIN);
    lv_obj_add_event_cb(
        _wifi_ssid->getNativeHandle(),
        wifiFieldFocusEvent,
        LV_EVENT_FOCUSED,
        this
    );
    lv_obj_add_event_cb(
        _wifi_ssid->getNativeHandle(),
        wifiSsidChangedEvent,
        LV_EVENT_VALUE_CHANGED,
        this
    );
    _wifi_password = std::make_unique<AppTextBox>(_wifi_editor.get());
    _wifi_password->setWidth(206);
    _wifi_password->setHeight(38);
    _wifi_password->setOneLine();
    _wifi_password->setPlaceholder("Password");
    _wifi_password->setFont(StyleFont::SIZE(12));
    _wifi_password->setTextColor(StyleColor::COLOR(kNavy));
    _wifi_password->setBackgroundColor(StyleColor::COLOR(kSurface));
    _wifi_password->align({STYLE_ALIGN_TYPE_TOP_LEFT, 20, 76});
    lv_textarea_set_max_length(_wifi_password->getNativeHandle(), 64);
    lv_textarea_set_password_mode(_wifi_password->getNativeHandle(), true);
    lv_obj_set_style_radius(_wifi_password->getNativeHandle(), 8, LV_PART_MAIN);
    lv_obj_add_event_cb(
        _wifi_password->getNativeHandle(),
        wifiFieldFocusEvent,
        LV_EVENT_FOCUSED,
        this
    );
    lv_obj_add_event_cb(
        _wifi_password->getNativeHandle(),
        wifiPasswordChangedEvent,
        LV_EVENT_VALUE_CHANGED,
        this
    );
    _wifi_password_visibility = std::make_unique<AppButton>(
        _wifi_editor.get(),
        [this]() {
            _wifi_password_visible = !_wifi_password_visible;
            lv_textarea_set_password_mode(
                _wifi_password->getNativeHandle(),
                !_wifi_password_visible
            );
            _wifi_password_visibility->setText(
                _wifi_password_visible ? "HIDE" : "SHOW"
            );
        }
    );
    _wifi_password_visibility->setText("SHOW");
    buttonStyle(*_wifi_password_visibility, 66, 38, kSlate);
    _wifi_password_visibility->align(
        {STYLE_ALIGN_TYPE_TOP_RIGHT, -20, 76}
    );
    _wifi_editor_message = makeLabel(
        _wifi_editor.get(),
        "Password is masked and never logged.",
        280,
        10,
        kMuted,
        STYLE_ALIGN_TYPE_TOP_MID,
        0,
        120
    );
    lv_label_set_long_mode(
        _wifi_editor_message->getNativeHandle(),
        LV_LABEL_LONG_MODE_WRAP
    );
    lv_obj_set_style_text_align(
        _wifi_editor_message->getNativeHandle(),
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );
    _wifi_keyboard = lv_keyboard_create(_wifi_editor->getNativeHandle());
    if (_wifi_keyboard != nullptr) {
        lv_obj_set_size(_wifi_keyboard, 280, 146);
        lv_obj_align(_wifi_keyboard, LV_ALIGN_TOP_MID, 0, 152);
        lv_keyboard_set_textarea(
            _wifi_keyboard,
            _wifi_ssid->getNativeHandle()
        );
        lv_obj_set_style_radius(_wifi_keyboard, 10, LV_PART_MAIN);
        lv_obj_set_style_translate_x(_wifi_keyboard, 0, kPressedSelector);
        lv_obj_set_style_translate_y(_wifi_keyboard, 0, kPressedSelector);
    }
    _wifi_editor_cancel = std::make_unique<AppButton>(
        _wifi_editor.get(), [this]() { show(Screen::Settings); }
    );
    _wifi_editor_cancel->setText("CANCEL");
    styleBottomArcCommand(
        *_wifi_editor_cancel, 49, 310, kSlate, _adaptive_command_bar.get()
    );
    _wifi_editor_save = std::make_unique<AppButton>(
        _wifi_editor.get(), [this]() { saveWifiCredentials(); }
    );
    _wifi_editor_save->setText("SAVE");
    styleBottomArcCommand(
        *_wifi_editor_save, 167, 310, kTeal, _adaptive_command_bar.get()
    );

    _pairing = std::make_unique<LvContainer>(_screen.get());
    _pairing->setStyleAttribute(StyleSize::RECT(320, 374));
    _pairing->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 82});
    transparentPanel(*_pairing, false);
    _pairing_title = makeLabel(
        _pairing.get(),
        "WI-FI PAIRING",
        280,
        17,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_MID,
        0,
        0
    );
    lv_obj_set_style_text_align(
        _pairing_title->getNativeHandle(),
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );
    _pairing_message = makeLabel(
        _pairing.get(),
        "",
        280,
        12,
        kMuted,
        STYLE_ALIGN_TYPE_TOP_MID,
        0,
        46
    );
    lv_label_set_long_mode(
        _pairing_message->getNativeHandle(),
        LV_LABEL_LONG_MODE_WRAP
    );
    lv_obj_set_style_text_align(
        _pairing_message->getNativeHandle(),
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN
    );
    _pairing_qr_canvas = lv_canvas_create(_pairing->getNativeHandle());
    if (_pairing_qr_canvas != nullptr) {
        lv_obj_set_size(
            _pairing_qr_canvas,
            kQrCanvasSize,
            kQrCanvasSize
        );
        lv_obj_align(_pairing_qr_canvas, LV_ALIGN_TOP_MID, 0, 42);
        lv_obj_add_flag(_pairing_qr_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    _pairing_secondary = std::make_unique<AppButton>(
        _pairing.get(), [this]() { pairingSecondaryAction(); }
    );
    _pairing_secondary->setText("BACK");
    styleBottomArcCommand(
        *_pairing_secondary, 49, 310, kSlate, _adaptive_command_bar.get()
    );
    _pairing_primary = std::make_unique<AppButton>(
        _pairing.get(), [this]() { pairingPrimaryAction(); }
    );
    _pairing_primary->setText("NEXT");
    styleBottomArcCommand(
        *_pairing_primary, 167, 310, kTeal, _adaptive_command_bar.get()
    );
}

void Navigation::setVisible(LvObject &object, bool visible)
{
    object.setStyleAttribute(StyleFlag::STYLE_FLAG_HIDDEN, !visible);
}

// Route screens and stop provisioning only after leaving its full flow.
void Navigation::show(Screen screen)
{
    const Screen previous = _current;
    if (previous == Screen::WifiCredentials &&
        screen != Screen::WifiCredentials) {
        clearWifiPassword();
    }
    if (isPairingScreen(previous) && !isPairingScreen(screen)) {
        (void)qsee_provisioning_stop();
    }

    const char *route = "unknown";
    const char *title = "QSEE.AI";
    const char *subtitle = "";
    switch (screen) {
    case Screen::Welcome:
        route = "welcome";
        title = "WELCOME";
        subtitle = "QSee device console";
        break;
    case Screen::Logs:
        route = "logs";
        title = "LIVE LOGS";
        subtitle = "RAM ring, newest lines";
        break;
    case Screen::Hardware:
        route = "hardware";
        title = "HARDWARE";
        subtitle = "Component inventory";
        break;
    case Screen::Audio:
        route = "audio";
        title = "AUDIO SCOPE";
        subtitle = "ES7210 microphone diagnostics";
        break;
    case Screen::AudioFft:
        route = "audio-fft";
        title = "FFT SCOPE";
        subtitle = "ES7210 dual microphone spectrum";
        break;
    case Screen::Imu:
        route = "imu";
        title = "IMU LEVEL";
        subtitle = "QMI8658 orientation and calibration";
        break;
    case Screen::About:
        route = "about";
        title = "ABOUT";
        subtitle = "About this device";
        break;
    case Screen::Settings:
        route = "settings";
        title = "SETTINGS";
        subtitle = "All controls - swipe to scroll";
        break;
    case Screen::WifiCredentials:
        route = "wifi-credentials";
        break;
    case Screen::PairIntro:
        route = "pair-intro";
        break;
    case Screen::PairBluetooth:
        route = "pair-bluetooth";
        break;
    case Screen::PairApp:
        route = "pair-app";
        break;
    case Screen::PairQr:
        route = "pair-qr";
        break;
    case Screen::AppStoreQr:
        route = "pair-app-store";
        break;
    case Screen::PairResult:
        route = "pair-result";
        break;
    }
    ESP_LOGI("qsee_navigation", "Opening route: %s", route);
    if (_dialog != nullptr) {
        closeDialog();
    }
    _current = screen;
    const bool diagnostic =
        screen == Screen::Audio || screen == Screen::AudioFft || screen == Screen::Imu;
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
    const bool full_height =
        screen == Screen::Logs ||
        screen == Screen::WifiCredentials ||
        isPairingScreen(screen);
    setVisible(*_title_bar, !full_height);
    setVisible(*_welcome, screen == Screen::Welcome);
    setVisible(*_logs, screen == Screen::Logs);
    setVisible(*_hardware, screen == Screen::Hardware);
    setVisible(*_audio, screen == Screen::Audio);
    setVisible(*_audio_fft_action, screen == Screen::Audio);
    setVisible(*_audio_fft, screen == Screen::AudioFft);
    setVisible(*_imu, screen == Screen::Imu);
    setVisible(*_settings, screen == Screen::Settings);
    setVisible(*_wifi_editor, screen == Screen::WifiCredentials);
    setVisible(*_pairing, isPairingScreen(screen));
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
    _page_title->setText(title);
    _page_subtitle->setText(subtitle);
    const bool command_bar_active = hasAdaptiveCommandBar(screen);
    if (_adaptive_command_bar != nullptr) {
        _adaptive_command_bar->activate(command_bar_active);
    }
    updateUiMotionMonitor(qsee_power_display_input_guard_required());

    // Settings refresh is deferred until this nested touch callback unwinds.
    if (isSettingsScreen(screen)) {
        _refresh_tick_count = kFullRefreshTicks;
    } else {
        refresh();
        _refresh_tick_count = 0;
    }
    if (screen == Screen::Audio) {
        updateAudioMeters(true);
    } else if (screen == Screen::AudioFft) {
        updateAudioSpectrum();
    }
    if (screen == Screen::WifiCredentials) {
        prepareWifiEditor();
    } else if (isPairingScreen(screen)) {
        renderPairing();
    }
    ESP_LOGI(
        "qsee_navigation",
        "Route ready: %s (lvgl-stack-free-min=%lu B)",
        route,
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr))
    );
}

bool Navigation::back()
{
    if (_current == Screen::Welcome) return false;
    switch (_current) {
    case Screen::AudioFft:
        show(Screen::Audio);
        break;
    case Screen::WifiCredentials:
    case Screen::PairIntro:
    case Screen::PairResult:
        show(Screen::Settings);
        break;
    case Screen::PairBluetooth:
        show(Screen::PairIntro);
        break;
    case Screen::PairApp:
        show(Screen::PairBluetooth);
        break;
    case Screen::AppStoreQr:
        show(Screen::PairApp);
        break;
    case Screen::PairQr:
        show(Screen::PairApp);
        break;
    default:
        show(Screen::Welcome);
        break;
    }
    return true;
}

void Navigation::shutdown()
{
    // Stop callbacks before Brookesia releases the app screen and its widgets.
    _refresh_timer.reset();
    if (_adaptive_command_bar != nullptr) {
        _adaptive_command_bar->shutdown();
        _adaptive_command_bar.reset();
    }
    _display_wake_guard.reset();
    (void)qsee_provisioning_stop();
    clearWifiPassword();
    (void)qsee_hardware_set_diagnostic_monitor(false);
    (void)qsee_hardware_set_system_monitor(false);
    (void)qsee_hardware_set_ui_motion_monitor(false);
    _ui_motion_monitor_active = false;
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
    _audio_meter_updated_at_us = 0;
    _refresh_tick_count = 0;
    _background_asset_attached = false;
    for (size_t channel = 0; channel < 2U; ++channel) {
        _audio_meter_visual[channel][0] = 0.0F;
        _audio_meter_visual[channel][1] = 0.0F;
        _audio_peak_hold_until_us[channel] = 0;
    }
    _audio_mic0.reset();
    _audio_mic1.reset();
    _audio_fft_bands = nullptr;
    _audio_fft_status.reset();
    _audio_fft_action.reset();
    _audio_status.reset();
    _audio_channels.reset();
    _imu_status.reset();
    _imu_accel_x.reset();
    _imu_accel_y.reset();
    _imu_accel_z.reset();
    _imu_gyro_x.reset();
    _imu_gyro_y.reset();
    _imu_gyro_z.reset();
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
    _welcome_ip.reset();
    _welcome.reset();
    _logs.reset();
    _hardware.reset();
    _audio.reset();
    _audio_fft.reset();
    _imu.reset();
    for (lv_obj_t *&arc : _imu_gyro_arcs) {
        arc = nullptr;
    }
    _about_tagline.reset();
    _about_website.reset();
    _about_email.reset();
    _about_copyright.reset();
    _settings_period.reset();
    _settings_period_dropdown = nullptr;
    _settings_smoothing.reset();
    _settings_smoothing_dropdown = nullptr;
    _settings_storage.reset();
    _settings_storage_mirror.reset();
    _settings_sd_mirror_switch = nullptr;
    _settings_wifi.reset();
    _settings_wifi_ssid.reset();
    _settings_wifi_ip.reset();
    _settings_wifi_credentials.reset();
    _settings_wifi_action_dropdown = nullptr;
    _settings_wifi_networks_dropdown = nullptr;
    _settings_http.reset();
    _settings_http_port.reset();
    _settings_http_switch = nullptr;
    _settings_ble.reset();
    _settings_ble_device.reset();
    _settings_ble_switch = nullptr;
    _settings_ble_action_dropdown = nullptr;
    _settings_gps.reset();
    _settings_gps_satellites.reset();
    _settings_gps_precision.reset();
    _settings_gps_switch = nullptr;
    _settings_wifi_scan_sequence = 0xFFFFFFFFU;
    _settings_wifi_scanning = false;
    _settings_brightness.reset();
    _settings_brightness_dropdown = nullptr;
    _settings_dim_timeout.reset();
    _settings_dim_timeout_dropdown = nullptr;
    _settings_panel_off_delay.reset();
    _settings_panel_off_dropdown = nullptr;
    _settings_command_bar_timeout.reset();
    _settings_command_bar_timeout_dropdown = nullptr;
    _settings_rotation.reset();
    _settings_rendering_rotation.reset();
    _settings_rotation_source.reset();
    _settings_auto_rotation_switch = nullptr;
    _wifi_editor_save.reset();
    _wifi_editor_cancel.reset();
    _wifi_password_visibility.reset();
    _wifi_password.reset();
    _wifi_ssid.reset();
    _wifi_editor_message.reset();
    _wifi_editor_title.reset();
    _wifi_editor.reset();
    _wifi_keyboard = nullptr;
    _pairing_primary.reset();
    _pairing_secondary.reset();
    _pairing_message.reset();
    _pairing_title.reset();
    _pairing.reset();
    _pairing_qr_canvas = nullptr;
    if (_pairing_qr_buffer != nullptr) {
        heap_caps_free(_pairing_qr_buffer);
        _pairing_qr_buffer = nullptr;
    }
    if (_qr_owner == this) {
        _qr_owner = nullptr;
    }
    wipeBytes(_pairing_result, sizeof(_pairing_result));
    _settings.reset();
    _about.reset();
    if (_log_snapshot_buffer != nullptr) {
        heap_caps_free(_log_snapshot_buffer);
        _log_snapshot_buffer = nullptr;
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
    if (_welcome_ip != nullptr) {
        const WifiSnapshot &wifi = snapshot.radios.wifi;
        char ip_text[64];
        if (wifi.ip_acquired && wifi.ip_address[0] != '\0') {
            std::snprintf(
                ip_text,
                sizeof(ip_text),
                "IP ADDRESS  |  %s",
                wifi.ip_address
            );
            _welcome_ip->setText(ip_text);
            _welcome_ip->setTextColor(StyleColor::COLOR(kTeal));
        } else {
            _welcome_ip->setText(
                wifi.started
                    ? "IP ADDRESS  |  CONNECTING"
                    : "IP ADDRESS  |  OFFLINE"
            );
            _welcome_ip->setTextColor(StyleColor::COLOR(kMuted));
        }
    }
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
            snapshot.imu.calibrated ? "CLB" : "NO CLB",
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
            wifi.reconnect_pending ? "RECON." : "SEARCHING",
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
    if (snapshot.storage.health == DeviceHealth::Ready) {
        BackgroundAssetCache::instance().request();
    }
    tryAttachBackgroundAsset();
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
                "%u%%",
                static_cast<unsigned>(snapshot.processor.cores[core].utilization_percent)
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
    } else if (isSettingsScreen(_current)) {
        updateSettings(snapshot);
    } else if (_current == Screen::PairQr) {
        updatePairingState();
    }
    updateDiagnosticViews(snapshot);
}

// Replace the fallback gradient only after the worker publishes a complete image.
void Navigation::tryAttachBackgroundAsset()
{
    if (_background_asset_attached || _screen == nullptr) {
        return;
    }
    const lv_image_dsc_t *image = BackgroundAssetCache::instance().image();
    if (image == nullptr) {
        return;
    }

    lv_obj_t *screen = _screen->getNativeHandle();
    lv_obj_set_style_bg_image_src(screen, image, LV_PART_MAIN);
    lv_obj_set_style_bg_image_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_image_tiled(screen, false, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(screen, LV_STYLE_BG_GRAD, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_invalidate(screen);
    _background_asset_attached = true;
}

// Refresh meters quickly while throttling the heavier snapshot render.
void Navigation::onRefreshTick()
{
    const uint32_t touch_inactive_ms =
        lv_display_get_inactive_time(nullptr);
    qsee_power_report_display_inactive_ms(touch_inactive_ms);
    updateDisplayWakeGuard();
    const bool wake_guard_required =
        qsee_power_display_input_guard_required();
    if (_adaptive_command_bar != nullptr &&
        (_adaptive_command_bar->isActive() || wake_guard_required)) {
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        const bool moved = _adaptive_command_bar->update(
            snapshot,
            touch_inactive_ms
        );
        if (moved && wake_guard_required) {
            qsee_power_wake_display();
        }
    }
    if (_current == Screen::Audio) {
        updateAudioMeters(false);
    } else if (_current == Screen::AudioFft) {
        updateAudioSpectrum();
    }
    ++_refresh_tick_count;
    if (_refresh_tick_count >= kFullRefreshTicks) {
        _refresh_tick_count = 0;
        refresh();
    }
}

// Synchronize the shell-owned touch guard with the physical panel state.
void Navigation::updateDisplayWakeGuard()
{
    if (_display_wake_guard == nullptr) {
        return;
    }
    const bool required = qsee_power_display_input_guard_required();
    updateUiMotionMonitor(required);
    setVisible(*_display_wake_guard, required);
    if (required) {
        lv_obj_move_foreground(_display_wake_guard->getNativeHandle());
    }
}

// Keep the IMU worker alive only while adaptive chrome or a dark panel needs it.
void Navigation::updateUiMotionMonitor(bool wake_guard_required)
{
    const bool required =
        (_adaptive_command_bar != nullptr &&
         _adaptive_command_bar->isActive()) ||
        wake_guard_required;
    if (required == _ui_motion_monitor_active) {
        return;
    }
    const esp_err_t result =
        qsee_hardware_set_ui_motion_monitor(required);
    if (result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "UI motion monitor toggle failed (%s)",
            esp_err_to_name(result)
        );
        return;
    }
    _ui_motion_monitor_active = required;
}

// Wake the panel but keep this press from reaching the hidden control below.
void Navigation::displayWakeGuardEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        qsee_power_wake_display();
    }
}

// Smooth live levels and hold each professional-style peak marker briefly.
void Navigation::updateAudioMeters(bool reset)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    const int targets[2] = {
        audioPeakMeterValue(snapshot.audio.channel_peak_level[0]),
        audioPeakMeterValue(snapshot.audio.channel_peak_level[1]),
    };
    const int64_t now_us = esp_timer_get_time();
    const float elapsed_seconds = !reset && _audio_meter_updated_at_us > 0
        ? std::clamp(
            static_cast<float>(now_us - _audio_meter_updated_at_us) / 1000000.0F,
            0.0F,
            0.25F
        )
        : 0.0F;

    esp_brookesia::gui::AppProgressBar *bars[2] = {
        _audio_mic0.get(),
        _audio_mic1.get(),
    };
    for (size_t channel = 0; channel < 2U; ++channel) {
        const float target = static_cast<float>(targets[channel]);
        if (reset || _audio_meter_updated_at_us == 0) {
            _audio_meter_visual[channel][0] = target;
            _audio_meter_visual[channel][1] = target;
            _audio_peak_hold_until_us[channel] = now_us + kAudioPeakHoldUs;
        } else {
            if (target >= _audio_meter_visual[channel][0]) {
                _audio_meter_visual[channel][0] = target;
            } else {
                _audio_meter_visual[channel][0] = std::max(
                    target,
                    _audio_meter_visual[channel][0] -
                        kAudioMeterReleasePerSecond * elapsed_seconds
                );
            }

            if (target >= _audio_meter_visual[channel][1]) {
                _audio_meter_visual[channel][1] = target;
                _audio_peak_hold_until_us[channel] = now_us + kAudioPeakHoldUs;
            } else if (now_us >= _audio_peak_hold_until_us[channel]) {
                _audio_meter_visual[channel][1] = std::max(
                    target,
                    _audio_meter_visual[channel][1] -
                        kAudioPeakReleasePerSecond * elapsed_seconds
                );
            }
        }

        if (bars[channel] != nullptr) {
            bars[channel]->setValue(
                static_cast<int>(_audio_meter_visual[channel][0] + 0.5F),
                false
            );
            lv_obj_invalidate(bars[channel]->getNativeHandle());
        }
    }
    _audio_meter_updated_at_us = now_us;
}

// Copy only new backend frames so the renderer stays idle between samples.
void Navigation::updateAudioSpectrum()
{
    if (_audio_fft_bands == nullptr) {
        return;
    }
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (_audio_spectrum_sequence == snapshot.audio.spectrum_sequence) {
        return;
    }
    std::memcpy(
        _audio_spectrum_visual,
        snapshot.audio.channel_spectrum,
        sizeof(_audio_spectrum_visual)
    );
    _audio_spectrum_sequence = snapshot.audio.spectrum_sequence;
    if (_audio_fft_status != nullptr) {
        char text[96];
        std::snprintf(
            text,
            sizeof(text),
            "SPECTRUM %s  |  %u CH  |  %.2f kHz",
            health(snapshot.audio.microphone.health),
            static_cast<unsigned>(snapshot.audio.channel_count),
            static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0
        );
        _audio_fft_status->setText(text);
    }
    lv_obj_invalidate(_audio_fft_bands);
}

// Draw both microphone spectra with a fixed number of low-cost rectangles.
void Navigation::audioSpectrumDrawEvent(lv_event_t *event)
{
    if (event == nullptr) {
        return;
    }
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    auto *navigation = static_cast<Navigation *>(lv_event_get_user_data(event));
    if (object == nullptr || layer == nullptr || navigation == nullptr) {
        return;
    }

    lv_area_t area;
    lv_obj_get_coords(object, &area);
    constexpr int kRowGap = 14;
    constexpr int kBandGap = 3;
    const int width = lv_area_get_width(&area);
    const int height = lv_area_get_height(&area);
    const int row_height = (height - kRowGap) / 2;
    const int band_width =
        (width - static_cast<int>(kAudioSpectrumBandCount - 1U) * kBandGap) /
        static_cast<int>(kAudioSpectrumBandCount);
    static constexpr uint32_t colors[2][4] = {
        {0x223A87, 0x345BB4, 0x7C63D2, 0xCCA8FF},
        {0x714017, 0xBD6517, 0xF0B73E, 0xD5E969},
    };

    for (size_t channel = 0; channel < 2U; ++channel) {
        lv_area_t row = area;
        row.y1 += static_cast<int>(channel) * (row_height + kRowGap);
        row.y2 = row.y1 + row_height - 1;
        for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
            lv_area_t column = row;
            column.x1 += static_cast<int>(band) * (band_width + kBandGap);
            column.x2 = band + 1U == kAudioSpectrumBandCount
                ? row.x2
                : column.x1 + band_width - 1;

            lv_draw_rect_dsc_t background;
            lv_draw_rect_dsc_init(&background);
            background.bg_opa = LV_OPA_COVER;
            background.bg_color = lv_color_hex(0x22313D);
            background.radius = 2;
            lv_draw_rect(layer, &background, &column);

            const uint8_t intensity = navigation->_audio_spectrum_visual[channel][band];
            if (intensity == 0U) {
                continue;
            }
            const int fill_height = std::max(
                1,
                static_cast<int>((static_cast<unsigned>(intensity) * row_height + 99U) / 100U)
            );
            lv_area_t fill = column;
            fill.y1 = fill.y2 - fill_height + 1;
            lv_draw_rect_dsc_t foreground;
            lv_draw_rect_dsc_init(&foreground);
            foreground.bg_opa = LV_OPA_COVER;
            foreground.bg_color = lv_color_hex(
                colors[channel][std::min(static_cast<size_t>(3U),
                                         static_cast<size_t>(intensity / 25U))]
            );
            foreground.radius = 2;
            lv_draw_rect(layer, &foreground, &fill);
        }
    }
}

void Navigation::updateDiagnosticViews(const DeviceSnapshot &snapshot)
{
    char text[512];
    char peak0[48];
    char peak1[48];
    std::snprintf(text, sizeof(text), "MICROPHONES %s", health(snapshot.audio.microphone.health));
    _audio_status->setText(text);
    formatAudioPeak(
        peak0, sizeof(peak0), 0U, snapshot.audio.channel_peak_level[0]
    );
    formatAudioPeak(
        peak1, sizeof(peak1), 1U, snapshot.audio.channel_peak_level[1]
    );
    std::snprintf(
        text, sizeof(text),
        "FORMAT     SIGNED PCM / 16-bit\n"
        "RATE       %.2f kHz\n"
        "CHANNELS   %u\n"
        "%s\n"
        "%s",
        static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0,
        static_cast<unsigned>(snapshot.audio.channel_count),
        peak0,
        peak1
    );
    _audio_channels->setText(text);

    std::snprintf(text, sizeof(text), "QMI8658 %s", health(snapshot.imu.health));
    _imu_status->setText(text);
    std::snprintf(text, sizeof(text), "CALIB %s | %u",
                  snapshot.imu.calibrated ? "READY" : "PENDING",
                  static_cast<unsigned>(snapshot.imu.calibration_samples));
    _imu_calibration->setText(text);

    if (snapshot.imu.available) {
        std::snprintf(text, sizeof(text), "X %+.2f", snapshot.imu.acceleration_mps2[0]);
        _imu_accel_x->setText(text);
        std::snprintf(text, sizeof(text), "Y %+.2f", snapshot.imu.acceleration_mps2[1]);
        _imu_accel_y->setText(text);
        std::snprintf(text, sizeof(text), "Z %+.2f", snapshot.imu.acceleration_mps2[2]);
        _imu_accel_z->setText(text);
        std::snprintf(text, sizeof(text), "X %+.2f", snapshot.imu.angular_velocity_dps[0]);
        _imu_gyro_x->setText(text);
        std::snprintf(text, sizeof(text), "Y %+.2f", snapshot.imu.angular_velocity_dps[1]);
        _imu_gyro_y->setText(text);
        std::snprintf(text, sizeof(text), "Z %+.2f", snapshot.imu.angular_velocity_dps[2]);
        _imu_gyro_z->setText(text);
        std::snprintf(text, sizeof(text), "%.1f C", snapshot.imu.temperature_c);
        _imu_temperature->setText(text);
    } else {
        _imu_accel_x->setText("X --");
        _imu_accel_y->setText("Y --");
        _imu_accel_z->setText("Z --");
        _imu_gyro_x->setText("X --");
        _imu_gyro_y->setText("Y --");
        _imu_gyro_z->setText("Z --");
        _imu_temperature->setText("-- C");
    }

    for (size_t axis = 0; axis < 3U; ++axis) {
        if (_imu_gyro_arcs[axis] == nullptr) continue;
        // The number preserves direction; arc intensity is the compact activity cue.
        const float activity = snapshot.imu.available
            ? std::min(
                1.0F,
                std::fabs(snapshot.imu.angular_velocity_dps[axis]) /
                    kImuGyroVisualRangeDps
            )
            : 0.0F;
        const int opacity = LV_OPA_30 +
            static_cast<int>((LV_OPA_COVER - LV_OPA_30) * activity);
        lv_obj_set_style_arc_opa(
            _imu_gyro_arcs[axis],
            static_cast<lv_opa_t>(opacity),
            LV_PART_MAIN
        );
    }
}

void Navigation::showAudio()
{
    show(Screen::Audio);
}

void Navigation::showImu()
{
    show(Screen::Imu);
}

// Map each unified Settings control to the existing platform setter.
void Navigation::settingsControlChangedEvent(lv_event_t *event)
{
    if (event == nullptr ||
        lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    lv_obj_t *control = lv_event_get_target_obj(event);
    if (navigation == nullptr || control == nullptr) {
        return;
    }

    esp_err_t result = ESP_OK;
    const char *setting = "unknown";
    if (control == navigation->_settings_period_dropdown) {
        const uint32_t index = lv_dropdown_get_selected(control);
        if (index >=
            sizeof(kDiagnosticPeriodOptions) /
                sizeof(kDiagnosticPeriodOptions[0])) {
            return;
        }
        setting = "Diagnostic period";
        result = qsee_hardware_set_diagnostic_period_ms(
            kDiagnosticPeriodOptions[index]
        );
    } else if (control == navigation->_settings_smoothing_dropdown) {
        const uint32_t index = lv_dropdown_get_selected(control);
        if (index >=
            sizeof(kImuSmoothingOptions) /
                sizeof(kImuSmoothingOptions[0])) {
            return;
        }
        setting = "IMU smoothing";
        result = qsee_hardware_set_imu_smoothing_percent(
            kImuSmoothingOptions[index]
        );
    } else if (control == navigation->_settings_sd_mirror_switch) {
        setting = "SD mirror";
        result = qsee_storage_set_logger_mirror(
            lv_obj_has_state(control, LV_STATE_CHECKED)
        );
    } else if (control == navigation->_settings_http_switch) {
        setting = "HTTP API";
        result = qsee::httpServerSetEnabled(
            lv_obj_has_state(control, LV_STATE_CHECKED)
        );
    } else if (control == navigation->_settings_wifi_action_dropdown) {
        const uint32_t action = lv_dropdown_get_selected(control);
        lv_dropdown_set_selected(control, 0);
        if (action == 1U) {
            setting = "Wi-Fi radio";
            result = qsee_wifi_init();
        } else if (action == 2U) {
            setting = "Wi-Fi scan";
            result = qsee_wifi_init();
            if (result == ESP_OK) {
                result = qsee_wifi_start_scan();
            }
        } else if (action == 3U) {
            navigation->show(Screen::WifiCredentials);
            return;
        } else {
            return;
        }
    } else if (control == navigation->_settings_wifi_networks_dropdown) {
        const uint32_t selected = lv_dropdown_get_selected(control);
        WifiNetworkSnapshot network{};
        if (!copyWifiNetworkSnapshot(selected, &network)) {
            return;
        }
        lv_dropdown_set_selected(control, 0);
        navigation->show(Screen::WifiCredentials);
        if (navigation->_wifi_ssid != nullptr &&
            navigation->_wifi_password != nullptr) {
            navigation->_wifi_editor_loading = true;
            navigation->_wifi_ssid->setText(network.ssid, false);
            navigation->_wifi_password->setText("", false);
            navigation->_wifi_password->setPlaceholder(
                network.secured
                    ? "Password for selected network"
                    : "Password (optional)"
            );
            // Replacing with blank is intentional for an open scanned network.
            navigation->_wifi_password_dirty = true;
            navigation->_wifi_selected_network_open = !network.secured;
            navigation->_wifi_editor_loading = false;
        }
        if (navigation->_wifi_editor_message != nullptr) {
            navigation->_wifi_editor_message->setText(
                network.secured
                    ? "Selected network is secured. Enter its password."
                    : "Selected network is open. Save to connect."
            );
        }
        return;
    } else if (control == navigation->_settings_ble_switch) {
        setting = "Bluetooth";
        result = qsee_ble_set_enabled(
            lv_obj_has_state(control, LV_STATE_CHECKED)
        );
    } else if (control == navigation->_settings_ble_action_dropdown) {
        const uint32_t action = lv_dropdown_get_selected(control);
        lv_dropdown_set_selected(control, 0);
        if (action == 1U) {
            navigation->startPairing();
        }
        return;
    } else if (control == navigation->_settings_gps_switch) {
        setting = "GPS";
        result = qsee_gps_set_enabled(
            lv_obj_has_state(control, LV_STATE_CHECKED)
        );
    } else if (control == navigation->_settings_brightness_dropdown) {
        const uint32_t index = lv_dropdown_get_selected(control);
        if (index >=
            sizeof(kBrightnessOptions) / sizeof(kBrightnessOptions[0])) {
            return;
        }
        setting = "Brightness";
        result = qsee_display_set_brightness_percent(
            kBrightnessOptions[index]
        );
    } else if (control == navigation->_settings_auto_rotation_switch) {
        setting = "Auto-rotation";
        result = qsee_display_set_auto_rotation_enabled(
            lv_obj_has_state(control, LV_STATE_CHECKED)
        );
    } else if (control == navigation->_settings_dim_timeout_dropdown) {
        const uint32_t index = lv_dropdown_get_selected(control);
        if (index >=
            sizeof(kDimTimeoutOptions) / sizeof(kDimTimeoutOptions[0])) {
            return;
        }
        setting = "Dim timeout";
        result = qsee_power_set_dim_timeout_seconds(
            kDimTimeoutOptions[index]
        );
    } else if (control == navigation->_settings_panel_off_dropdown) {
        const uint32_t index = lv_dropdown_get_selected(control);
        if (index >= sizeof(kPanelOffDelayOptions) /
                         sizeof(kPanelOffDelayOptions[0])) {
            return;
        }
        setting = "Panel-off delay";
        result = qsee_power_set_panel_off_delay_seconds(
            kPanelOffDelayOptions[index]
        );
    } else {
        return;
    }

    if (result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "%s update failed (%s)",
            setting,
            esp_err_to_name(result)
        );
    }
    // Let the timer refresh after this nested LVGL value-change callback.
    navigation->_refresh_tick_count = kFullRefreshTicks;
}

// Apply a preset selected from the real LVGL dropdown and persist it in NVS.
void Navigation::commandBarTimeoutChangedEvent(lv_event_t *event)
{
    if (event == nullptr ||
        lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    if (navigation == nullptr || dropdown == nullptr ||
        navigation->_adaptive_command_bar == nullptr) {
        return;
    }

    const uint32_t index = lv_dropdown_get_selected(dropdown);
    if (index >= sizeof(kCommandBarTimeoutOptions) /
                     sizeof(kCommandBarTimeoutOptions[0])) {
        return;
    }
    const esp_err_t result =
        navigation->_adaptive_command_bar->setTimeoutSeconds(
            kCommandBarTimeoutOptions[index]
        );
    if (result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "Adaptive-interface timeout persistence failed (%s)",
            esp_err_to_name(result)
        );
    }
}

void Navigation::updateSettings(const DeviceSnapshot &snapshot)
{
    if (_settings_period == nullptr || _settings_smoothing == nullptr ||
        _settings_storage == nullptr || _settings_storage_mirror == nullptr ||
        _settings_wifi == nullptr || _settings_wifi_ssid == nullptr ||
        _settings_wifi_ip == nullptr || _settings_wifi_credentials == nullptr ||
        _settings_http == nullptr || _settings_http_port == nullptr ||
        _settings_ble == nullptr || _settings_ble_device == nullptr ||
        _settings_gps == nullptr || _settings_gps_satellites == nullptr ||
        _settings_gps_precision == nullptr ||
        _settings_brightness == nullptr || _settings_dim_timeout == nullptr ||
        _settings_panel_off_delay == nullptr || _settings_rotation == nullptr ||
        _settings_rendering_rotation == nullptr ||
        _settings_rotation_source == nullptr ||
        _settings_period_dropdown == nullptr ||
        _settings_smoothing_dropdown == nullptr ||
        _settings_sd_mirror_switch == nullptr ||
        _settings_http_switch == nullptr ||
        _settings_wifi_action_dropdown == nullptr ||
        _settings_wifi_networks_dropdown == nullptr ||
        _settings_ble_switch == nullptr ||
        _settings_ble_action_dropdown == nullptr ||
        _settings_gps_switch == nullptr ||
        _settings_brightness_dropdown == nullptr ||
        _settings_auto_rotation_switch == nullptr ||
        _settings_dim_timeout_dropdown == nullptr ||
        _settings_panel_off_dropdown == nullptr) {
        return;
    }
    char text[64];
    const uint32_t period_ms = qsee_hardware_get_diagnostic_period_ms();
    const uint32_t smoothing = qsee_hardware_get_imu_smoothing_percent();
    _settings_period->setText("Sampling interval");
    lv_dropdown_set_selected(
        _settings_period_dropdown,
        static_cast<uint32_t>(
            presetIndex(period_ms, kDiagnosticPeriodOptions)
        )
    );
    _settings_smoothing->setText("IMU input smoothing");
    lv_dropdown_set_selected(
        _settings_smoothing_dropdown,
        static_cast<uint32_t>(
            presetIndex(smoothing, kImuSmoothingOptions)
        )
    );

    const bool mirror = qsee_storage_get_logger_mirror_preference();
    const char *storage_state =
        snapshot.storage.health == DeviceHealth::Ready ? "READY" :
        snapshot.storage.card_present ? "NOT MOUNTED" : "ABSENT";
    _settings_storage->setText(storage_state);
    _settings_storage_mirror->setText(
        !mirror ? "MEMORY ONLY" :
        snapshot.storage.health == DeviceHealth::Ready ? "SD + MEMORY" :
        "WAITING FOR SD"
    );
    if (mirror) {
        lv_obj_add_state(_settings_sd_mirror_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_settings_sd_mirror_switch, LV_STATE_CHECKED);
    }

    const WifiSnapshot &wifi = snapshot.radios.wifi;
    const char *wifi_state =
        wifi.ip_acquired ? "ONLINE" :
        wifi.connected ? "CONNECTED" :
        wifi.started ? "STARTED" : "DEFERRED";
    _settings_wifi->setText(wifi_state);
    _settings_wifi_ssid->setText(
        wifi.ssid[0] != '\0' ? wifi.ssid : "NOT SELECTED"
    );
    _settings_wifi_ip->setText(
        wifi.ip_acquired && wifi.ip_address[0] != '\0'
            ? wifi.ip_address : "NOT ASSIGNED"
    );
    _settings_wifi_credentials->setText(
        wifi.credentials_configured ? "SAVED" : "NOT SET"
    );

    // LVGL's open popup borrows the options buffer; update it only after close.
    if (!lv_dropdown_is_open(_settings_wifi_networks_dropdown) &&
        wifi.scanning) {
        if (!_settings_wifi_scanning) {
            lv_dropdown_set_options(
                _settings_wifi_networks_dropdown,
                "Scanning..."
            );
        }
        _settings_wifi_scanning = true;
    } else if (!lv_dropdown_is_open(_settings_wifi_networks_dropdown) &&
               (_settings_wifi_scanning ||
                _settings_wifi_scan_sequence != wifi.scan_sequence)) {
        _settings_wifi_scanning = false;
        _settings_wifi_scan_sequence = wifi.scan_sequence;
        if (!wifi.scan_completed) {
            lv_dropdown_set_options(
                _settings_wifi_networks_dropdown,
                "Scan to find networks"
            );
        } else if (wifi.network_count == 0U) {
            lv_dropdown_set_options(
                _settings_wifi_networks_dropdown,
                "No networks found"
            );
        } else {
            char *options = static_cast<char *>(heap_caps_malloc(
                kWifiOptionsBufferBytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            ));
            if (options == nullptr) {
                lv_dropdown_set_options(
                    _settings_wifi_networks_dropdown,
                    "Networks unavailable - low PSRAM"
                );
            } else {
                std::snprintf(
                    options,
                    kWifiOptionsBufferBytes,
                    "Select a network..."
                );
                size_t used = std::strlen(options);
                const size_t network_count = std::min(
                    static_cast<size_t>(wifi.network_count),
                    kWifiSnapshotMaxNetworks
                );
                for (size_t index = 0; index < network_count; ++index) {
                    char ssid[sizeof(wifi.networks[index].ssid)];
                    std::snprintf(
                        ssid,
                        sizeof(ssid),
                        "%s",
                        wifi.networks[index].ssid
                    );
                    for (char &character : ssid) {
                        if (character == '\n' || character == '\r') {
                            character = ' ';
                        }
                    }
                    const int written = std::snprintf(
                        options + used,
                        kWifiOptionsBufferBytes - used,
                        "\n%s  %d dBm  %s",
                        ssid[0] != '\0' ? ssid : "<hidden>",
                        static_cast<int>(wifi.networks[index].rssi),
                        wifi.networks[index].secured ? "SECURE" : "OPEN"
                    );
                    if (written < 0 ||
                        static_cast<size_t>(written) >=
                            kWifiOptionsBufferBytes - used) {
                        break;
                    }
                    used += static_cast<size_t>(written);
                }
                lv_dropdown_set_options(
                    _settings_wifi_networks_dropdown,
                    options
                );
                heap_caps_free(options);
            }
        }
        lv_dropdown_set_selected(_settings_wifi_networks_dropdown, 0);
    }

    const HttpSnapshot &http = snapshot.http;
    _settings_http->setText(
        !http.enabled ? "OFF" :
        http.running ? "RUNNING" :
        http.last_error != ESP_OK ? "ERROR" :
        "WAITING FOR WI-FI"
    );
    if (http.enabled) {
        lv_obj_add_state(_settings_http_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_settings_http_switch, LV_STATE_CHECKED);
    }
    if (http.enabled) {
        std::snprintf(
            text,
            sizeof(text),
            "PORT %u",
            static_cast<unsigned>(http.port)
        );
        _settings_http_port->setText(text);
    } else {
        _settings_http_port->setText("DISABLED");
    }

    const BleSnapshot &ble = snapshot.radios.ble;
    const char *ble_state =
        !ble.enabled ? "OFF" :
        ble.connected ? "CONNECTED" :
        ble.provisioning ? "PROVISIONING" :
        ble.advertising ? "ADVERTISING" : "READY";
    _settings_ble->setText(ble_state);
    _settings_ble_device->setText(
        ble.device_name[0] != '\0' ? ble.device_name : "QSee"
    );
    if (ble.enabled) {
        lv_obj_add_state(_settings_ble_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_settings_ble_switch, LV_STATE_CHECKED);
    }

    const GpsSnapshot &gps = snapshot.gps;
    const char *gps_state =
        !gps.enabled ? "OFF" :
        gps.has_fix ? "FIX" :
        gps.available ? "SEARCHING" : "UNAVAILABLE";
    _settings_gps->setText(gps_state);
    std::snprintf(
        text,
        sizeof(text),
        "%u",
        static_cast<unsigned>(gps.satellites)
    );
    _settings_gps_satellites->setText(text);
    std::snprintf(
        text,
        sizeof(text),
        "%.1f",
        static_cast<double>(gps.hdop)
    );
    _settings_gps_precision->setText(text);
    if (gps.enabled) {
        lv_obj_add_state(_settings_gps_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_settings_gps_switch, LV_STATE_CHECKED);
    }

    const uint32_t brightness = qsee_display_get_brightness_percent();
    _settings_brightness->setText("AMOLED brightness");
    lv_dropdown_set_selected(
        _settings_brightness_dropdown,
        static_cast<uint32_t>(
            presetIndex(brightness, kBrightnessOptions)
        )
    );
    const OrientationSnapshot &orientation = snapshot.ui.orientation;
    const bool auto_rotation = orientation.auto_rotation_enabled;
    const char *orientation_source =
        !auto_rotation
            ? "IDLE"
            : orientation.gravity_valid
                ? "GRAVITY LOCKED"
                : snapshot.imu.health == DeviceHealth::Ready
                    ? "GYRO READY"
                    : "IMU WAITING";
    std::snprintf(text, sizeof(text), "%u deg",
                  static_cast<unsigned>(orientation.physical_orientation_degrees));
    _settings_rotation->setText(text);
    std::snprintf(text, sizeof(text), "%u deg",
                  static_cast<unsigned>(orientation.rendering_rotation_degrees));
    _settings_rendering_rotation->setText(text);
    std::snprintf(
        text,
        sizeof(text),
        "%s%s",
        orientation_source,
        orientation.rotation_pending ? " / PENDING" : ""
    );
    _settings_rotation_source->setText(text);
    if (auto_rotation) {
        lv_obj_add_state(_settings_auto_rotation_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_settings_auto_rotation_switch, LV_STATE_CHECKED);
    }

    const uint32_t dim_seconds = qsee_power_get_dim_timeout_seconds();
    const uint32_t panel_seconds = qsee_power_get_panel_off_delay_seconds();
    _settings_dim_timeout->setText("Dim display to 50%");
    lv_dropdown_set_selected(
        _settings_dim_timeout_dropdown,
        static_cast<uint32_t>(presetIndex(dim_seconds, kDimTimeoutOptions))
    );
    _settings_panel_off_delay->setText("Turn panel off after dimming");
    lv_dropdown_set_selected(
        _settings_panel_off_dropdown,
        static_cast<uint32_t>(
            presetIndex(panel_seconds, kPanelOffDelayOptions)
        )
    );
    if (_settings_command_bar_timeout_dropdown != nullptr &&
        _adaptive_command_bar != nullptr) {
        lv_dropdown_set_selected(
            _settings_command_bar_timeout_dropdown,
            static_cast<uint32_t>(
                commandBarTimeoutIndex(
                    _adaptive_command_bar->timeoutSeconds()
                )
            )
        );
    }
}

// Load the SSID but never copy a saved password into editable UI memory.
void Navigation::prepareWifiEditor()
{
    if (_wifi_ssid == nullptr || _wifi_password == nullptr ||
        _wifi_editor_message == nullptr) {
        return;
    }

    const esp_err_t wifi_result = qsee_wifi_init();
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    _wifi_editor_loading = true;
    _wifi_ssid->setText(snapshot.radios.wifi.ssid, false);
    _wifi_password->setText("", false);
    _wifi_password->setPlaceholder(
        snapshot.radios.wifi.credentials_configured
            ? "Blank keeps saved password"
            : "Password for this network"
    );
    _wifi_password_dirty = false;
    _wifi_selected_network_open = false;
    _wifi_password_visible = false;
    lv_textarea_set_password_mode(_wifi_password->getNativeHandle(), true);
    _wifi_password_visibility->setText("SHOW");
    if (_wifi_keyboard != nullptr) {
        lv_keyboard_set_textarea(
            _wifi_keyboard,
            _wifi_ssid->getNativeHandle()
        );
    }
    _wifi_editor_loading = false;

    if (wifi_result == ESP_OK) {
        _wifi_editor_message->setText(
            snapshot.radios.wifi.credentials_configured
                ? "Password is masked. Blank keeps the saved password."
                : "Enter SSID and password. Secrets are never logged."
        );
        _wifi_editor_message->setTextColor(StyleColor::COLOR(kMuted));
    } else {
        char message[96];
        std::snprintf(
            message,
            sizeof(message),
            "Wi-Fi unavailable: %s",
            esp_err_to_name(wifi_result)
        );
        _wifi_editor_message->setText(message);
        _wifi_editor_message->setTextColor(StyleColor::COLOR(kError));
    }
}

// Scrub temporary password text before resetting the editor state.
void Navigation::clearWifiPassword()
{
    if (_wifi_password == nullptr) {
        return;
    }
    _wifi_editor_loading = true;
    const size_t length = strnlen(_wifi_password->getText(), 64U);
    if (length > 0U) {
        char scrub[65] = {};
        std::memset(scrub, ' ', length);
        scrub[length] = '\0';
        _wifi_password->setText(scrub, false);
        wipeBytes(scrub, sizeof(scrub));
    }
    _wifi_password->setText("", false);
    _wifi_password_dirty = false;
    _wifi_selected_network_open = false;
    _wifi_password_visible = false;
    lv_textarea_set_password_mode(_wifi_password->getNativeHandle(), true);
    if (_wifi_password_visibility != nullptr) {
        _wifi_password_visibility->setText("SHOW");
    }
    _wifi_editor_loading = false;
}

void Navigation::wifiFieldFocusEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    lv_obj_t *field = lv_event_get_target_obj(event);
    if (navigation == nullptr || navigation->_wifi_keyboard == nullptr ||
        field == nullptr) {
        return;
    }
    lv_keyboard_set_textarea(navigation->_wifi_keyboard, field);
}

// Prevent a manually changed SSID from inheriting an open-network exception.
void Navigation::wifiSsidChangedEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    if (navigation != nullptr && !navigation->_wifi_editor_loading) {
        navigation->_wifi_selected_network_open = false;
    }
}

// Distinguish a real password edit from programmatic editor setup.
void Navigation::wifiPasswordChangedEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    if (navigation != nullptr && !navigation->_wifi_editor_loading) {
        navigation->_wifi_password_dirty = true;
    }
}

// Preserve the saved password unless the user supplied a replacement.
void Navigation::saveWifiCredentials()
{
    if (_wifi_ssid == nullptr || _wifi_password == nullptr ||
        _wifi_editor_message == nullptr) {
        return;
    }

    char ssid[QSEE_WIFI_SSID_MAX_LEN + 1U] = {};
    char password[65] = {};
    strlcpy(ssid, _wifi_ssid->getText(), sizeof(ssid));
    strlcpy(password, _wifi_password->getText(), sizeof(password));
    if (ssid[0] == '\0') {
        _wifi_editor_message->setText("SSID is required.");
        _wifi_editor_message->setTextColor(StyleColor::COLOR(kError));
        wipeBytes(password, sizeof(password));
        return;
    }

    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    const bool same_saved_profile =
        snapshot.radios.wifi.credentials_configured &&
        std::strcmp(snapshot.radios.wifi.ssid, ssid) == 0;
    const bool password_entered = password[0] != '\0';
    if ((!same_saved_profile && !password_entered &&
         !_wifi_selected_network_open) ||
        (_wifi_password_dirty && !password_entered &&
         !_wifi_selected_network_open)) {
        _wifi_editor_message->setText(
            "Enter a password for a new or changed network."
        );
        _wifi_editor_message->setTextColor(StyleColor::COLOR(kError));
        wipeBytes(password, sizeof(password));
        return;
    }

    const bool replace_password =
        password_entered || _wifi_selected_network_open;
    const esp_err_t result = qsee_wifi_update_credentials(
        ssid,
        password,
        replace_password,
        true
    );
    wipeBytes(password, sizeof(password));
    clearWifiPassword();
    if (result == ESP_OK) {
        show(Screen::Settings);
        return;
    }

    char message[112];
    std::snprintf(
        message,
        sizeof(message),
        "Save failed: %s. Password cleared.",
        esp_err_to_name(result)
    );
    _wifi_editor_message->setText(message);
    _wifi_editor_message->setTextColor(StyleColor::COLOR(kError));
}

void Navigation::startPairing()
{
    show(Screen::PairIntro);
}

void Navigation::showPairingBluetooth()
{
    show(Screen::PairBluetooth);
}

void Navigation::showPairingApp()
{
    show(Screen::PairApp);
}

// Start BLE provisioning only when the user reaches the device QR step.
void Navigation::showPairingQr()
{
    const esp_err_t wifi_result = qsee_wifi_init();
    if (wifi_result != ESP_OK) {
        char message[112];
        std::snprintf(
            message,
            sizeof(message),
            "Wi-Fi could not start: %s",
            esp_err_to_name(wifi_result)
        );
        showPairingResult(message, false);
        return;
    }
    const esp_err_t start_result = qsee_provisioning_start();
    if (start_result != ESP_OK) {
        char message[112];
        std::snprintf(
            message,
            sizeof(message),
            "BLE provisioning could not start: %s",
            esp_err_to_name(start_result)
        );
        showPairingResult(message, false);
        return;
    }
    _pairing_started_at_us = esp_timer_get_time();
    show(Screen::PairQr);
}

void Navigation::showAppStoreQr()
{
    show(Screen::AppStoreQr);
}

void Navigation::showPairingResult(const char *message, bool success)
{
    strlcpy(
        _pairing_result,
        message != nullptr ? message : "",
        sizeof(_pairing_result)
    );
    _pairing_result_success = success;
    _pairing_started_at_us = 0;
    show(Screen::PairResult);
}

void Navigation::pairingPrimaryAction()
{
    switch (_current) {
    case Screen::PairIntro:
        showPairingBluetooth();
        break;
    case Screen::PairBluetooth:
        showPairingApp();
        break;
    case Screen::PairApp:
        showPairingQr();
        break;
    case Screen::PairQr:
        if (ProvisioningService::instance().state() ==
            ProvisioningState::Failed) {
            showPairingQr();
        } else {
            showAppStoreQr();
        }
        break;
    case Screen::AppStoreQr:
        showPairingQr();
        break;
    case Screen::PairResult:
        if (_pairing_result_success) {
            show(Screen::Settings);
        } else {
            showPairingQr();
        }
        break;
    default:
        break;
    }
}

void Navigation::pairingSecondaryAction()
{
    switch (_current) {
    case Screen::PairIntro:
    case Screen::PairResult:
        show(Screen::Settings);
        break;
    case Screen::PairBluetooth:
        show(Screen::PairIntro);
        break;
    case Screen::PairApp:
        showAppStoreQr();
        break;
    case Screen::PairQr:
        show(Screen::PairApp);
        break;
    case Screen::AppStoreQr:
        show(Screen::PairApp);
        break;
    default:
        break;
    }
}

// Render the current pairing stage without changing the BLE lifecycle.
void Navigation::renderPairing()
{
    if (_pairing_title == nullptr || _pairing_message == nullptr ||
        _pairing_primary == nullptr || _pairing_secondary == nullptr) {
        return;
    }

    const bool qr_visible =
        _current == Screen::PairQr || _current == Screen::AppStoreQr;
    if (_pairing_qr_canvas != nullptr) {
        if (qr_visible) {
            lv_obj_remove_flag(_pairing_qr_canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_pairing_qr_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }
    _pairing_message->align({
        STYLE_ALIGN_TYPE_TOP_MID,
        0,
        qr_visible ? 266 : 68
    });
    _pairing_message->setTextColor(StyleColor::COLOR(kMuted));

    switch (_current) {
    case Screen::PairIntro:
        _pairing_title->setText("WI-FI PAIRING");
        _pairing_message->setText(
            "Configure Wi-Fi securely over Bluetooth.\n"
            "The Espressif app will send credentials directly to this device."
        );
        _pairing_secondary->setText("CANCEL");
        _pairing_primary->setText("START");
        break;
    case Screen::PairBluetooth:
        _pairing_title->setText("ENABLE BLUETOOTH");
        _pairing_message->setText(
            "Enable Bluetooth on your phone and keep it close to QSee."
        );
        _pairing_secondary->setText("BACK");
        _pairing_primary->setText("NEXT");
        break;
    case Screen::PairApp:
        _pairing_title->setText("ESPRESSIF APP");
        _pairing_message->setText(
            "Open ESP BLE Provisioning by Espressif.\n"
            "Install it first if it is not on the phone."
        );
        _pairing_secondary->setText("APP QR");
        _pairing_primary->setText("NEXT");
        break;
    case Screen::PairQr: {
        _pairing_title->setText("SCAN DEVICE QR");
        char message[128];
        std::snprintf(
            message,
            sizeof(message),
            "In the Espressif app choose Scan QR.\nDevice: %s",
            qsee_provisioning_service_name()
        );
        _pairing_message->setText(message);
        _pairing_secondary->setText("BACK");
        _pairing_primary->setText(
            ProvisioningService::instance().state() ==
                ProvisioningState::Failed
                ? "RETRY" : "APP QR"
        );
        if (!renderQr(ProvisioningService::instance().qrPayload())) {
            _pairing_message->setText("Device QR is unavailable. Go back and retry.");
            _pairing_message->setTextColor(StyleColor::COLOR(kError));
        }
        break;
    }
    case Screen::AppStoreQr:
        _pairing_title->setText("INSTALL ESPRESSIF");
        _pairing_message->setText(
            "Scan to open ESP BLE Provisioning on Google Play."
        );
        _pairing_secondary->setText("BACK");
        _pairing_primary->setText("DEVICE");
        if (!renderQr(kEspressifAppStoreUrl)) {
            _pairing_message->setText("App-store QR is unavailable.");
            _pairing_message->setTextColor(StyleColor::COLOR(kError));
        }
        break;
    case Screen::PairResult:
        _pairing_title->setText(
            _pairing_result_success ? "WI-FI READY" : "PAIRING FAILED"
        );
        _pairing_message->setText(_pairing_result);
        _pairing_message->setTextColor(
            StyleColor::COLOR(_pairing_result_success ? kTeal : kError)
        );
        _pairing_secondary->setText("NETWORK");
        _pairing_primary->setText(
            _pairing_result_success ? "DONE" : "RETRY"
        );
        break;
    default:
        break;
    }
}

// Generate a QR into one reusable PSRAM-backed LVGL canvas buffer.
bool Navigation::renderQr(const char *payload)
{
    if (_pairing_qr_canvas == nullptr || payload == nullptr ||
        payload[0] == '\0') {
        return false;
    }
    const size_t stride = lv_draw_buf_width_to_stride(
        kQrCanvasSize,
        LV_COLOR_FORMAT_RGB565
    );
    const size_t buffer_size = stride * kQrCanvasSize;
    if (_pairing_qr_buffer == nullptr) {
        _pairing_qr_buffer = static_cast<uint8_t *>(
            heap_caps_malloc(
                buffer_size,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            )
        );
        if (_pairing_qr_buffer == nullptr) {
            _pairing_qr_buffer = static_cast<uint8_t *>(
                heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT)
            );
        }
        if (_pairing_qr_buffer == nullptr) {
            return false;
        }
        lv_canvas_set_buffer(
            _pairing_qr_canvas,
            _pairing_qr_buffer,
            kQrCanvasSize,
            kQrCanvasSize,
            LV_COLOR_FORMAT_RGB565
        );
    }

    std::memset(_pairing_qr_buffer, 0xFF, buffer_size);
    _qr_owner = this;
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.display_func = qrDisplayCallback;
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    const esp_err_t result = esp_qrcode_generate(&config, payload);
    _qr_owner = nullptr;
    lv_obj_invalidate(_pairing_qr_canvas);
    if (result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "QR generation failed (%s)",
            esp_err_to_name(result)
        );
    }
    return result == ESP_OK;
}

// Rasterize the QR library callback into the currently owned canvas.
void Navigation::qrDisplayCallback(const uint8_t *qrcode)
{
    if (_qr_owner == nullptr || _qr_owner->_pairing_qr_canvas == nullptr ||
        qrcode == nullptr) {
        return;
    }

    const int modules = esp_qrcode_get_size(qrcode);
    constexpr int quiet_zone = 4;
    const int scale = std::max(
        1,
        kQrCanvasSize / (modules + quiet_zone * 2)
    );
    const int rendered = (modules + quiet_zone * 2) * scale;
    const int offset = (kQrCanvasSize - rendered) / 2;
    for (int y = 0; y < rendered; ++y) {
        for (int x = 0; x < rendered; ++x) {
            const int module_x = (x / scale) - quiet_zone;
            const int module_y = (y / scale) - quiet_zone;
            const bool dark =
                module_x >= 0 && module_y >= 0 &&
                module_x < modules && module_y < modules &&
                esp_qrcode_get_module(qrcode, module_x, module_y);
            lv_canvas_set_px(
                _qr_owner->_pairing_qr_canvas,
                offset + x,
                offset + y,
                dark ? lv_color_black() : lv_color_white(),
                LV_OPA_COVER
            );
        }
    }
}

// Translate provisioning events into progress text or a terminal screen.
void Navigation::updatePairingState()
{
    if (_current != Screen::PairQr) {
        return;
    }

    const ProvisioningState state = ProvisioningService::instance().state();
    switch (state) {
    case ProvisioningState::WaitingForApp:
        _pairing_message->setText(
            "Bluetooth ready. Scan this QR in the Espressif app."
        );
        break;
    case ProvisioningState::CredentialsReceived:
        _pairing_message->setText(
            "Credentials received. Preparing the Wi-Fi profile..."
        );
        break;
    case ProvisioningState::Connecting:
        _pairing_message->setText(
            "Credentials received. Connecting to Wi-Fi..."
        );
        break;
    case ProvisioningState::Success: {
        char summary[128] = {};
        if (ProvisioningService::instance().connectionSummary(
                summary,
                sizeof(summary)
            ) != ESP_OK) {
            strlcpy(
                summary,
                "Credentials accepted. Wi-Fi is connecting.",
                sizeof(summary)
            );
        }
        showPairingResult(summary, true);
        return;
    }
    case ProvisioningState::Failed:
        showPairingResult(
            "The network did not accept the credentials.\n"
            "Check the password and retry.",
            false
        );
        return;
    default:
        break;
    }

    if (_pairing_started_at_us > 0 &&
        esp_timer_get_time() - _pairing_started_at_us >=
            kPairingTimeoutUs) {
        (void)qsee_provisioning_stop();
        showPairingResult(
            "Pairing timed out after 5 minutes.\nStart a new session to retry.",
            false
        );
    }
}

void Navigation::updateLogView()
{
    if (_log_box == nullptr || _log_snapshot_buffer == nullptr) {
        if (_log_box != nullptr) {
            _log_box->setText("Log buffer unavailable", false);
        }
        return;
    }
    qsee_logger_snapshot(_log_snapshot_buffer, kLogBufferBytes, 32);
    const bool follow_latest = _log_box->isAtBottom();
    _log_box->setText(_log_snapshot_buffer, follow_latest);
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
