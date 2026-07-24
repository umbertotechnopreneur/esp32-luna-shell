#pragma once

#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"

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
        std::string message;
        bool primary_visible{false};
        std::function<void()> on_confirm;
        std::function<void()> on_cancel;
    };

    static void primaryEvent(lv_event_t *event);
    static void confirmEvent(lv_event_t *event);
    static void cancelEvent(lv_event_t *event);

    static const char *screenName(Screen screen);
    void navigate(Route route, bool push_current);
    void render();
    void showHome();
    void showConfirmation();
    void showResult(const char *message);
    void setActionButtons(bool visible);
    lv_obj_t *makeActionButton(const char *text, int x, lv_event_cb_t callback);

    lv_obj_t *_root{};
    lv_obj_t *_safe_area{};
    lv_obj_t *_title{};
    lv_obj_t *_message{};
    lv_obj_t *_primary{};
    lv_obj_t *_cancel{};
    lv_obj_t *_confirm{};
    Route _current;
    std::vector<Route> _history;
};

} // namespace qsee
