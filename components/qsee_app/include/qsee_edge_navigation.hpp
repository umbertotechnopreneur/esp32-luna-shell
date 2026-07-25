#pragma once

// Edge-gesture helpers used by the QSee navigation shell.
// Converts boundary swipes into screen navigation decisions.

#include <cstdint>
#include <functional>

#include "lvgl.h"

namespace qsee {

class EdgeNavigation final {
public:
    enum class RightMode : uint8_t {
        None,
        Ok,
        Close,
    };

    using Callback = std::function<void()>;

    EdgeNavigation() = default;
    ~EdgeNavigation();

    EdgeNavigation(const EdgeNavigation &) = delete;
    EdgeNavigation &operator=(const EdgeNavigation &) = delete;

    /** Create round-margin actions under the supplied LVGL root. */
    bool Begin(lv_obj_t *root);
    /** Delete owned LVGL controls and clear their cached handles. */
    void Shutdown();

    void SetLeftCallback(Callback callback);
    void SetRightCallback(Callback callback);
    void SetBackVisible(bool visible);
    void SetRightMode(RightMode mode);
    void SetEnabled(bool enabled);

private:
    /** Dispatch clicks and clear cached handles when LVGL deletes a button. */
    static void OnButtonEvent(lv_event_t *event);

    /** Build one curved edge action and return its separately colored accent. */
    lv_obj_t *CreateEdgeButton(lv_obj_t *root, bool left, lv_obj_t **accent);
    lv_obj_t *CreateLineIcon(lv_obj_t *parent);
    lv_obj_t *CreateLabel(lv_obj_t *parent);
    void ApplyBackVisibility();
    void ApplyRightMode();
    void ApplyEnabledState(lv_obj_t *button) const;

    lv_obj_t *_left_button = nullptr;
    lv_obj_t *_left_accent = nullptr;
    lv_obj_t *_left_label = nullptr;
    lv_obj_t *_left_icon_primary = nullptr;
    lv_obj_t *_left_icon_secondary = nullptr;
    lv_obj_t *_right_button = nullptr;
    lv_obj_t *_right_accent = nullptr;
    lv_obj_t *_right_label = nullptr;
    lv_obj_t *_right_icon_primary = nullptr;
    lv_obj_t *_right_icon_secondary = nullptr;

    Callback _left_callback;
    Callback _right_callback;
    RightMode _right_mode = RightMode::None;
    bool _back_visible = false;
    bool _enabled = true;
};

}  // namespace qsee
