/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include "esp_brookesia_gui_internal.h"
#if !ESP_BROOKESIA_LVGL_HELPER_ENABLE_DEBUG_LOG
#   define ESP_BROOKESIA_UTILS_DISABLE_DEBUG_LOG
#endif
#include "private/esp_brookesia_lv_utils.hpp"
#include "style/esp_brookesia_gui_style.hpp"
#include "esp_brookesia_lv_helper.hpp"

namespace esp_brookesia::gui {

lv_color_t toLvColor(uint32_t color)
{
    return lv_color_hex(color);
}

lv_align_t toLvAlign(const StyleAlignType &type)
{
    switch (type) {
    case STYLE_ALIGN_TYPE_TOP_LEFT:
        return LV_ALIGN_TOP_LEFT;
    case STYLE_ALIGN_TYPE_TOP_MID:
        return LV_ALIGN_TOP_MID;
    case STYLE_ALIGN_TYPE_TOP_RIGHT:
        return LV_ALIGN_TOP_RIGHT;
    case STYLE_ALIGN_TYPE_BOTTOM_LEFT:
        return LV_ALIGN_BOTTOM_LEFT;
    case STYLE_ALIGN_TYPE_BOTTOM_MID:
        return LV_ALIGN_BOTTOM_MID;
    case STYLE_ALIGN_TYPE_BOTTOM_RIGHT:
        return LV_ALIGN_BOTTOM_RIGHT;
    case STYLE_ALIGN_TYPE_LEFT_MID:
        return LV_ALIGN_LEFT_MID;
    case STYLE_ALIGN_TYPE_RIGHT_MID:
        return LV_ALIGN_RIGHT_MID;
    case STYLE_ALIGN_TYPE_CENTER:
        return LV_ALIGN_CENTER;
    default:
        ESP_UTILS_LOGE("Invalid align type: %d, use default.", type);
        return LV_ALIGN_DEFAULT;
    }
}

lv_text_align_t toLvTextAlign(const StyleAlignType &type)
{
    switch (type) {
    case STYLE_ALIGN_TYPE_LEFT_MID:
        return LV_TEXT_ALIGN_LEFT;
    case STYLE_ALIGN_TYPE_RIGHT_MID:
        return LV_TEXT_ALIGN_RIGHT;
    case STYLE_ALIGN_TYPE_CENTER:
        return LV_TEXT_ALIGN_CENTER;
    default:
        ESP_UTILS_LOGE("Invalid align type: %d, use default.", type);
        return LV_TEXT_ALIGN_AUTO;
    }
}

lv_flex_flow_t toLvFlexFlow(const StyleLayoutFlex::FlowType &flow)
{
    switch (flow) {
    case StyleLayoutFlex::FLOW_ROW:
        return LV_FLEX_FLOW_ROW;
    case StyleLayoutFlex::FLOW_COLUMN:
        return LV_FLEX_FLOW_COLUMN;
    case StyleLayoutFlex::FLOW_ROW_WRAP:
        return LV_FLEX_FLOW_ROW_WRAP;
    case StyleLayoutFlex::FLOW_ROW_REVERSE:
        return LV_FLEX_FLOW_ROW_REVERSE;
    case StyleLayoutFlex::FLOW_ROW_WRAP_REVERSE:
        return LV_FLEX_FLOW_ROW_WRAP_REVERSE;
    case StyleLayoutFlex::FLOW_COLUMN_WRAP:
        return LV_FLEX_FLOW_COLUMN_WRAP;
    case StyleLayoutFlex::FLOW_COLUMN_REVERSE:
        return LV_FLEX_FLOW_COLUMN_REVERSE;
    case StyleLayoutFlex::FLOW_COLUMN_WRAP_REVERSE:
        return LV_FLEX_FLOW_COLUMN_WRAP_REVERSE;
    default:
        ESP_UTILS_LOGE("Invalid flow type: %d, use default.", flow);
        return LV_FLEX_FLOW_ROW;
    }
}

lv_flex_align_t toLvFlexAlign(const StyleLayoutFlex::AlignType &align)
{
    switch (align) {
    case StyleLayoutFlex::ALIGN_START:
        return LV_FLEX_ALIGN_START;
    case StyleLayoutFlex::ALIGN_END:
        return LV_FLEX_ALIGN_END;
    case StyleLayoutFlex::ALIGN_CENTER:
        return LV_FLEX_ALIGN_CENTER;
    case StyleLayoutFlex::ALIGN_SPACE_EVENLY:
        return LV_FLEX_ALIGN_SPACE_EVENLY;
    case StyleLayoutFlex::ALIGN_SPACE_AROUND:
        return LV_FLEX_ALIGN_SPACE_AROUND;
    case StyleLayoutFlex::ALIGN_SPACE_BETWEEN:
        return LV_FLEX_ALIGN_SPACE_BETWEEN;
    default:
        ESP_UTILS_LOGE("Invalid align type: %d, use default.", align);
        return LV_FLEX_ALIGN_START;
    }
}

lv_obj_flag_t toLvFlags(const StyleFlag &flags)
{
    lv_obj_flag_t lv_flag = static_cast<lv_obj_flag_t>(0);

    if (flags & STYLE_FLAG_HIDDEN) {
        lv_flag = static_cast<lv_obj_flag_t>(lv_flag | LV_OBJ_FLAG_HIDDEN);
    }
    if (flags & STYLE_FLAG_CLICKABLE) {
        lv_flag = static_cast<lv_obj_flag_t>(lv_flag | LV_OBJ_FLAG_CLICKABLE);
    }
    if (flags & STYLE_FLAG_SCROLLABLE) {
        lv_flag = static_cast<lv_obj_flag_t>(lv_flag | LV_OBJ_FLAG_SCROLLABLE);
    }
    if (flags & STYLE_FLAG_EVENT_BUBBLE) {
        lv_flag = static_cast<lv_obj_flag_t>(lv_flag | LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    if (flags & STYLE_FLAG_SEND_DRAW_TASK_EVENTS) {
        lv_flag = static_cast<lv_obj_flag_t>(lv_flag | LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    }

    return lv_flag;
}

bool checkLvObjIsValid(lv_obj_t *obj)
{
    return (obj != nullptr) && lv_obj_is_valid(obj);
}

bool checkLvObjOutOfParent(lv_obj_t *obj)
{
    lv_area_t child_coords;
    lv_area_t parent_coords;
    lv_obj_t *parent = lv_obj_get_parent(obj);

    lv_obj_refr_pos(obj);
    lv_obj_refr_pos(parent);
    lv_obj_update_layout(obj);
    lv_obj_update_layout(parent);
    lv_obj_get_coords(obj, &child_coords);
    lv_obj_get_coords(parent, &parent_coords);

    if ((child_coords.x1 < parent_coords.x1) || (child_coords.y1 < parent_coords.y1) ||
            (child_coords.x2 > parent_coords.x2) || (child_coords.y2 > parent_coords.y2)) {

        return true;
    }

    return false;
}

bool checkLvEventCodeValid(lv_event_code_t code)
{
    return ((code > _LV_EVENT_LAST) && (code < LV_EVENT_PREPROCESS));
}

bool getLvInternalFontBySize(uint8_t size_px, const lv_font_t **font)
{
    struct FontCandidate {
        uint8_t size;
        const lv_font_t *font;
    };

    static const FontCandidate candidates[] = {
#if LV_FONT_MONTSERRAT_8
        {8, &lv_font_montserrat_8},
#endif
#if LV_FONT_MONTSERRAT_10
        {10, &lv_font_montserrat_10},
#endif
#if LV_FONT_MONTSERRAT_12
        {12, &lv_font_montserrat_12},
#endif
#if LV_FONT_MONTSERRAT_14
        {14, &lv_font_montserrat_14},
#endif
#if LV_FONT_MONTSERRAT_16
        {16, &lv_font_montserrat_16},
#endif
#if LV_FONT_MONTSERRAT_18
        {18, &lv_font_montserrat_18},
#endif
#if LV_FONT_MONTSERRAT_20
        {20, &lv_font_montserrat_20},
#endif
#if LV_FONT_MONTSERRAT_22
        {22, &lv_font_montserrat_22},
#endif
#if LV_FONT_MONTSERRAT_24
        {24, &lv_font_montserrat_24},
#endif
#if LV_FONT_MONTSERRAT_26
        {26, &lv_font_montserrat_26},
#endif
#if LV_FONT_MONTSERRAT_28
        {28, &lv_font_montserrat_28},
#endif
#if LV_FONT_MONTSERRAT_30
        {30, &lv_font_montserrat_30},
#endif
#if LV_FONT_MONTSERRAT_32
        {32, &lv_font_montserrat_32},
#endif
#if LV_FONT_MONTSERRAT_34
        {34, &lv_font_montserrat_34},
#endif
#if LV_FONT_MONTSERRAT_36
        {36, &lv_font_montserrat_36},
#endif
#if LV_FONT_MONTSERRAT_38
        {38, &lv_font_montserrat_38},
#endif
#if LV_FONT_MONTSERRAT_40
        {40, &lv_font_montserrat_40},
#endif
#if LV_FONT_MONTSERRAT_42
        {42, &lv_font_montserrat_42},
#endif
#if LV_FONT_MONTSERRAT_44
        {44, &lv_font_montserrat_44},
#endif
#if LV_FONT_MONTSERRAT_46
        {46, &lv_font_montserrat_46},
#endif
#if LV_FONT_MONTSERRAT_48
        {48, &lv_font_montserrat_48},
#endif
    };

    const uint8_t requested_size = std::clamp(
        static_cast<int>(size_px),
        StyleFont::FONT_SIZE_MIN,
        StyleFont::FONT_SIZE_MAX
    );
    const FontCandidate *nearest = nullptr;
    int nearest_distance = std::numeric_limits<int>::max();
    for (const auto &candidate : candidates) {
        const int distance = candidate.size > requested_size
            ? candidate.size - requested_size
            : requested_size - candidate.size;
        if ((distance < nearest_distance) ||
                ((distance == nearest_distance) &&
                 (nearest == nullptr || candidate.size > nearest->size))) {
            nearest = &candidate;
            nearest_distance = distance;
        }
    }

    if (font != NULL) {
        *font = nearest != nullptr ? nearest->font : LV_FONT_DEFAULT;
    }

    return nearest != nullptr;
}

lv_color_t getLvRandomColor(void)
{
    srand((unsigned int)time(NULL));

    uint8_t r = rand() & 0xff;
    uint8_t g = rand() & 0xff;
    uint8_t b = rand() & 0xff;

    return lv_color_make(r, g, b);
}

lv_indev_t *getLvInputDev(const lv_display_t *display, lv_indev_type_t type)
{
    lv_indev_t *indev = NULL;
    lv_indev_t *indev_tmp = lv_indev_get_next(NULL);

    while (indev_tmp != NULL) {
        if (indev_tmp->disp == display && indev_tmp->type == type) {
            indev = indev_tmp;
            break;
        }
        indev_tmp = lv_indev_get_next(indev_tmp);
    }

    return indev;
}

lv_anim_path_cb_t getLvAnimPathCb(StyleAnimation::AnimationPathType type)
{
    ESP_UTILS_CHECK_FALSE_RETURN(
        type < StyleAnimation::ANIM_PATH_TYPE_MAX, nullptr, "Invalid animation path type(%d)", type
    );

    switch (type) {
    case StyleAnimation::ANIM_PATH_TYPE_LINEAR:
        return lv_anim_path_linear;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_EASE_IN:
        return lv_anim_path_ease_in;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_EASE_OUT:
        return lv_anim_path_ease_out;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_EASE_IN_OUT:
        return lv_anim_path_ease_in_out;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_OVERSHOOT:
        return lv_anim_path_overshoot;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_BOUNCE:
        return lv_anim_path_bounce;
        break;
    case StyleAnimation::ANIM_PATH_TYPE_STEP:
        return lv_anim_path_step;
        break;
    default:
        break;
    }
    ESP_UTILS_LOGE("Invalid animation path type(%d)", type);

    return nullptr;
}

} // namespace esp_brookesia::gui
