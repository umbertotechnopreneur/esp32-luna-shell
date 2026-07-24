#pragma once

#include <functional>
#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_brookesia_lv_container.hpp"
#include "esp_brookesia_lv_screen.hpp"

namespace esp_brookesia::gui {

class AppScreen final : public LvScreen {
public:
    AppScreen();
    explicit AppScreen(lv_obj_t *existing);
    static AppScreen active();
};

class AppLabel final : public LvObject {
public:
    explicit AppLabel(const LvObject *parent);

    bool setText(const char *text);
    bool setWidth(int width);
    bool setFont(const StyleFont &font);
    bool setTextColor(const StyleColor &color);
    bool align(const StyleAlign &alignment);
};

class AppButton final : public LvObject {
public:
    AppButton(const LvObject *parent, std::function<void()> on_click);

    bool setText(const char *text);
    bool setWidth(int width);
    bool setHeight(int height);
    bool setFont(const StyleFont &font);
    bool setTextColor(const StyleColor &color);
    bool setBackgroundGradient(const StyleColor &start, const StyleColor &end);
    bool align(const StyleAlign &alignment);

private:
    static void onEvent(lv_event_t *event);
    std::function<void()> _on_click;
    std::unique_ptr<AppLabel> _label;
};

class AppProgressBar final : public LvObject {
public:
    explicit AppProgressBar(const LvObject *parent);

    bool setRange(int minimum, int maximum);
    bool setValue(int value, bool animated = true);
    bool setWidth(int width);
    bool setHeight(int height);
    bool setIndicatorColor(const StyleColor &color);
    bool setIndicatorGradient(const StyleColor &start, const StyleColor &end);
    bool setBackgroundColor(const StyleColor &color);
    bool align(const StyleAlign &alignment);
};

class AppTextBox final : public LvObject {
public:
    explicit AppTextBox(const LvObject *parent);

    bool setText(const char *text, bool follow_latest = true);
    bool isAtBottom() const;
    bool setWidth(int width);
    bool setHeight(int height);
    bool setFont(const StyleFont &font);
    bool setTextColor(const StyleColor &color);
    bool setBackgroundColor(const StyleColor &color);
    bool align(const StyleAlign &alignment);
};

class AppRichText final : public LvObject {
public:
    explicit AppRichText(const LvObject *parent);

    bool setText(const char *text, bool follow_latest = true);
    bool setWidth(int width);
    bool setHeight(int height);
    bool setFont(const StyleFont &font);
    bool setTextColor(const StyleColor &color);
    bool setBackgroundColor(const StyleColor &color);
    bool align(const StyleAlign &alignment);
    bool isAtBottom() const;
};

class AppImage final : public LvObject {
public:
    explicit AppImage(const LvObject *parent);

    bool setRawRgb565(const uint8_t *data, uint32_t width, uint32_t height, size_t data_size);
    bool setWidth(int width);
    bool setHeight(int height);
    bool align(const StyleAlign &alignment);

private:
    lv_image_dsc_t _raw_image{};
};

class AppTimer final {
public:
    AppTimer(uint32_t period_ms, std::function<void()> callback);
    ~AppTimer();

    AppTimer(const AppTimer &) = delete;
    AppTimer &operator=(const AppTimer &) = delete;

private:
    static void onTimer(lv_timer_t *timer);
    lv_timer_t *_timer{};
    std::function<void()> _callback;
};

}  // namespace esp_brookesia::gui
