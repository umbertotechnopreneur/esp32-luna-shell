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

// Public app entry point for the QSee.ai phone-style experience.
// Owns navigation startup, back handling, and status bar synchronization.

#pragma once

// Public app entry point for the QSee.ai phone-style experience.
// Owns navigation startup, back handling, and status bar synchronization.

#include <memory>

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "gui/lvgl/esp_brookesia_app_widgets.hpp"
#include "qsee_status_bar.hpp"

namespace qsee {

class App final : public esp_brookesia::systems::phone::App {
public:
    static App *requestInstance();
    ~App() override = default;

protected:
    App();
    /** Build the navigation shell and start snapshot-driven status updates. */
    bool run() override;
    /** Navigate inside QSee first, then release the app back to Brookesia. */
    bool back() override;
    /** Map one canonical device snapshot to display-only indicator state. */
    void syncStatusBar();

private:
    static App *_instance;
    std::unique_ptr<esp_brookesia::gui::AppTimer> _status_timer;
    std::unique_ptr<QSeeStatusBar> _status_bar;
};

} // namespace qsee
