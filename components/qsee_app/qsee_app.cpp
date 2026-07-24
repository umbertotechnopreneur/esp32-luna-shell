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

#include "qsee_app.hpp"

#include <cstdio>
#include <memory>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_navigation.hpp"

namespace {
constexpr int kStatusTextId = 0x5141;

const char *statusGlyph(qsee::DeviceHealth health)
{
    return health == qsee::DeviceHealth::Ready ? "●" :
           health == qsee::DeviceHealth::Missing ? "○" : "!";
}

bool storageFormatRequired(const qsee::StorageSnapshot &storage)
{
    return storage.card_present && !storage.formatted &&
           (storage.last_error == ESP_FAIL || storage.format_attempted);
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
    _status_timer = std::make_unique<esp_brookesia::gui::AppTimer>(1000, [this]() { syncStatusBar(); });
    syncStatusBar();
    return s_navigation.begin();
}

void App::syncStatusBar()
{
    auto *status_bar = getSystem()->getDisplay().getStatusBar();
    if (status_bar == nullptr) return;

    const auto snapshot = qsee::DeviceSnapshotService::instance().snapshot();
    if (!_status_text_added) {
        _status_text_added = status_bar->addText("BT○ Wi○ GP○ SD○", 0, kStatusTextId);
    }
    char status_text[64];
    std::snprintf(status_text, sizeof(status_text), "BT%s Wi%s GP%s SD%s",
                  statusGlyph(snapshot.radios.ble.health),
                  statusGlyph(snapshot.radios.wifi.health),
                  statusGlyph(snapshot.gps.health),
                  storageFormatRequired(snapshot.storage) ? "!" :
                  statusGlyph(snapshot.storage.health));
    if (_status_text_added) {
        (void)status_bar->setText(kStatusTextId, status_text);
    }
    if (snapshot.battery.level_percent >= 0) {
        (void)status_bar->showBatteryPercent();
        (void)status_bar->setBatteryPercent(snapshot.battery.usb_present,
                                            snapshot.battery.level_percent);
    } else {
        (void)status_bar->hideBatteryPercent();
    }

    const auto wifi_state = snapshot.radios.wifi.health == qsee::DeviceHealth::Ready
        ? esp_brookesia::systems::phone::StatusBar::WifiState::SIGNAL_3
        : esp_brookesia::systems::phone::StatusBar::WifiState::DISCONNECTED;
    (void)status_bar->setWifiIconState(wifi_state);
}

bool App::back()
{
    if (s_navigation.back()) {
        ESP_LOGI("qsee_app", "In-app back navigation");
        return true;
    }

    ESP_LOGI("qsee_app", "Returning to ESP-Brookesia launcher");
    _status_timer.reset();
    _status_text_added = false;
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
