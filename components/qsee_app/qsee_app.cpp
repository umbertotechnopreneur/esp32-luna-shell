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

#include <memory>

#include "esp_brookesia.hpp"
#include "esp_lib_utils.h"
#include "esp_log.h"
#include "qsee_navigation.hpp"

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
    esp_brookesia::systems::phone::App(APP_NAME, nullptr, true, false, false)
{
}

bool App::run()
{
    ESP_LOGI("qsee_app", "QSee.ai app started");
    return s_navigation.begin(lv_screen_active());
}

bool App::back()
{
    if (s_navigation.back()) {
        ESP_LOGI("qsee_app", "In-app back navigation");
        return true;
    }

    ESP_LOGI("qsee_app", "Returning to ESP-Brookesia launcher");
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
