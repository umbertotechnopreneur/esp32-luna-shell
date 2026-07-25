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

#include "qsee_status_bar.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <new>

namespace {

// The top of the panel is circular, so a straight pill wastes the upper
// corners. Keep the status indicators on a shallow arch instead.
constexpr int kBarWidth = 300;
constexpr int kBarHeight = 132;
constexpr int kBarOffsetY = 3;
constexpr int kSlotWidth = 40;
constexpr int kSlotHeight = 47;
constexpr int kSlotStartX = 3;
constexpr int kSlotPitch = 50;
constexpr int kSlotDotY = 38;
constexpr int kCurveStartAngle = 214;
constexpr int kCurveEndAngle = 326;
constexpr int kCurveBandWidth = 48;
constexpr int kCurveEdgeWidth = 2;

constexpr uint32_t kPillColor = 0x02070B;
constexpr uint32_t kPillBorderColor = 0x245363;
constexpr uint32_t kReadyColor = 0x20DFC1;
constexpr uint32_t kWarningColor = 0xF0B44D;
constexpr uint32_t kMissingColor = 0xEB4D5C;
constexpr uint32_t kFaultColor = 0xFF3859;
constexpr uint32_t kChargeMarkColor = 0xF4FFFF;

constexpr size_t kIndicatorCount = 6;
constexpr size_t kMaxPartsPerIndicator = 8;

enum class PaintKind : uint8_t {
    Fill,
    Border,
    Line,
    Arc,
};

struct PaintPart {
    lv_obj_t *object = nullptr;
    PaintKind kind = PaintKind::Fill;
};

struct IndicatorVisual {
    lv_obj_t *root = nullptr;
    lv_obj_t *status_dot = nullptr;
    std::array<PaintPart, kMaxPartsPerIndicator> parts{};
    size_t part_count = 0;
};

enum IndicatorIndex : size_t {
    kUsbIndex = 0,
    kBatteryIndex,
    kBluetoothIndex,
    kWifiIndex,
    kGpsIndex,
    kSdIndex,
};

static const lv_point_precise_t kBluetoothMainPoints[] = {
    {12, 2},
    {19, 8},
    {12, 13},
    {19, 18},
    {12, 24},
    {12, 2},
    {5, 8},
};

static const lv_point_precise_t kBluetoothLowerPoints[] = {
    {5, 18},
    {12, 13},
};

static const lv_point_precise_t kGpsTailPoints[] = {
    {8, 14},
    {13, 23},
    {18, 14},
};

static const lv_point_precise_t kSdOutlinePoints[] = {
    {5, 2},
    {17, 2},
    {23, 8},
    {23, 24},
    {5, 24},
    {5, 2},
};

void makePassive(lv_obj_t *object)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *makeFill(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    int radius = 0
)
{
    lv_obj_t *object = lv_obj_create(parent);
    if (object == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    makePassive(object);
    return object;
}

lv_obj_t *makeBorder(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    int radius,
    int border_width
)
{
    lv_obj_t *object = lv_obj_create(parent);
    if (object == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, border_width, LV_PART_MAIN);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    makePassive(object);
    return object;
}

lv_obj_t *makeLine(
    lv_obj_t *parent,
    const lv_point_precise_t *points,
    size_t point_count,
    int width = 2
)
{
    lv_obj_t *object = lv_line_create(parent);
    if (object == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, 4, 2);
    lv_obj_set_size(object, 26, 26);
    lv_line_set_points(object, points, static_cast<uint32_t>(point_count));
    lv_obj_set_style_line_width(object, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(object, true, LV_PART_MAIN);
    lv_obj_set_style_line_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    makePassive(object);
    return object;
}

lv_obj_t *makeArc(lv_obj_t *parent, int inset)
{
    lv_obj_t *object = lv_arc_create(parent);
    if (object == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, 4 + inset, 1 + inset);
    lv_obj_set_size(object, 26 - (inset * 2), 26 - (inset * 2));
    lv_arc_set_bg_angles(object, 215, 325);
    lv_obj_set_style_arc_width(object, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(object, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    makePassive(object);
    return object;
}

lv_obj_t *makeCurve(lv_obj_t *parent, int width, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *object = lv_arc_create(parent);
    if (object == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, 0, 0);
    lv_obj_set_size(object, kBarWidth, kBarHeight);
    lv_arc_set_bg_angles(object, kCurveStartAngle, kCurveEndAngle);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(object, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(object, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(object, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(object, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(object, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(object, LV_OPA_TRANSP, LV_PART_KNOB);
    makePassive(object);
    return object;
}

bool addPart(IndicatorVisual &indicator, lv_obj_t *object, PaintKind kind)
{
    if (object == nullptr || indicator.part_count >= indicator.parts.size()) {
        return false;
    }

    indicator.parts[indicator.part_count++] = {object, kind};
    return true;
}

void setPaintColor(const PaintPart &part, lv_color_t color)
{
    if (part.object == nullptr) {
        return;
    }

    switch (part.kind) {
    case PaintKind::Fill:
        lv_obj_set_style_bg_color(part.object, color, LV_PART_MAIN);
        break;
    case PaintKind::Border:
        lv_obj_set_style_border_color(part.object, color, LV_PART_MAIN);
        break;
    case PaintKind::Line:
        lv_obj_set_style_line_color(part.object, color, LV_PART_MAIN);
        break;
    case PaintKind::Arc:
        lv_obj_set_style_arc_color(part.object, color, LV_PART_MAIN);
        break;
    }
}

lv_color_t stateColor(qsee::StatusIndicatorState state)
{
    switch (state) {
    case qsee::StatusIndicatorState::Ready:
        return lv_color_hex(kReadyColor);
    case qsee::StatusIndicatorState::Degraded:
    case qsee::StatusIndicatorState::Unknown:
        return lv_color_hex(kWarningColor);
    case qsee::StatusIndicatorState::Missing:
        return lv_color_hex(kMissingColor);
    case qsee::StatusIndicatorState::Fault:
    default:
        return lv_color_hex(kFaultColor);
    }
}

void updateIndicator(IndicatorVisual &indicator, qsee::StatusIndicatorState state)
{
    const lv_color_t color = stateColor(state);
    for (size_t i = 0; i < indicator.part_count; ++i) {
        setPaintColor(indicator.parts[i], color);
    }
    if (indicator.status_dot != nullptr) {
        lv_obj_set_style_bg_color(indicator.status_dot, color, LV_PART_MAIN);
    }
}

qsee::StatusIndicatorState resolvedBatteryState(const qsee::StatusBarState &state)
{
    if (state.battery != qsee::StatusIndicatorState::Ready ||
        state.battery_charging ||
        state.battery_percent < 0) {
        return state.battery;
    }
    if (state.battery_percent <= 10) {
        return qsee::StatusIndicatorState::Fault;
    }
    if (state.battery_percent <= 20) {
        return qsee::StatusIndicatorState::Degraded;
    }
    return state.battery;
}

qsee::StatusIndicatorState resolvedGpsState(const qsee::StatusBarState &state)
{
    if (state.gps == qsee::StatusIndicatorState::Ready && !state.gps_has_fix) {
        return qsee::StatusIndicatorState::Degraded;
    }
    return state.gps;
}

qsee::StatusIndicatorState resolvedSdState(const qsee::StatusBarState &state)
{
    if (state.sd == qsee::StatusIndicatorState::Ready && !state.sd_mounted) {
        return qsee::StatusIndicatorState::Degraded;
    }
    return state.sd;
}

}  // namespace

namespace qsee {

struct QSeeStatusBar::Impl {
    lv_obj_t *root = nullptr;
    lv_obj_t *curve_base = nullptr;
    lv_obj_t *curve_edge = nullptr;
    std::array<IndicatorVisual, kIndicatorCount> indicators{};
    lv_obj_t *battery_fill = nullptr;
    lv_obj_t *battery_charge_mark = nullptr;
    std::array<lv_obj_t *, 3> wifi_arcs{};
};

QSeeStatusBar::QSeeStatusBar() = default;

QSeeStatusBar::~QSeeStatusBar()
{
    Shutdown();
}

bool QSeeStatusBar::Begin(lv_obj_t *parent)
{
    if (parent == nullptr || !lv_obj_is_valid(parent)) {
        return false;
    }
    if (_impl != nullptr && _impl->root != nullptr && lv_obj_is_valid(_impl->root)) {
        return true;
    }

    Shutdown();
    _impl.reset(new (std::nothrow) Impl());
    if (_impl == nullptr) {
        return false;
    }

    _impl->root = lv_obj_create(parent);
    if (_impl->root == nullptr) {
        Shutdown();
        return false;
    }

    lv_obj_remove_style_all(_impl->root);
    lv_obj_set_size(_impl->root, kBarWidth, kBarHeight);
    lv_obj_align(_impl->root, LV_ALIGN_TOP_MID, 0, kBarOffsetY);
    lv_obj_set_style_bg_opa(_impl->root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_impl->root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_impl->root, 0, LV_PART_MAIN);
    makePassive(_impl->root);

    _impl->curve_base = makeCurve(
        _impl->root, kCurveBandWidth, kPillColor, LV_OPA_70
    );
    _impl->curve_edge = makeCurve(
        _impl->root, kCurveEdgeWidth, kPillBorderColor, LV_OPA_90
    );
    if (_impl->curve_base == nullptr || _impl->curve_edge == nullptr) {
        Shutdown();
        return false;
    }

    for (size_t i = 0; i < _impl->indicators.size(); ++i) {
        IndicatorVisual &indicator = _impl->indicators[i];
        indicator.root = lv_obj_create(_impl->root);
        if (indicator.root == nullptr) {
            Shutdown();
            return false;
        }

        lv_obj_remove_style_all(indicator.root);
        const int distance_from_center_twice =
            std::abs(5 - (2 * static_cast<int>(i)));
        const int slot_y = 3 + (distance_from_center_twice * 2);
        lv_obj_set_pos(
            indicator.root,
            kSlotStartX + (static_cast<int>(i) * kSlotPitch),
            slot_y
        );
        lv_obj_set_size(indicator.root, kSlotWidth, kSlotHeight);
        makePassive(indicator.root);

        indicator.status_dot = makeFill(
            indicator.root, (kSlotWidth - 6) / 2, kSlotDotY, 6, 6, LV_RADIUS_CIRCLE
        );
        if (indicator.status_dot == nullptr) {
            Shutdown();
            return false;
        }
    }

    IndicatorVisual &usb = _impl->indicators[kUsbIndex];
    if (!addPart(usb, makeFill(usb.root, 9, 9, 16, 12, 3), PaintKind::Fill) ||
        !addPart(usb, makeFill(usb.root, 11, 3, 3, 8, 1), PaintKind::Fill) ||
        !addPart(usb, makeFill(usb.root, 20, 3, 3, 8, 1), PaintKind::Fill) ||
        !addPart(usb, makeFill(usb.root, 15, 20, 4, 8, 1), PaintKind::Fill)) {
        Shutdown();
        return false;
    }

    IndicatorVisual &battery = _impl->indicators[kBatteryIndex];
    if (!addPart(battery, makeBorder(battery.root, 5, 8, 21, 14, 3, 2), PaintKind::Border) ||
        !addPart(battery, makeFill(battery.root, 26, 12, 3, 6, 1), PaintKind::Fill)) {
        Shutdown();
        return false;
    }
    _impl->battery_fill = makeFill(battery.root, 8, 11, 15, 8, 1);
    if (!addPart(battery, _impl->battery_fill, PaintKind::Fill)) {
        Shutdown();
        return false;
    }
    static const lv_point_precise_t charge_points[] = {
        {15, 6},
        {11, 13},
        {15, 13},
        {11, 21},
    };
    _impl->battery_charge_mark = makeLine(battery.root, charge_points, std::size(charge_points), 2);
    if (_impl->battery_charge_mark == nullptr) {
        Shutdown();
        return false;
    }

    IndicatorVisual &bluetooth = _impl->indicators[kBluetoothIndex];
    if (!addPart(
            bluetooth,
            makeLine(bluetooth.root, kBluetoothMainPoints, std::size(kBluetoothMainPoints)),
            PaintKind::Line
        ) ||
        !addPart(
            bluetooth,
            makeLine(bluetooth.root, kBluetoothLowerPoints, std::size(kBluetoothLowerPoints)),
            PaintKind::Line
        )) {
        Shutdown();
        return false;
    }

    IndicatorVisual &wifi = _impl->indicators[kWifiIndex];
    _impl->wifi_arcs[0] = makeArc(wifi.root, 7);
    _impl->wifi_arcs[1] = makeArc(wifi.root, 4);
    _impl->wifi_arcs[2] = makeArc(wifi.root, 1);
    for (lv_obj_t *arc : _impl->wifi_arcs) {
        if (!addPart(wifi, arc, PaintKind::Arc)) {
            Shutdown();
            return false;
        }
    }
    if (!addPart(wifi, makeFill(wifi.root, 14, 21, 6, 6, LV_RADIUS_CIRCLE), PaintKind::Fill)) {
        Shutdown();
        return false;
    }

    IndicatorVisual &gps = _impl->indicators[kGpsIndex];
    if (!addPart(gps, makeBorder(gps.root, 8, 3, 18, 18, LV_RADIUS_CIRCLE, 2), PaintKind::Border) ||
        !addPart(gps, makeFill(gps.root, 14, 9, 6, 6, LV_RADIUS_CIRCLE), PaintKind::Fill) ||
        !addPart(
            gps,
            makeLine(gps.root, kGpsTailPoints, std::size(kGpsTailPoints)),
            PaintKind::Line
        )) {
        Shutdown();
        return false;
    }

    IndicatorVisual &sd = _impl->indicators[kSdIndex];
    if (!addPart(sd, makeLine(sd.root, kSdOutlinePoints, std::size(kSdOutlinePoints)), PaintKind::Line) ||
        !addPart(sd, makeFill(sd.root, 12, 6, 2, 7, 1), PaintKind::Fill) ||
        !addPart(sd, makeFill(sd.root, 16, 6, 2, 7, 1), PaintKind::Fill) ||
        !addPart(sd, makeFill(sd.root, 20, 8, 2, 5, 1), PaintKind::Fill)) {
        Shutdown();
        return false;
    }

    Update({});
    return true;
}

void QSeeStatusBar::Shutdown()
{
    if (_impl == nullptr) {
        return;
    }

    if (_impl->root != nullptr && lv_obj_is_valid(_impl->root)) {
        lv_obj_delete(_impl->root);
    }
    _impl.reset();
}

void QSeeStatusBar::Update(const StatusBarState &state)
{
    if (_impl == nullptr || _impl->root == nullptr || !lv_obj_is_valid(_impl->root)) {
        return;
    }

    updateIndicator(_impl->indicators[kUsbIndex], state.usb);
    updateIndicator(_impl->indicators[kBatteryIndex], resolvedBatteryState(state));
    updateIndicator(_impl->indicators[kBluetoothIndex], state.bluetooth);
    updateIndicator(_impl->indicators[kWifiIndex], state.wifi);
    updateIndicator(_impl->indicators[kGpsIndex], resolvedGpsState(state));
    updateIndicator(_impl->indicators[kSdIndex], resolvedSdState(state));

    const int battery_percent = state.battery_percent < 0
        ? 35
        : std::clamp<int>(state.battery_percent, 0, 100);
    const int battery_fill_width = std::max(2, (15 * battery_percent) / 100);
    lv_obj_set_width(_impl->battery_fill, battery_fill_width);
    lv_obj_set_style_bg_opa(
        _impl->battery_fill,
        state.battery_percent < 0 ? LV_OPA_40 : LV_OPA_COVER,
        LV_PART_MAIN
    );

    if (state.battery_charging) {
        lv_obj_clear_flag(_impl->battery_charge_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_line_color(
            _impl->battery_charge_mark,
            lv_color_hex(kChargeMarkColor),
            LV_PART_MAIN
        );
    } else {
        lv_obj_add_flag(_impl->battery_charge_mark, LV_OBJ_FLAG_HIDDEN);
    }

    const int wifi_bars = state.wifi_bars < 0
        ? -1
        : std::clamp<int>(state.wifi_bars, 0, 3);
    for (size_t i = 0; i < _impl->wifi_arcs.size(); ++i) {
        lv_opa_t opacity = LV_OPA_COVER;
        if (wifi_bars < 0) {
            opacity = LV_OPA_50;
        } else if (static_cast<int>(i) >= wifi_bars) {
            opacity = LV_OPA_20;
        }
        lv_obj_set_style_arc_opa(_impl->wifi_arcs[i], opacity, LV_PART_MAIN);
    }
}

lv_obj_t *QSeeStatusBar::NativeHandle() const
{
    if (_impl == nullptr || _impl->root == nullptr || !lv_obj_is_valid(_impl->root)) {
        return nullptr;
    }
    return _impl->root;
}

}  // namespace qsee
