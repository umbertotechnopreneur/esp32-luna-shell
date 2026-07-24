#include "esp_brookesia_app_widgets.hpp"

#include <cstring>
#include <utility>

#include "esp_brookesia_lv_helper.hpp"

namespace esp_brookesia::gui {

AppScreen::AppScreen(): LvScreen()
{
}

AppScreen::AppScreen(lv_obj_t *existing): LvScreen(existing, false)
{
}

AppScreen AppScreen::active()
{
    return AppScreen(lv_screen_active());
}

AppLabel::AppLabel(const LvObject *parent):
    LvObject(lv_label_create(parent != nullptr ? parent->getNativeHandle() : nullptr))
{
}

bool AppLabel::setText(const char *text)
{
    if (!isValid()) {
        return false;
    }
    lv_label_set_text(getNativeHandle(), text != nullptr ? text : "");
    return true;
}

bool AppLabel::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppLabel::setFont(const StyleFont &font)
{
    return setStyleAttribute(font);
}

bool AppLabel::setTextColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_TEXT, color);
}

bool AppLabel::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

AppButton::AppButton(const LvObject *parent, std::function<void()> on_click):
    LvObject(lv_button_create(parent != nullptr ? parent->getNativeHandle() : nullptr)),
    _on_click(std::move(on_click))
{
    if (isValid()) {
        addEventCallback(onEvent, LV_EVENT_CLICKED, this);
        _label = std::make_unique<AppLabel>(this);
        _label->align({STYLE_ALIGN_TYPE_CENTER, 0, 0});
    }
}

bool AppButton::setText(const char *text)
{
    return _label != nullptr && _label->setText(text);
}

bool AppButton::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppButton::setHeight(int height)
{
    return setStyleAttribute(StyleSize::RECT(StyleSize::LENGTH_AUTO, height));
}

bool AppButton::setFont(const StyleFont &font)
{
    return _label != nullptr && _label->setFont(font);
}

bool AppButton::setTextColor(const StyleColor &color)
{
    return _label != nullptr && _label->setTextColor(color);
}

bool AppButton::setBackgroundGradient(const StyleColor &start, const StyleColor &end)
{
    if (!isValid()) return false;
    lv_obj_set_style_bg_color(getNativeHandle(), toLvColor(start.color), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(getNativeHandle(), toLvColor(end.color), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(getNativeHandle(), LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(getNativeHandle(), start.opacity, LV_PART_MAIN);
    return true;
}

bool AppButton::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

void AppButton::onEvent(lv_event_t *event)
{
    auto *button = static_cast<AppButton *>(lv_event_get_user_data(event));
    if (button != nullptr && button->_on_click) {
        button->_on_click();
    }
}

AppProgressBar::AppProgressBar(const LvObject *parent):
    LvObject(lv_bar_create(parent != nullptr ? parent->getNativeHandle() : nullptr))
{
}

bool AppProgressBar::setRange(int minimum, int maximum)
{
    if (!isValid()) {
        return false;
    }
    lv_bar_set_range(getNativeHandle(), minimum, maximum);
    return true;
}

bool AppProgressBar::setValue(int value, bool animated)
{
    if (!isValid()) {
        return false;
    }
    lv_bar_set_value(getNativeHandle(), value, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    return true;
}

bool AppProgressBar::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppProgressBar::setHeight(int height)
{
    return setStyleAttribute(StyleSize::RECT(StyleSize::LENGTH_AUTO, height));
}

bool AppProgressBar::setIndicatorColor(const StyleColor &color)
{
    if (!isValid()) return false;
    lv_obj_set_style_bg_color(getNativeHandle(), toLvColor(color.color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(getNativeHandle(), color.opacity, LV_PART_INDICATOR);
    return true;
}

bool AppProgressBar::setIndicatorGradient(const StyleColor &start, const StyleColor &end)
{
    if (!isValid()) return false;
    lv_obj_set_style_bg_color(getNativeHandle(), toLvColor(start.color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(getNativeHandle(), toLvColor(end.color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(getNativeHandle(), LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(getNativeHandle(), start.opacity, LV_PART_INDICATOR);
    return true;
}

bool AppProgressBar::setBackgroundColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, color);
}

bool AppProgressBar::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

AppTextBox::AppTextBox(const LvObject *parent):
    LvObject(lv_textarea_create(parent != nullptr ? parent->getNativeHandle() : nullptr))
{
    if (isValid()) {
        lv_textarea_set_cursor_click_pos(getNativeHandle(), false);
        lv_textarea_set_one_line(getNativeHandle(), false);
    }
}

bool AppTextBox::setText(const char *text, bool follow_latest)
{
    if (!isValid()) {
        return false;
    }
    lv_textarea_set_text(getNativeHandle(), text != nullptr ? text : "");
    if (follow_latest) {
        lv_textarea_set_cursor_pos(getNativeHandle(), LV_TEXTAREA_CURSOR_LAST);
    }
    return true;
}

bool AppTextBox::isAtBottom() const
{
    return isValid() && lv_obj_get_scroll_bottom(getNativeHandle()) <= 2;
}

bool AppTextBox::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppTextBox::setHeight(int height)
{
    return setStyleAttribute(StyleSize::RECT(StyleSize::LENGTH_AUTO, height));
}

bool AppTextBox::setFont(const StyleFont &font)
{
    return setStyleAttribute(font);
}

bool AppTextBox::setTextColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_TEXT, color);
}

bool AppTextBox::setBackgroundColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, color);
}

bool AppTextBox::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

AppRichText::AppRichText(const LvObject *parent):
    LvObject(lv_spangroup_create(parent != nullptr ? parent->getNativeHandle() : nullptr))
{
    if (isValid()) {
        lv_spangroup_set_mode(getNativeHandle(), LV_SPAN_MODE_BREAK);
        lv_spangroup_set_overflow(getNativeHandle(), LV_SPAN_OVERFLOW_CLIP);
        lv_obj_set_scroll_dir(getNativeHandle(), LV_DIR_VER);
        lv_obj_set_scrollbar_mode(getNativeHandle(), LV_SCROLLBAR_MODE_AUTO);
    }
}

bool AppRichText::setText(const char *text, bool follow_latest)
{
    if (!isValid()) return false;
    const bool was_at_bottom = isAtBottom();
    const char *source = text != nullptr ? text : "";
    while (lv_spangroup_get_span_count(getNativeHandle()) > 0U) {
        lv_span_t *span = lv_spangroup_get_child(getNativeHandle(), 0);
        if (span == nullptr) break;
        lv_spangroup_delete_span(getNativeHandle(), span);
    }

    const size_t length = std::strlen(source);
    size_t line_start = 0;
    while (line_start < length || (length == 0U && line_start == 0U)) {
        size_t line_end = line_start;
        while (line_end < length && source[line_end] != '\n') ++line_end;
        size_t first = line_start;
        while (first < line_end && (source[first] == ' ' || source[first] == '\t')) ++first;
        uint32_t color = 0xEAF1F6;
        if (first < line_end && (source[first] == 'W' || source[first] == 'w')) color = 0xF6AD55;
        else if (first < line_end && (source[first] == 'E' || source[first] == 'e' ||
                                      source[first] == 'A' || source[first] == 'a')) color = 0xFC8181;
        else if (first < line_end && (source[first] == 'I' || source[first] == 'i')) color = 0x68D391;
        else if (first < line_end && (source[first] == 'D' || source[first] == 'd')) color = 0xA0AEC0;

        std::string line(source + line_start, line_end - line_start);
        line.push_back('\n');
        lv_span_t *span = lv_spangroup_add_span(getNativeHandle());
        if (span == nullptr) return false;
        lv_span_set_text(span, line.c_str());
        lv_style_set_text_color(lv_span_get_style(span), lv_color_hex(color));
        lv_style_set_text_font(lv_span_get_style(span), &lv_font_montserrat_10);
        if (line_end >= length) break;
        line_start = line_end + 1U;
    }
    lv_spangroup_refresh(getNativeHandle());
    if (follow_latest && was_at_bottom) {
        lv_obj_scroll_to_y(getNativeHandle(),
                           lv_obj_get_scroll_y(getNativeHandle()) +
                               lv_obj_get_scroll_bottom(getNativeHandle()), LV_ANIM_OFF);
    }
    return true;
}

bool AppRichText::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppRichText::setHeight(int height)
{
    return setStyleAttribute(StyleSize::RECT(StyleSize::LENGTH_AUTO, height));
}

bool AppRichText::setFont(const StyleFont &font)
{
    return setStyleAttribute(font);
}

bool AppRichText::setTextColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_TEXT, color);
}

bool AppRichText::setBackgroundColor(const StyleColor &color)
{
    return setStyleAttribute(StyleColorItem::STYLE_COLOR_ITEM_BACKGROUND, color);
}

bool AppRichText::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

bool AppRichText::isAtBottom() const
{
    return isValid() && lv_obj_get_scroll_bottom(getNativeHandle()) <= 2;
}

AppImage::AppImage(const LvObject *parent):
    LvObject(lv_image_create(parent != nullptr ? parent->getNativeHandle() : nullptr))
{
}

bool AppImage::setRawRgb565(const uint8_t *data, uint32_t width, uint32_t height, size_t data_size)
{
    if (!isValid() || data == nullptr || width == 0 || height == 0 || data_size == 0) {
        return false;
    }
    _raw_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    _raw_image.header.cf = LV_COLOR_FORMAT_RGB565;
    _raw_image.header.w = width;
    _raw_image.header.h = height;
    _raw_image.header.stride = width * 2U;
    _raw_image.data_size = data_size;
    _raw_image.data = data;
    lv_image_set_src(getNativeHandle(), &_raw_image);
    return true;
}

bool AppImage::setWidth(int width)
{
    return setStyleAttribute(StyleSize::RECT(width, StyleSize::LENGTH_AUTO));
}

bool AppImage::setHeight(int height)
{
    return setStyleAttribute(StyleSize::RECT(StyleSize::LENGTH_AUTO, height));
}

bool AppImage::align(const StyleAlign &alignment)
{
    return setStyleAttribute(alignment);
}

AppTimer::AppTimer(uint32_t period_ms, std::function<void()> callback):
    _callback(std::move(callback))
{
    _timer = lv_timer_create(onTimer, period_ms, this);
}

AppTimer::~AppTimer()
{
    if (_timer != nullptr) {
        lv_timer_del(_timer);
    }
}

void AppTimer::onTimer(lv_timer_t *timer)
{
    auto *app_timer = static_cast<AppTimer *>(lv_timer_get_user_data(timer));
    if (app_timer != nullptr && app_timer->_callback) {
        app_timer->_callback();
    }
}

}  // namespace esp_brookesia::gui
