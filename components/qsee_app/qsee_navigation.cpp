#include "qsee_navigation.hpp"

#include "esp_log.h"

namespace {
constexpr int kDisplaySize = 466;
constexpr int kSafeOrigin = 73;
constexpr int kSafeSize = 320;
constexpr uint32_t kBlue = 0x2F80ED;
constexpr uint32_t kNavy = 0x071A2C;
constexpr uint32_t kMuted = 0x69849C;
const char *TAG = "qsee_nav";
}

namespace qsee {

bool Navigation::begin(lv_obj_t *root)
{
    if (root == nullptr) {
        return false;
    }
    _root = root;
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, kDisplaySize, kDisplaySize);
    lv_obj_set_style_bg_color(_root, lv_color_hex(0xF7FBFF), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _safe_area = lv_obj_create(_root);
    lv_obj_set_pos(_safe_area, kSafeOrigin, kSafeOrigin);
    lv_obj_set_size(_safe_area, kSafeSize, kSafeSize);
    lv_obj_set_style_radius(_safe_area, 28, 0);
    lv_obj_set_style_bg_color(_safe_area, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(_safe_area, 1, 0);
    lv_obj_set_style_border_color(_safe_area, lv_color_hex(0xD9E9F7), 0);
    lv_obj_set_style_pad_all(_safe_area, 24, 0);
    lv_obj_clear_flag(_safe_area, LV_OBJ_FLAG_SCROLLABLE);

    _title = lv_label_create(_safe_area);
    lv_obj_set_width(_title, 272);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_title, lv_color_hex(kNavy), 0);
    lv_obj_set_style_text_font(_title, &lv_font_montserrat_28, 0);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 22);

    _message = lv_label_create(_safe_area);
    lv_obj_set_width(_message, 258);
    lv_obj_set_style_text_align(_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_message, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(_message, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(_message, LV_LABEL_LONG_WRAP);
    lv_obj_align(_message, LV_ALIGN_CENTER, 0, 4);

    _primary = lv_button_create(_safe_area);
    lv_obj_set_size(_primary, 196, 52);
    lv_obj_align(_primary, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(_primary, 26, 0);
    lv_obj_set_style_bg_color(_primary, lv_color_hex(kBlue), 0);
    lv_obj_set_style_bg_color(_primary, lv_color_hex(0x1E66BC), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(_primary, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(_primary, 0, LV_STATE_PRESSED);
    lv_obj_add_event_cb(_primary, primaryEvent, LV_EVENT_CLICKED, this);
    lv_obj_t *primary_label = lv_label_create(_primary);
    lv_obj_set_style_text_color(primary_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(primary_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(primary_label, "OPEN CONFIRMATION");
    lv_obj_center(primary_label);

    _cancel = makeActionButton("CANCEL", 6, cancelEvent);
    _confirm = makeActionButton("OK", 398, confirmEvent);
    _history.clear();
    showHome();
    return true;
}

lv_obj_t *Navigation::makeActionButton(const char *text, int x, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(_root);
    lv_obj_set_pos(button, x, 190);
    lv_obj_set_size(button, 62, 86);
    lv_obj_set_style_radius(button, 30, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kNavy), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kBlue), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void Navigation::setActionButtons(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_confirm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_cancel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_confirm, LV_OBJ_FLAG_HIDDEN);
    }
}

void Navigation::showHome()
{
    navigate(
        {
            .screen = Screen::Home,
            .title = "QSee.ai",
            .message = "Device ready.\nAll essential content stays inside the 320 x 320 safe area.",
            .primary_visible = true,
        },
        false
    );
}

void Navigation::presentConfirmation(
    const char *title,
    const char *message,
    std::function<void()> on_confirm,
    std::function<void()> on_cancel
)
{
    navigate(
        {
            .screen = Screen::Confirmation,
            .title = title,
            .message = message,
            .primary_visible = false,
            .on_confirm = std::move(on_confirm),
            .on_cancel = std::move(on_cancel),
        },
        true
    );
}

void Navigation::showConfirmation()
{
    presentConfirmation(
        "Confirm action?",
        "The side margins are reserved for deterministic Cancel and OK actions.",
        [this]() {
            ESP_LOGI(TAG, "Confirmation accepted");
            showResult("Confirmed");
        },
        []() {
            ESP_LOGI(TAG, "Confirmation cancelled");
        }
    );
}

void Navigation::showResult(const char *message)
{
    navigate(
        {
            .screen = Screen::Result,
            .title = "QSee.ai",
            .message = message,
            .primary_visible = true,
        },
        false
    );
}

bool Navigation::back()
{
    if (_history.empty()) {
        return false;
    }

    if (_current.screen == Screen::Confirmation && _current.on_cancel) {
        _current.on_cancel();
    }

    const Screen from = _current.screen;
    _current = std::move(_history.back());
    _history.pop_back();
    render();
    ESP_LOGI(
        TAG,
        "History pop: %s -> %s, depth=%u",
        screenName(from),
        screenName(_current.screen),
        static_cast<unsigned>(_history.size())
    );
    return true;
}

const char *Navigation::screenName(Screen screen)
{
    switch (screen) {
    case Screen::Home:
        return "home";
    case Screen::Confirmation:
        return "confirmation";
    case Screen::Result:
        return "result";
    }
    return "unknown";
}

void Navigation::navigate(Route route, bool push_current)
{
    const Screen from = _current.screen;
    if (push_current) {
        _history.push_back(std::move(_current));
    }
    _current = std::move(route);
    render();
    ESP_LOGI(
        TAG,
        "History %s: %s -> %s, depth=%u",
        push_current ? "push" : "replace",
        screenName(from),
        screenName(_current.screen),
        static_cast<unsigned>(_history.size())
    );
}

void Navigation::render()
{
    lv_label_set_text(_title, _current.title.c_str());
    lv_label_set_text(_message, _current.message.c_str());
    if (_current.primary_visible) {
        lv_obj_remove_flag(_primary, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_primary, LV_OBJ_FLAG_HIDDEN);
    }
    setActionButtons(_current.screen == Screen::Confirmation);
}

void Navigation::primaryEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(lv_event_get_user_data(event));
    navigation->showConfirmation();
}

void Navigation::confirmEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(lv_event_get_user_data(event));
    if (navigation->_current.on_confirm) {
        navigation->_current.on_confirm();
    }
}

void Navigation::cancelEvent(lv_event_t *event)
{
    auto *navigation = static_cast<Navigation *>(lv_event_get_user_data(event));
    (void)navigation->back();
}

} // namespace qsee
