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

#include "qsee_boot.h"

#include <stdbool.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "qsee_logger.h"

extern const uint8_t qsee_boot_rgb565_start[] asm("_binary_qsee_boot_rgb565_start");
extern const uint8_t qsee_boot_rgb565_end[] asm("_binary_qsee_boot_rgb565_end");

static const char *TAG = "qsee_boot";
static lv_obj_t *s_screen;
static lv_obj_t *s_log_rich_text;
static lv_obj_t *s_continue_label;
static lv_timer_t *s_log_timer;
static SemaphoreHandle_t s_continue_semaphore;
static StaticSemaphore_t s_continue_semaphore_control;
static bool s_ready;

static lv_image_dsc_t s_boot_image = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = 466,
    .header.h = 466,
    .header.stride = 466 * 2,
    .data_size = 466 * 466 * 2,
    .data = qsee_boot_rgb565_start,
};

typedef struct {
    int inset_px;
    int border_width_px;
    uint32_t color_hex;
    lv_opa_t border_opa;
    int shadow_width_px;
    int shadow_spread_px;
    lv_opa_t shadow_opa;
} boot_ring_layer_t;

static void create_boot_glow_ring(lv_obj_t *parent)
{
    const boot_ring_layer_t layers[] = {
        {
            .inset_px = 4,
            .border_width_px = 2,
            .color_hex = 0x143E78,
            .border_opa = LV_OPA_40,
            .shadow_width_px = 18,
            .shadow_spread_px = 1,
            .shadow_opa = LV_OPA_30,
        },
        {
            .inset_px = 7,
            .border_width_px = 3,
            .color_hex = 0x258CFF,
            .border_opa = LV_OPA_80,
            .shadow_width_px = 12,
            .shadow_spread_px = 1,
            .shadow_opa = LV_OPA_40,
        },
        {
            .inset_px = 10,
            .border_width_px = 2,
            .color_hex = 0x9FE8FF,
            .border_opa = LV_OPA_COVER,
            .shadow_width_px = 0,
            .shadow_spread_px = 0,
            .shadow_opa = LV_OPA_TRANSP,
        },
    };

    for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
        const boot_ring_layer_t *layer = &layers[i];
        lv_obj_t *ring = lv_obj_create(parent);
        const int diameter = 466 - (layer->inset_px * 2);

        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, diameter, diameter);
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, layer->border_width_px, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(layer->color_hex), 0);
        lv_obj_set_style_border_opa(ring, layer->border_opa, 0);
        lv_obj_set_style_shadow_color(ring, lv_color_hex(layer->color_hex), 0);
        lv_obj_set_style_shadow_width(ring, layer->shadow_width_px, 0);
        lv_obj_set_style_shadow_spread(ring, layer->shadow_spread_px, 0);
        lv_obj_set_style_shadow_opa(ring, layer->shadow_opa, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

static lv_color_t log_level_color(const char *line, size_t length)
{
    size_t first = 0;
    while (first < length && (line[first] == ' ' || line[first] == '\t')) {
        ++first;
    }

    if (first < length && (line[first] == 'W' || line[first] == 'w')) {
        return lv_color_hex(0xF6AD55);
    }
    if (first < length && (line[first] == 'E' || line[first] == 'e' ||
                           line[first] == 'A' || line[first] == 'a')) {
        return lv_color_hex(0xFC8181);
    }
    if (first < length && (line[first] == 'I' || line[first] == 'i')) {
        return lv_color_hex(0x68D391);
    }
    if (first < length && (line[first] == 'D' || line[first] == 'd')) {
        return lv_color_hex(0xA0AEC0);
    }
    return lv_color_hex(0xEAF1F6);
}

static void refresh_log_label(lv_timer_t *timer)
{
    (void)timer;
    char snapshot[1100];
    qsee_logger_snapshot(snapshot, sizeof(snapshot), 7);
    if (s_log_rich_text == NULL) {
        return;
    }

    while (lv_spangroup_get_span_count(s_log_rich_text) > 0U) {
        lv_span_t *span = lv_spangroup_get_child(s_log_rich_text, 0);
        if (span == NULL) {
            break;
        }
        lv_spangroup_delete_span(s_log_rich_text, span);
    }

    const size_t length = strlen(snapshot);
    size_t line_start = 0;
    while (line_start < length || (length == 0U && line_start == 0U)) {
        size_t line_end = line_start;
        while (line_end < length && snapshot[line_end] != '\n') {
            ++line_end;
        }

        char line[256];
        const size_t line_length = line_end - line_start;
        const size_t copy_length = line_length < sizeof(line) - 2U
            ? line_length : sizeof(line) - 2U;
        memcpy(line, snapshot + line_start, copy_length);
        line[copy_length] = '\n';
        line[copy_length + 1U] = '\0';

        lv_span_t *span = lv_spangroup_add_span(s_log_rich_text);
        if (span == NULL) {
            break;
        }
        lv_span_set_text(span, line);
        lv_style_set_text_color(lv_span_get_style(span),
                                log_level_color(line, copy_length));
        lv_style_set_text_font(lv_span_get_style(span), &lv_font_unscii_8);

        if (line_end >= length) {
            break;
        }
        line_start = line_end + 1U;
    }
    lv_spangroup_refresh(s_log_rich_text);
}

static void boot_touch_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (!s_ready) {
        ESP_LOGI(TAG, "Touch received; startup still in progress");
        return;
    }

    ESP_LOGI(TAG, "Touch confirmed; opening ESP-Brookesia");
    xSemaphoreGive(s_continue_semaphore);
}

esp_err_t qsee_boot_ui_show(void)
{
    if (s_screen != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_continue_semaphore = xSemaphoreCreateBinaryStatic(&s_continue_semaphore_control);
    if (s_continue_semaphore == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Boot UI: acquiring LVGL lock");
    if (bsp_display_lock(1000) != ESP_OK) {
        ESP_LOGE(TAG, "Boot UI: LVGL lock timed out");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Boot UI: creating screen");
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 466, 466);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, boot_touch_event, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Boot UI: attaching RGB565 branding");
    lv_obj_t *image = lv_image_create(s_screen);
    lv_image_set_src(image, &s_boot_image);
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    create_boot_glow_ring(s_screen);

    ESP_LOGI(TAG, "Boot UI: creating canonical log panel");
    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_pos(panel, 73, 222);
    lv_obj_set_size(panel, 320, 152);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x071A2C), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2F80ED), 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    s_log_rich_text = lv_spangroup_create(panel);
    lv_obj_set_width(s_log_rich_text, 294);
    lv_obj_set_height(s_log_rich_text, 128);
    lv_spangroup_set_overflow(s_log_rich_text, LV_SPAN_OVERFLOW_CLIP);
    lv_obj_set_scroll_dir(s_log_rich_text, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_log_rich_text, LV_SCROLLBAR_MODE_AUTO);

    s_continue_label = lv_label_create(s_screen);
    lv_obj_align(s_continue_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_text_color(s_continue_label, lv_color_hex(0x2F80ED), 0);
    lv_obj_set_style_text_font(s_continue_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_continue_label, "INITIALIZING");

    ESP_LOGI(TAG, "Boot UI: loading screen");
    s_log_timer = lv_timer_create(refresh_log_label, 180, NULL);
    refresh_log_label(NULL);
    lv_screen_load(s_screen);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Boot screen online: QSee.ai + canonical live log");
    return ESP_OK;
}

void qsee_boot_ui_mark_ready(void)
{
    s_ready = true;
    if (bsp_display_lock(1000) == ESP_OK) {
        if (s_continue_label != NULL) {
            lv_label_set_text(s_continue_label, "OPENING WELCOME");
        }
        bsp_display_unlock();
    }
    ESP_LOGI(TAG, "Startup complete; touch to continue");
}

void qsee_boot_ui_wait_for_continue(void)
{
    if (s_continue_semaphore != NULL) {
        xSemaphoreTake(s_continue_semaphore, portMAX_DELAY);
    }
}

void qsee_boot_ui_hide(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    if (s_log_timer != NULL) {
        lv_timer_delete(s_log_timer);
        s_log_timer = NULL;
    }
    if (s_screen != NULL) {
        lv_obj_delete_async(s_screen);
        s_screen = NULL;
    }
    s_log_rich_text = NULL;
    s_continue_label = NULL;
    bsp_display_unlock();
}
