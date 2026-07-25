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
#include <memory>

#include "lvgl.h"

namespace qsee {

enum class StatusIndicatorState : uint8_t {
    Ready,
    Degraded,
    Unknown,
    Missing,
    Fault,
};

/**
 * Display-only state for the round-device status bar.
 *
 * Keeping this model independent from DeviceSnapshotService makes the widget
 * reusable and leaves snapshot-to-visual policy in the application shell.
 */
struct StatusBarState {
    StatusIndicatorState usb = StatusIndicatorState::Unknown;
    StatusIndicatorState battery = StatusIndicatorState::Unknown;
    StatusIndicatorState bluetooth = StatusIndicatorState::Unknown;
    StatusIndicatorState wifi = StatusIndicatorState::Unknown;
    StatusIndicatorState gps = StatusIndicatorState::Unknown;
    StatusIndicatorState sd = StatusIndicatorState::Unknown;

    int8_t battery_percent = -1;
    int8_t wifi_bars = -1;
    bool battery_charging = false;
    bool gps_has_fix = false;
    bool sd_mounted = false;
};

class QSeeStatusBar final {
public:
    QSeeStatusBar();
    ~QSeeStatusBar();

    QSeeStatusBar(const QSeeStatusBar &) = delete;
    QSeeStatusBar &operator=(const QSeeStatusBar &) = delete;

    /**
     * Creates the 244 x 44 px bar at y=28, above the 320 px safe square.
     * Call from the LVGL thread.
     */
    bool Begin(lv_obj_t *parent);
    void Shutdown();
    void Update(const StatusBarState &state);

    lv_obj_t *NativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace qsee
