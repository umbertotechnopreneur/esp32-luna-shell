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

// Application shell for QSee.ai UI flow, navigation, and status updates.
// Keeps the app lifecycle wired to the Brookesia phone framework.

#include "qsee_app.hpp"

// Application shell for QSee.ai UI flow, navigation, and status updates.
// Keeps the app lifecycle wired to the Brookesia phone framework.

#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_navigation.hpp"

namespace {
// Map snapshot health to a visual state, promoting hard runtime failures.
qsee::StatusIndicatorState indicatorState(qsee::DeviceHealth health, esp_err_t error)
{
    switch (health) {
    case qsee::DeviceHealth::Ready:
        return qsee::StatusIndicatorState::Ready;
    case qsee::DeviceHealth::Degraded:
        return error == ESP_FAIL || error == ESP_ERR_INVALID_STATE ||
                       error == ESP_ERR_NO_MEM
                   ? qsee::StatusIndicatorState::Fault
                   : qsee::StatusIndicatorState::Degraded;
    case qsee::DeviceHealth::Missing:
        return qsee::StatusIndicatorState::Missing;
    case qsee::DeviceHealth::Unknown:
    default:
        return qsee::StatusIndicatorState::Unknown;
    }
}

// Collapse the canonical snapshot before entering the status widget's LVGL call path.
qsee::StatusBarState buildStatusBarState()
{
    const qsee::DeviceSnapshot snapshot =
        qsee::DeviceSnapshotService::instance().snapshot();
    qsee::StatusBarState state;
    state.usb = snapshot.battery.usb_present
        ? qsee::StatusIndicatorState::Ready
        : qsee::StatusIndicatorState::Missing;
    state.battery = indicatorState(
        snapshot.battery.health,
        snapshot.battery.last_error
    );
    state.bluetooth = indicatorState(
        snapshot.radios.ble.health,
        snapshot.radios.ble.last_error
    );
    state.wifi = indicatorState(
        snapshot.radios.wifi.health,
        snapshot.radios.wifi.last_error
    );
    state.gps = indicatorState(snapshot.gps.health, snapshot.gps.last_error);
    state.sd = indicatorState(
        snapshot.storage.health,
        snapshot.storage.last_error
    );
    state.battery_percent =
        static_cast<int8_t>(snapshot.battery.level_percent);
    state.battery_charging =
        std::strstr(snapshot.battery.power_state, "charg") != nullptr;
    if (snapshot.radios.wifi.connected) {
        state.wifi_bars = snapshot.radios.wifi.rssi <= -80 ? 1 :
                          snapshot.radios.wifi.rssi <= -65 ? 2 : 3;
    }
    state.gps_has_fix = snapshot.gps.has_fix;
    state.sd_mounted = snapshot.storage.health == qsee::DeviceHealth::Ready;
    return state;
}
}

#define APP_NAME "QSee.ai"

namespace {
qsee::Navigation s_navigation;
}

namespace qsee {

App *App::_instance = nullptr;

App *App::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new App();
    }
    return _instance;
}

App::App():
    esp_brookesia::systems::phone::App(APP_NAME, nullptr, true, true, false)
{
}

bool App::run()
{
    ESP_LOGI("qsee_app", "QSee.ai app started");
    if (!s_navigation.begin()) {
        return false;
    }
    _status_bar = std::make_unique<QSeeStatusBar>();
    if (!_status_bar->Begin(lv_screen_active())) {
        ESP_LOGW("qsee_app", "Custom status bar unavailable");
        _status_bar.reset();
    } else {
        s_navigation.registerAdaptiveVisual(_status_bar->NativeHandle());
    }
    _status_timer = std::make_unique<esp_brookesia::gui::AppTimer>(1000, [this]() { syncStatusBar(); });
    syncStatusBar();
    return true;
}

// Translate one canonical snapshot into display-only status-bar state.
void App::syncStatusBar()
{
    if (_status_bar == nullptr) return;

    _status_bar->Update(buildStatusBarState());
}

bool App::back()
{
    if (s_navigation.back()) {
        ESP_LOGI("qsee_app", "In-app back navigation");
        return true;
    }

    ESP_LOGI("qsee_app", "Returning to ESP-Brookesia launcher");
    _status_timer.reset();
    if (_status_bar != nullptr) {
        _status_bar->Shutdown();
        _status_bar.reset();
    }
    s_navigation.shutdown();
    return notifyCoreClosed();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(
    esp_brookesia::systems::base::App,
    App,
    APP_NAME,
    []() {
        return std::shared_ptr<App>(App::requestInstance(), [](App *) {});
    }
)

} // namespace qsee
