#include "qsee_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "qrcode.h"
#include "qsee_device_snapshot.hpp"
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
constexpr int64_t kPairingTimeoutUs = 5LL * 60LL * 1000000LL;
constexpr char kEspressifAppStoreUrl[] =
    "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr lv_style_selector_t kPressedSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
constexpr uint32_t kBrightnessLowPercent = 35;
constexpr uint32_t kBrightnessDefaultPercent = 70;
constexpr uint32_t kBrightnessHighPercent = 100;
constexpr uint32_t kDimTimeoutOptions[] = {15, 30, 60, 120, 0};
constexpr uint32_t kPanelOffDelayOptions[] = {15, 30, 60, 120, 300, 0};

template <size_t Count>
uint32_t nextPreset(uint32_t current, const uint32_t (&options)[Count])
{
    for (size_t index = 0; index < Count; ++index) {
        if (options[index] == current) {
            return options[(index + 1U) % Count];
        }
    }
    return options[0];
}

void formatDuration(uint32_t seconds, char *buffer, size_t buffer_size)
{
    if (seconds == 0U) {
        std::snprintf(buffer, buffer_size, "OFF");
    } else if (seconds < 60U) {
        std::snprintf(buffer, buffer_size, "%lu S",
                      static_cast<unsigned long>(seconds));
    } else {
        std::snprintf(buffer, buffer_size, "%lu MIN",
                      static_cast<unsigned long>(seconds / 60U));
    }
}

bool isSettingsScreen(qsee::Navigation::Screen screen)
{
    using Screen = qsee::Navigation::Screen;
    return screen == Screen::Settings ||
           screen == Screen::SettingsDiagnostics ||
           screen == Screen::SettingsStorage ||
           screen == Screen::SettingsNetwork ||
           screen == Screen::SettingsDisplay ||
           screen == Screen::SettingsPower;
}

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
    const char *symbol
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
}

void styleBottomArcCommand(
    AppButton &button,
    int x,
    int y,
    uint32_t color
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
    const int marker_x = area.x1 + static_cast<int>(
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

double audioPeakDbfs(uint32_t peak)
{
    if (peak == 0U) {
        return kAudioMeterFloorDbfs;
    }
    const double normalized = std::min(1.0, static_cast<double>(peak) / kPcm16FullScale);
    return 20.0 * std::log10(normalized);
}

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
    _log_snapshot_buffer = static_cast<char *>(
        heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (_log_snapshot_buffer == nullptr) {
        _log_snapshot_buffer = static_cast<char *>(heap_caps_malloc(kLogBufferBytes, MALLOC_CAP_8BIT));
    }
    if (_log_snapshot_buffer != nullptr) _log_snapshot_buffer[0] = '\0';
    ESP_LOGI("qsee_navigation", "Log snapshot buffer: %s",
             _log_snapshot_buffer != nullptr ? "ready" : "unavailable");

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
    makeMetric(_ram, _welcome.get(), "PSRAM", 0, kTeal);
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

    // Icon-only actions keep the six destinations legible inside the circular bezel.
    _open_logs = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Logs); });
    styleArcButton(*_open_logs, 54, 56, 330, kBlue, LV_SYMBOL_LIST);
    _open_audio = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Audio); });
    styleArcButton(*_open_audio, 54, 114, 344, kBlue, LV_SYMBOL_AUDIO);
    _open_imu = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Imu); });
    styleArcButton(*_open_imu, 54, 172, 360, kTeal, LV_SYMBOL_GPS);
    _open_hardware = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Hardware); });
    styleArcButton(*_open_hardware, 54, 230, 360, kTeal, LV_SYMBOL_DRIVE);
    _open_about = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::About); });
    styleArcButton(*_open_about, 54, 288, 344, kBlue, "i");
    _open_settings = std::make_unique<AppButton>(_screen.get(), [this]() { show(Screen::Settings); });
    styleArcButton(*_open_settings, 54, 346, 330, kBlue, LV_SYMBOL_SETTINGS);

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
        _logs_pause->setText(_logs_paused ? "RESUME" : "PAUSE");
    });
    _logs_pause->setText("PAUSE");
    // Equal commands sit symmetrically on the lower safe arc, like the home actions.
    styleBottomArcCommand(*_logs_pause, 49, 308, kSlate);
    _logs_clear = std::make_unique<AppButton>(_logs.get(), [this]() {
        qsee_logger_clear();
        _log_box->setText("", false);
    });
    _logs_clear->setText("CLEAR");
    styleBottomArcCommand(*_logs_clear, 167, 308, kBlue);

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
        lv_obj_set_size(1, 151);
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

void Navigation::buildSettingsUi()
{
    const auto make_page = [this]() {
        auto page = std::make_unique<LvContainer>(_surface.get());
        page->setStyleAttribute(StyleSize::RECT(kWidth, 232));
        page->setStyleAttribute(StyleAlign{STYLE_ALIGN_TYPE_TOP_MID, 0, 44});
        transparentPanel(*page, false);
        return page;
    };

    _settings = make_page();
    _settings_diagnostics_open = std::make_unique<AppButton>(
        _settings.get(), [this]() { show(Screen::SettingsDiagnostics); }
    );
    _settings_diagnostics_open->setText("DIAGNOSTICS\n300 ms | 70%");
    buttonStyle(*_settings_diagnostics_open, 112, 54, kTeal);
    _settings_diagnostics_open->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 2});
    _settings_storage_open = std::make_unique<AppButton>(
        _settings.get(), [this]() { show(Screen::SettingsStorage); }
    );
    _settings_storage_open->setText("STORAGE\nSD -- | M --");
    buttonStyle(*_settings_storage_open, 112, 54, kBlue);
    _settings_storage_open->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 2});
    _settings_network_open = std::make_unique<AppButton>(
        _settings.get(), [this]() { show(Screen::SettingsNetwork); }
    );
    _settings_network_open->setText("NETWORK\nWI-FI --");
    buttonStyle(*_settings_network_open, 112, 54, kBlue);
    _settings_network_open->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 64});
    _settings_display_open = std::make_unique<AppButton>(
        _settings.get(), [this]() { show(Screen::SettingsDisplay); }
    );
    _settings_display_open->setText("DISPLAY\n100% | ROT OFF");
    buttonStyle(*_settings_display_open, 112, 54, kTeal);
    _settings_display_open->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 64});
    _settings_power_open = std::make_unique<AppButton>(
        _settings.get(), [this]() { show(Screen::SettingsPower); }
    );
    _settings_power_open->setText("POWER\nDIM --\nOFF --");
    buttonStyle(*_settings_power_open, 112, 54, kSlate);
    _settings_power_open->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 126});
    _content_labels.push_back(
        makeLabel(
            _settings.get(),
            "Only implemented device controls are exposed",
            kRowWidth,
            10,
            kMuted,
            STYLE_ALIGN_TYPE_TOP_MID,
            0,
            190
        )
    );

    _settings_diagnostics_page = make_page();
    _settings_period = makeLabel(
        _settings_diagnostics_page.get(),
        "Sample interval 300 ms",
        kRowWidth,
        15,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        4
    );
    _content_labels.push_back(
        makeLabel(
            _settings_diagnostics_page.get(),
            "Audio Scope and IMU diagnostics",
            kRowWidth,
            10,
            kMuted,
            STYLE_ALIGN_TYPE_TOP_LEFT,
            8,
            27
        )
    );
    _settings_faster = std::make_unique<AppButton>(
        _settings_diagnostics_page.get(),
        [this]() { adjustDiagnosticPeriod(-100); }
    );
    _settings_faster->setText("FASTER");
    buttonStyle(*_settings_faster, 104, 34, kTeal);
    _settings_faster->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 44});
    _settings_slower = std::make_unique<AppButton>(
        _settings_diagnostics_page.get(),
        [this]() { adjustDiagnosticPeriod(100); }
    );
    _settings_slower->setText("SLOWER");
    buttonStyle(*_settings_slower, 104, 34, kBlue);
    _settings_slower->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 44});
    _settings_smoothing = makeLabel(
        _settings_diagnostics_page.get(),
        "IMU input smoothing 70%",
        kRowWidth,
        15,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        92
    );
    _settings_smoothing_less = std::make_unique<AppButton>(
        _settings_diagnostics_page.get(),
        [this]() { adjustImuSmoothing(-10); }
    );
    _settings_smoothing_less->setText("LESS");
    buttonStyle(*_settings_smoothing_less, 104, 34, kSlate);
    _settings_smoothing_less->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 120});
    _settings_smoothing_more = std::make_unique<AppButton>(
        _settings_diagnostics_page.get(),
        [this]() { adjustImuSmoothing(10); }
    );
    _settings_smoothing_more->setText("MORE");
    buttonStyle(*_settings_smoothing_more, 104, 34, kBlue);
    _settings_smoothing_more->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 120});
    _settings_default = std::make_unique<AppButton>(
        _settings_diagnostics_page.get(),
        [this]() {
            (void)qsee_hardware_set_diagnostic_period_ms(300);
            (void)qsee_hardware_set_imu_smoothing_percent(70);
            updateSettings();
        }
    );
    _settings_default->setText("RESET");
    styleBottomArcCommand(*_settings_default, 84, 180, kSlate);

    _settings_storage_page = make_page();
    _settings_storage = makeLabel(
        _settings_storage_page.get(),
        "SD --\nLog mirror --",
        kRowWidth,
        16,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        14
    );
    lv_label_set_long_mode(
        _settings_storage->getNativeHandle(),
        LV_LABEL_LONG_MODE_WRAP
    );
    _content_labels.push_back(
        makeLabel(
            _settings_storage_page.get(),
            "Logs are mirrored only while the card is ready.\n"
            "Formatting remains an explicit Hardware action.",
            kRowWidth,
            11,
            kMuted,
            STYLE_ALIGN_TYPE_TOP_LEFT,
            8,
            76
        )
    );
    _settings_sd_mirror = std::make_unique<AppButton>(
        _settings_storage_page.get(), [this]() { toggleSdMirror(); }
    );
    _settings_sd_mirror->setText("MIRROR ON");
    styleBottomArcCommand(*_settings_sd_mirror, 84, 180, kTeal);

    _settings_network_page = make_page();
    _settings_wifi = makeLabel(
        _settings_network_page.get(),
        "WI-FI DEFERRED\nSSID --\nIP --\nPASSWORD --",
        kRowWidth,
        13,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        2
    );
    lv_label_set_long_mode(_settings_wifi->getNativeHandle(), LV_LABEL_LONG_MODE_WRAP);
    _settings_wifi_start = std::make_unique<AppButton>(
        _settings_network_page.get(), [this]() { startWifi(); }
    );
    _settings_wifi_start->setText("START RADIO");
    buttonStyle(*_settings_wifi_start, 128, 32, kSlate);
    _settings_wifi_start->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 126});
    _content_labels.push_back(
        makeLabel(
            _settings_network_page.get(),
            "Local keyboard or Espressif BLE provisioning",
            kRowWidth,
            10,
            kMuted,
            STYLE_ALIGN_TYPE_TOP_MID,
            0,
            163
        )
    );
    _settings_wifi_edit = std::make_unique<AppButton>(
        _settings_network_page.get(),
        [this]() { show(Screen::WifiCredentials); }
    );
    _settings_wifi_edit->setText("EDIT");
    styleBottomArcCommand(*_settings_wifi_edit, 16, 180, kBlue);
    _settings_wifi_pair = std::make_unique<AppButton>(
        _settings_network_page.get(),
        [this]() { startPairing(); }
    );
    _settings_wifi_pair->setText("PAIR");
    styleBottomArcCommand(*_settings_wifi_pair, 152, 180, kTeal);

    _settings_display_page = make_page();
    _settings_brightness = makeLabel(
        _settings_display_page.get(),
        "AMOLED brightness DEFAULT | 70%",
        kRowWidth,
        14,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        4
    );
    _settings_brightness_low = std::make_unique<AppButton>(
        _settings_display_page.get(),
        [this]() { setBrightnessProfile(kBrightnessLowPercent); }
    );
    _settings_brightness_low->setText("LOW");
    buttonStyle(*_settings_brightness_low, 70, 34, kSlate);
    _settings_brightness_low->align({STYLE_ALIGN_TYPE_TOP_LEFT, 8, 34});
    _settings_brightness_default = std::make_unique<AppButton>(
        _settings_display_page.get(),
        [this]() { setBrightnessProfile(kBrightnessDefaultPercent); }
    );
    _settings_brightness_default->setText("DEFAULT");
    buttonStyle(*_settings_brightness_default, 82, 34, kBlue);
    _settings_brightness_default->align({STYLE_ALIGN_TYPE_TOP_MID, 0, 34});
    _settings_brightness_high = std::make_unique<AppButton>(
        _settings_display_page.get(),
        [this]() { setBrightnessProfile(kBrightnessHighPercent); }
    );
    _settings_brightness_high->setText("HIGH");
    buttonStyle(*_settings_brightness_high, 70, 34, kTeal);
    _settings_brightness_high->align({STYLE_ALIGN_TYPE_TOP_RIGHT, -8, 34});
    _settings_rotation = makeLabel(
        _settings_display_page.get(),
        "Auto-rotation OFF\nRendering 0 deg | gravity --",
        kRowWidth,
        13,
        kNavy,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        86
    );
    lv_label_set_long_mode(
        _settings_rotation->getNativeHandle(),
        LV_LABEL_LONG_MODE_WRAP
    );
    _content_labels.push_back(
        makeLabel(
            _settings_display_page.get(),
            "Cardinal CO5300 rotations: 0 / 90 / 180 / 270",
            kRowWidth,
            10,
            kMuted,
            STYLE_ALIGN_TYPE_TOP_LEFT,
            8,
            143
        )
    );
    _settings_auto_rotate = std::make_unique<AppButton>(
        _settings_display_page.get(), [this]() { toggleAutoRotation(); }
    );
    _settings_auto_rotate->setText("ROTATE ON");
    styleBottomArcCommand(*_settings_auto_rotate, 84, 180, kTeal);

    _settings_power_page = make_page();
    _settings_dim_timeout = makeLabel(
        _settings_power_page.get(),
        "DIM 50% AFTER",
        108,
        12,
        kMuted,
        STYLE_ALIGN_TYPE_TOP_LEFT,
        8,
        4
    );
    _settings_panel_off_delay = makeLabel(
        _settings_power_page.get(),
        "PANEL OFF LATER",
        108,
        12,
        kMuted,
        STYLE_ALIGN_TYPE_TOP_RIGHT,
        -8,
        4
    );
    _settings_dim_cycle = std::make_unique<AppButton>(
        _settings_power_page.get(), [this]() { cycleDimTimeout(); }
    );
    _settings_dim_cycle->setText("30 S");
    styleBottomArcCommand(*_settings_dim_cycle, 16, 180, kBlue);
    _settings_panel_off_cycle = std::make_unique<AppButton>(
        _settings_power_page.get(), [this]() { cyclePanelOffDelay(); }
    );
    _settings_panel_off_cycle->setText("1 MIN");
    styleBottomArcCommand(*_settings_panel_off_cycle, 152, 180, kSlate);
    _content_labels.push_back(
        makeLabel(
            _settings_power_page.get(),
            "BOOT wakes the device.\n"
            "Display inactivity values are stored in NVS.",
            kRowWidth,
            13,
            kNavy,
            STYLE_ALIGN_TYPE_TOP_LEFT,
            8,
            94
        )
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
        _wifi_editor.get(), [this]() { show(Screen::SettingsNetwork); }
    );
    _wifi_editor_cancel->setText("CANCEL");
    styleBottomArcCommand(*_wifi_editor_cancel, 49, 310, kSlate);
    _wifi_editor_save = std::make_unique<AppButton>(
        _wifi_editor.get(), [this]() { saveWifiCredentials(); }
    );
    _wifi_editor_save->setText("SAVE");
    styleBottomArcCommand(*_wifi_editor_save, 167, 310, kTeal);

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
    styleBottomArcCommand(*_pairing_secondary, 49, 310, kSlate);
    _pairing_primary = std::make_unique<AppButton>(
        _pairing.get(), [this]() { pairingPrimaryAction(); }
    );
    _pairing_primary->setText("NEXT");
    styleBottomArcCommand(*_pairing_primary, 167, 310, kTeal);
}

void Navigation::setVisible(LvObject &object, bool visible)
{
    object.setStyleAttribute(StyleFlag::STYLE_FLAG_HIDDEN, !visible);
}

void Navigation::show(Screen screen)
{
    const Screen previous = _current;
    if (previous == Screen::WifiCredentials &&
        screen != Screen::WifiCredentials) {
        clearWifiPassword();
    }
    if (previous == Screen::PairQr && screen != Screen::PairQr) {
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
        subtitle = "Device controls by category";
        break;
    case Screen::SettingsDiagnostics:
        route = "settings-diagnostics";
        title = "DIAGNOSTICS";
        subtitle = "Sampling and IMU smoothing";
        break;
    case Screen::SettingsStorage:
        route = "settings-storage";
        title = "STORAGE";
        subtitle = "SD card and log mirror";
        break;
    case Screen::SettingsNetwork:
        route = "settings-network";
        title = "NETWORK";
        subtitle = "Wi-Fi credentials and pairing";
        break;
    case Screen::SettingsDisplay:
        route = "settings-display";
        title = "DISPLAY";
        subtitle = "Brightness and cardinal rotation";
        break;
    case Screen::SettingsPower:
        route = "settings-power";
        title = "POWER";
        subtitle = "Dim and panel-off timing";
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
    const bool full_height =
        screen == Screen::Logs ||
        screen == Screen::WifiCredentials ||
        isPairingScreen(screen);
    setVisible(*_title_bar, !full_height);
    setVisible(*_welcome, screen == Screen::Welcome);
    setVisible(*_logs, screen == Screen::Logs);
    setVisible(*_hardware, screen == Screen::Hardware);
    setVisible(*_audio, screen == Screen::Audio);
    setVisible(*_imu, screen == Screen::Imu);
    setVisible(*_settings, screen == Screen::Settings);
    setVisible(
        *_settings_diagnostics_page,
        screen == Screen::SettingsDiagnostics
    );
    setVisible(*_settings_storage_page, screen == Screen::SettingsStorage);
    setVisible(*_settings_network_page, screen == Screen::SettingsNetwork);
    setVisible(*_settings_display_page, screen == Screen::SettingsDisplay);
    setVisible(*_settings_power_page, screen == Screen::SettingsPower);
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
    refresh();
    _refresh_tick_count = 0;
    if (screen == Screen::Audio) {
        updateAudioMeters(true);
    }
    if (isSettingsScreen(screen)) {
        updateSettings();
    } else if (screen == Screen::WifiCredentials) {
        prepareWifiEditor();
    } else if (isPairingScreen(screen)) {
        renderPairing();
    }
    ESP_LOGI("qsee_navigation", "Route ready: %s", route);
}

bool Navigation::back()
{
    if (_current == Screen::Welcome) return false;
    switch (_current) {
    case Screen::SettingsDiagnostics:
    case Screen::SettingsStorage:
    case Screen::SettingsNetwork:
    case Screen::SettingsDisplay:
    case Screen::SettingsPower:
        show(Screen::Settings);
        break;
    case Screen::WifiCredentials:
    case Screen::PairIntro:
    case Screen::PairResult:
        show(Screen::SettingsNetwork);
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
    _display_wake_guard.reset();
    (void)qsee_provisioning_stop();
    clearWifiPassword();
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
    _audio_meter_updated_at_us = 0;
    _refresh_tick_count = 0;
    for (size_t channel = 0; channel < 2U; ++channel) {
        _audio_meter_visual[channel][0] = 0.0F;
        _audio_meter_visual[channel][1] = 0.0F;
        _audio_peak_hold_until_us[channel] = 0;
    }
    _audio_mic0.reset();
    _audio_mic1.reset();
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
    _welcome.reset();
    _logs.reset();
    _hardware.reset();
    _audio.reset();
    _imu.reset();
    for (lv_obj_t *&arc : _imu_gyro_arcs) {
        arc = nullptr;
    }
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
    _settings_wifi_edit.reset();
    _settings_wifi_pair.reset();
    _settings_brightness.reset();
    _settings_brightness_low.reset();
    _settings_brightness_default.reset();
    _settings_brightness_high.reset();
    _settings_dim_timeout.reset();
    _settings_dim_cycle.reset();
    _settings_panel_off_delay.reset();
    _settings_panel_off_cycle.reset();
    _settings_rotation.reset();
    _settings_auto_rotate.reset();
    _settings_diagnostics_open.reset();
    _settings_storage_open.reset();
    _settings_network_open.reset();
    _settings_display_open.reset();
    _settings_power_open.reset();
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
    _settings_diagnostics_page.reset();
    _settings_storage_page.reset();
    _settings_network_page.reset();
    _settings_display_page.reset();
    _settings_power_page.reset();
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
        updateSettings();
    } else if (_current == Screen::PairQr) {
        updatePairingState();
    }
    updateDiagnosticViews();
}

void Navigation::onRefreshTick()
{
    qsee_power_report_display_inactive_ms(
        lv_display_get_inactive_time(nullptr)
    );
    updateDisplayWakeGuard();
    if (_current == Screen::Audio) {
        updateAudioMeters(false);
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
    setVisible(*_display_wake_guard, required);
    if (required) {
        lv_obj_move_foreground(_display_wake_guard->getNativeHandle());
    }
}

// Wake the panel but keep this press from reaching the hidden control below.
void Navigation::displayWakeGuardEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        qsee_power_wake_display();
    }
}

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

void Navigation::updateDiagnosticViews()
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    char text[160];
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

void Navigation::setBrightnessProfile(uint32_t percent)
{
    const esp_err_t result = qsee_display_set_brightness_percent(percent);
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Brightness update failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::cycleDimTimeout()
{
    const uint32_t requested = nextPreset(
        qsee_power_get_dim_timeout_seconds(),
        kDimTimeoutOptions
    );
    const esp_err_t result =
        qsee_power_set_dim_timeout_seconds(requested);
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Dim timeout update failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::cyclePanelOffDelay()
{
    const uint32_t requested = nextPreset(
        qsee_power_get_panel_off_delay_seconds(),
        kPanelOffDelayOptions
    );
    const esp_err_t result =
        qsee_power_set_panel_off_delay_seconds(requested);
    if (result != ESP_OK) {
        ESP_LOGW("qsee_navigation", "Panel-off delay update failed (%s)",
                 esp_err_to_name(result));
    }
    updateSettings();
}

void Navigation::toggleAutoRotation()
{
    const bool enabled = !qsee_display_get_auto_rotation_enabled();
    const esp_err_t result =
        qsee_display_set_auto_rotation_enabled(enabled);
    if (result != ESP_OK) {
        ESP_LOGW(
            "qsee_navigation",
            "Auto-rotation update failed (%s)",
            esp_err_to_name(result)
        );
    }
    updateSettings();
}

void Navigation::updateSettings()
{
    if (_settings_period == nullptr || _settings_smoothing == nullptr ||
        _settings_storage == nullptr || _settings_wifi == nullptr ||
        _settings_brightness == nullptr ||
        _settings_brightness_low == nullptr ||
        _settings_brightness_default == nullptr ||
        _settings_brightness_high == nullptr ||
        _settings_dim_timeout == nullptr ||
        _settings_dim_cycle == nullptr ||
        _settings_panel_off_delay == nullptr ||
        _settings_panel_off_cycle == nullptr ||
        _settings_rotation == nullptr ||
        _settings_auto_rotate == nullptr ||
        _settings_diagnostics_open == nullptr ||
        _settings_storage_open == nullptr ||
        _settings_network_open == nullptr ||
        _settings_display_open == nullptr ||
        _settings_power_open == nullptr) {
        return;
    }
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    char text[192];
    const uint32_t period_ms = qsee_hardware_get_diagnostic_period_ms();
    const uint32_t smoothing = qsee_hardware_get_imu_smoothing_percent();
    std::snprintf(
        text,
        sizeof(text),
        "Sample interval %lu ms",
        static_cast<unsigned long>(period_ms)
    );
    _settings_period->setText(text);
    std::snprintf(
        text,
        sizeof(text),
        "IMU input smoothing %lu%%",
        static_cast<unsigned long>(smoothing)
    );
    _settings_smoothing->setText(text);
    std::snprintf(
        text,
        sizeof(text),
        "DIAGNOSTICS\n%lu ms | %lu%%",
        static_cast<unsigned long>(period_ms),
        static_cast<unsigned long>(smoothing)
    );
    _settings_diagnostics_open->setText(text);

    const bool mirror = qsee_storage_get_logger_mirror_preference();
    const char *storage_state =
        snapshot.storage.health == DeviceHealth::Ready ? "READY" :
        snapshot.storage.card_present ? "NOT MOUNTED" : "ABSENT";
    const char *storage_hub_state =
        snapshot.storage.health == DeviceHealth::Ready ? "READY" :
        snapshot.storage.card_present ? "UNMOUNT" : "ABSENT";
    std::snprintf(
        text,
        sizeof(text),
        "SD %s\nLog mirror %s",
        storage_state,
        mirror ? "ON" : "OFF"
    );
    _settings_storage->setText(text);
    _settings_sd_mirror->setText(mirror ? "MIRROR OFF" : "MIRROR ON");
    std::snprintf(
        text,
        sizeof(text),
        "STORAGE\n%s | M %s",
        storage_hub_state,
        mirror ? "ON" : "OFF"
    );
    _settings_storage_open->setText(text);

    const WifiSnapshot &wifi = snapshot.radios.wifi;
    const char *wifi_state =
        wifi.ip_acquired ? "ONLINE" :
        wifi.connected ? "CONNECTED" :
        wifi.started ? "STARTED" : "DEFERRED";
    std::snprintf(
        text,
        sizeof(text),
        "WI-FI %s\nSSID %s\nIP %s\nPASSWORD %s",
        wifi_state,
        wifi.ssid[0] != '\0' ? wifi.ssid : "--",
        wifi.ip_acquired && wifi.ip_address[0] != '\0'
            ? wifi.ip_address : "--",
        wifi.credentials_configured ? "SAVED" : "NOT SET"
    );
    _settings_wifi->setText(text);
    _settings_wifi_start->setText(
        wifi.started ? "RADIO ON" : "START RADIO"
    );
    setVisible(*_settings_wifi_start, !wifi.started);
    std::snprintf(
        text,
        sizeof(text),
        "NETWORK\nWI-FI %s",
        wifi_state
    );
    _settings_network_open->setText(text);

    const uint32_t brightness = qsee_display_get_brightness_percent();
    const char *brightness_profile =
        brightness == kBrightnessLowPercent ? "LOW" :
        brightness == kBrightnessDefaultPercent ? "DEFAULT" :
        brightness == kBrightnessHighPercent ? "HIGH" : "CUSTOM";
    std::snprintf(text, sizeof(text), "AMOLED brightness %s  |  %lu%%",
                  brightness_profile,
                  static_cast<unsigned long>(brightness));
    _settings_brightness->setText(text);
    _settings_brightness_low->setText(
        brightness == kBrightnessLowPercent ? "LOW *" : "LOW"
    );
    _settings_brightness_default->setText(
        brightness == kBrightnessDefaultPercent ? "DEFAULT *" : "DEFAULT"
    );
    _settings_brightness_high->setText(
        brightness == kBrightnessHighPercent ? "HIGH *" : "HIGH"
    );
    const bool auto_rotation = qsee_display_get_auto_rotation_enabled();
    const OrientationSnapshot &orientation = snapshot.ui.orientation;
    std::snprintf(
        text,
        sizeof(text),
        "Auto-rotation %s\nPhysical %u deg | rendering %u deg\nGravity %s%s",
        auto_rotation ? "ON" : "OFF",
        static_cast<unsigned>(orientation.physical_orientation_degrees),
        static_cast<unsigned>(orientation.rendering_rotation_degrees),
        auto_rotation
            ? (orientation.gravity_valid ? "LOCKED" : "WAITING")
            : "IDLE",
        orientation.rotation_pending ? " | pending" : ""
    );
    _settings_rotation->setText(text);
    _settings_auto_rotate->setText(
        auto_rotation ? "ROTATE OFF" : "ROTATE ON"
    );
    std::snprintf(
        text,
        sizeof(text),
        "DISPLAY\n%lu%% | ROT %s",
        static_cast<unsigned long>(brightness),
        auto_rotation ? "ON" : "OFF"
    );
    _settings_display_open->setText(text);

    char dim_duration[24];
    char panel_duration[24];
    formatDuration(
        qsee_power_get_dim_timeout_seconds(),
        dim_duration,
        sizeof(dim_duration)
    );
    _settings_dim_cycle->setText(dim_duration);
    formatDuration(
        qsee_power_get_panel_off_delay_seconds(),
        panel_duration,
        sizeof(panel_duration)
    );
    _settings_panel_off_cycle->setText(panel_duration);
    std::snprintf(
        text,
        sizeof(text),
        "POWER\nDIM %s\nOFF %s",
        dim_duration,
        panel_duration
    );
    _settings_power_open->setText(text);
}

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

void Navigation::wifiPasswordChangedEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(
        lv_event_get_user_data(event)
    );
    if (navigation != nullptr && !navigation->_wifi_editor_loading) {
        navigation->_wifi_password_dirty = true;
    }
}

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
    if ((!same_saved_profile && !password_entered) ||
        (_wifi_password_dirty && !password_entered)) {
        _wifi_editor_message->setText(
            "Enter a password for a new or changed network."
        );
        _wifi_editor_message->setTextColor(StyleColor::COLOR(kError));
        wipeBytes(password, sizeof(password));
        return;
    }

    const bool replace_password = password_entered;
    const esp_err_t result = qsee_wifi_update_credentials(
        ssid,
        password,
        replace_password,
        true
    );
    wipeBytes(password, sizeof(password));
    clearWifiPassword();
    if (result == ESP_OK) {
        show(Screen::SettingsNetwork);
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
            show(Screen::SettingsNetwork);
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
        show(Screen::SettingsNetwork);
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
