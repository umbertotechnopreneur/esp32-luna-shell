#include "qsee_edge_navigation.hpp"

#include <utility>

namespace {

constexpr int kDisplaySize = 466;
constexpr int kSafeAreaLeft = 73;
constexpr int kSafeAreaRight = 393;
constexpr int kEdgeInset = 4;
constexpr int kButtonWidth = 68;
constexpr int kButtonHeight = 144;
constexpr int kButtonY = (kDisplaySize - kButtonHeight) / 2;
constexpr int kLeftButtonX = kEdgeInset;
constexpr int kRightButtonX = kDisplaySize - kEdgeInset - kButtonWidth;

constexpr uint32_t kDarkTop = 0x142632;
constexpr uint32_t kDarkBottom = 0x071018;
constexpr uint32_t kBackAccent = 0x51C7F2;
constexpr uint32_t kOkAccent = 0x3DE0B0;
constexpr uint32_t kCloseAccent = 0xFF7787;
constexpr uint32_t kText = 0xF4FAFF;
constexpr lv_style_selector_t kPressedSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);

static_assert(kLeftButtonX + kButtonWidth < kSafeAreaLeft,
              "The left edge action must stay outside the safe area");
static_assert(kRightButtonX > kSafeAreaRight,
              "The right edge action must stay outside the safe area");

constexpr lv_point_precise_t kBackChevron[] = {
    {30, 3},
    {8, 17},
    {30, 31},
};
constexpr lv_point_precise_t kBackShaft[] = {
    {8, 17},
    {34, 17},
};
constexpr lv_point_precise_t kOkCheck[] = {
    {4, 18},
    {13, 27},
    {31, 6},
};
constexpr lv_point_precise_t kCloseForward[] = {
    {6, 6},
    {28, 28},
};
constexpr lv_point_precise_t kCloseReverse[] = {
    {28, 6},
    {6, 28},
};

const lv_font_t *edgeFont()
{
#if LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return LV_FONT_DEFAULT;
#endif
}

void setLine(lv_obj_t *line, const lv_point_precise_t *points, uint32_t count, uint32_t color)
{
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(line, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
}

void setHidden(lv_obj_t *object, bool hidden)
{
    if (object == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

namespace qsee {

EdgeNavigation::~EdgeNavigation()
{
    Shutdown();
}

bool EdgeNavigation::Begin(lv_obj_t *root)
{
    if (root == nullptr) {
        return false;
    }

    Shutdown();

    _left_button = CreateEdgeButton(root, true, &_left_accent);
    _right_button = CreateEdgeButton(root, false, &_right_accent);
    if (_left_button == nullptr || _right_button == nullptr) {
        Shutdown();
        return false;
    }

    _left_icon_primary = CreateLineIcon(_left_button);
    _left_icon_secondary = CreateLineIcon(_left_button);
    _left_label = CreateLabel(_left_button);
    _right_icon_primary = CreateLineIcon(_right_button);
    _right_icon_secondary = CreateLineIcon(_right_button);
    _right_label = CreateLabel(_right_button);
    if (_left_accent == nullptr || _left_icon_primary == nullptr || _left_icon_secondary == nullptr ||
        _left_label == nullptr || _right_accent == nullptr || _right_icon_primary == nullptr ||
        _right_icon_secondary == nullptr || _right_label == nullptr) {
        Shutdown();
        return false;
    }

    setLine(_left_icon_primary, kBackChevron, 3, kBackAccent);
    setLine(_left_icon_secondary, kBackShaft, 2, kBackAccent);
    lv_label_set_text(_left_label, "BACK");

    lv_obj_add_event_cb(_left_button, OnButtonEvent, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(_right_button, OnButtonEvent, LV_EVENT_ALL, this);

    ApplyBackVisibility();
    ApplyRightMode();
    ApplyEnabledState(_left_button);
    ApplyEnabledState(_right_button);
    return true;
}

void EdgeNavigation::Shutdown()
{
    if (_left_button != nullptr) {
        lv_obj_delete(_left_button);
    }
    if (_right_button != nullptr) {
        lv_obj_delete(_right_button);
    }

    _left_button = nullptr;
    _left_accent = nullptr;
    _left_label = nullptr;
    _left_icon_primary = nullptr;
    _left_icon_secondary = nullptr;
    _right_button = nullptr;
    _right_accent = nullptr;
    _right_label = nullptr;
    _right_icon_primary = nullptr;
    _right_icon_secondary = nullptr;
}

void EdgeNavigation::SetLeftCallback(Callback callback)
{
    _left_callback = std::move(callback);
}

void EdgeNavigation::SetRightCallback(Callback callback)
{
    _right_callback = std::move(callback);
}

void EdgeNavigation::SetBackVisible(bool visible)
{
    _back_visible = visible;
    ApplyBackVisibility();
}

void EdgeNavigation::SetRightMode(RightMode mode)
{
    _right_mode = mode;
    ApplyRightMode();
}

void EdgeNavigation::SetEnabled(bool enabled)
{
    _enabled = enabled;
    ApplyEnabledState(_left_button);
    ApplyEnabledState(_right_button);
}

void EdgeNavigation::OnButtonEvent(lv_event_t *event)
{
    auto *self = static_cast<EdgeNavigation *>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }

    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        if (target == self->_left_button) {
            self->_left_button = nullptr;
            self->_left_accent = nullptr;
            self->_left_label = nullptr;
            self->_left_icon_primary = nullptr;
            self->_left_icon_secondary = nullptr;
        } else if (target == self->_right_button) {
            self->_right_button = nullptr;
            self->_right_accent = nullptr;
            self->_right_label = nullptr;
            self->_right_icon_primary = nullptr;
            self->_right_icon_secondary = nullptr;
        }
        return;
    }

    if (code != LV_EVENT_CLICKED || !self->_enabled) {
        return;
    }

    if (target == self->_left_button && self->_back_visible && self->_left_callback) {
        self->_left_callback();
    } else if (target == self->_right_button && self->_right_mode != RightMode::None &&
               self->_right_callback) {
        self->_right_callback();
    }
}

lv_obj_t *EdgeNavigation::CreateEdgeButton(lv_obj_t *root, bool left, lv_obj_t **accent)
{
    lv_obj_t *button = lv_button_create(root);
    if (button == nullptr) {
        return nullptr;
    }

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kButtonWidth, kButtonHeight);
    lv_obj_set_pos(button, left ? kLeftButtonX : kRightButtonX, kButtonY);
    lv_obj_add_flag(button, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(button, 10);

    lv_obj_set_style_radius(button, kButtonWidth / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(kDarkTop), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(kDarkBottom), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(left ? kBackAccent : kOkAccent), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(button, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(
        button, lv_color_hex(left ? kBackAccent : kOkAccent), LV_PART_MAIN
    );
    lv_obj_set_style_shadow_opa(button, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(button, lv_color_hex(0x1B3545), kPressedSelector);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, kPressedSelector);
    lv_obj_set_style_shadow_opa(button, LV_OPA_60, kPressedSelector);
    lv_obj_set_style_translate_x(button, 0, kPressedSelector);
    lv_obj_set_style_translate_y(button, 0, kPressedSelector);
    lv_obj_set_style_transform_width(button, 0, kPressedSelector);
    lv_obj_set_style_transform_height(button, 0, kPressedSelector);

    *accent = lv_obj_create(button);
    if (*accent != nullptr) {
        lv_obj_remove_style_all(*accent);
        lv_obj_set_size(*accent, 4, 84);
        lv_obj_align(*accent, left ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, left ? -4 : 4, 0);
        lv_obj_set_style_radius(*accent, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(*accent, lv_color_hex(left ? kBackAccent : kOkAccent), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(*accent, LV_OPA_80, LV_PART_MAIN);
        lv_obj_remove_flag(*accent, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(*accent, LV_OBJ_FLAG_SCROLLABLE);
    }

    return button;
}

lv_obj_t *EdgeNavigation::CreateLineIcon(lv_obj_t *parent)
{
    lv_obj_t *line = lv_line_create(parent);
    if (line == nullptr) {
        return nullptr;
    }
    lv_obj_set_size(line, 36, 36);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

lv_obj_t *EdgeNavigation::CreateLabel(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == nullptr) {
        return nullptr;
    }
    lv_obj_set_width(label, kButtonWidth - 8);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -27);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, edgeFont(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

void EdgeNavigation::ApplyBackVisibility()
{
    setHidden(_left_button, !_back_visible);
}

void EdgeNavigation::ApplyRightMode()
{
    if (_right_button == nullptr) {
        return;
    }

    const bool hidden = _right_mode == RightMode::None;
    setHidden(_right_button, hidden);
    if (hidden) {
        return;
    }

    const bool is_ok = _right_mode == RightMode::Ok;
    const uint32_t accent = is_ok ? kOkAccent : kCloseAccent;
    lv_label_set_text(_right_label, is_ok ? "OK" : "CLOSE");
    lv_obj_set_style_border_color(_right_button, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_right_button, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_right_accent, lv_color_hex(accent), LV_PART_MAIN);

    setLine(_right_icon_primary, is_ok ? kOkCheck : kCloseForward, is_ok ? 3 : 2, accent);
    setHidden(_right_icon_secondary, is_ok);
    if (!is_ok) {
        setLine(_right_icon_secondary, kCloseReverse, 2, accent);
    }
}

void EdgeNavigation::ApplyEnabledState(lv_obj_t *button) const
{
    if (button == nullptr) {
        return;
    }

    if (_enabled) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(button, LV_OPA_30, LV_PART_MAIN);
    } else {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(button, LV_OPA_10, LV_PART_MAIN);
    }
}

}  // namespace qsee
