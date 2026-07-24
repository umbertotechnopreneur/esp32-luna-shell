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

#include <memory>

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "gui/lvgl/esp_brookesia_app_widgets.hpp"

namespace qsee {

class App final : public esp_brookesia::systems::phone::App {
public:
    static App *requestInstance();
    ~App() override = default;

protected:
    App();
    bool run() override;
    bool back() override;
    void syncStatusBar();

private:
    static App *_instance;
    std::unique_ptr<esp_brookesia::gui::AppTimer> _status_timer;
    bool _status_text_added = false;
};

} // namespace qsee
