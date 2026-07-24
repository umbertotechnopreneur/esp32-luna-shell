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

#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"
#include "qsee_device_snapshot.hpp"

namespace qsee {

class Navigation {
public:
    enum class Screen {
        Home,
        Confirmation,
        Result,
    };

    bool begin(lv_obj_t *root);
    void presentConfirmation(
        const char *title,
        const char *message,
        std::function<void()> on_confirm,
        std::function<void()> on_cancel
    );
    bool back();

private:
    struct Route {
        Screen screen{Screen::Home};
        std::string title;
        std::string description;
        bool primary_visible{false};
        std::function<void()> on_confirm;
        std::function<void()> on_cancel;
    };

    static void primaryEvent(lv_event_t *event);
    static void confirmEvent(lv_event_t *event);
    static void cancelEvent(lv_event_t *event);
    static void statusTimer(lv_timer_t *timer);

    static const char *screenName(Screen screen);
    void navigate(Route route, bool push_current);
    void render();
    void renderStatus();
    void showHome();
    void showConfirmation();
    void showResult(const char *message);
    void setActionButtons(bool visible);
    lv_obj_t *makeActionButton(const char *text, int x, lv_event_cb_t callback);

    lv_obj_t *_root{};
    lv_obj_t *_safe_area{};
    lv_obj_t *_status_bar{};
    lv_obj_t *_bluetooth_status{};
    lv_obj_t *_wifi_status{};
    lv_obj_t *_gps_status{};
    lv_obj_t *_battery_status{};
    lv_obj_t *_title{};
    lv_obj_t *_description{};
    lv_obj_t *_primary{};
    lv_obj_t *_cancel{};
    lv_obj_t *_confirm{};
    Route _current;
    std::vector<Route> _history;
    lv_timer_t *_status_timer{};
};

} // namespace qsee
