/*
 * LUNASHELL.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 LUNASHELL.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * LUNASHELL.ai: https://LUNASHELL.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@LUNASHELL.ai
 */

// Boot screen lifecycle and readiness handshake for LUNASHELL.
// Shows the splash UI until the rest of the stack is ready.

#include "LUNASHELL_boot.h"

#include <stdbool.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"

extern const uint8_t LUNASHELL_boot_rgb565_start[] asm("_binary_LUNASHELL_boot_rgb565_start");
extern const uint8_t LUNASHELL_boot_rgb565_end[] asm("_binary_LUNASHELL_boot_rgb565_end");

static const char *TAG = "LUNASHELL_boot";
static lv_obj_t *s_screen;
static lv_obj_t *s_continue_label;

static lv_image_dsc_t s_boot_image = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = 466,
    .header.h = 466,
    .header.stride = 466 * 2,
    .data_size = 466 * 466 * 2,
    .data = LUNASHELL_boot_rgb565_start,
};

typedef struct {
    int inset_px;
    int border_width_px;
    uint32_t color_hex;
    lv_opa_t border_opa;
} boot_ring_layer_t;

static void create_boot_glow_ring(lv_obj_t *parent)
{
    const boot_ring_layer_t layers[] = {
        {
            .inset_px = 4,
            .border_width_px = 2,
            .color_hex = 0x143E78,
            .border_opa = LV_OPA_40,
        },
        {
            .inset_px = 7,
            .border_width_px = 3,
            .color_hex = 0x258CFF,
            .border_opa = LV_OPA_80,
        },
        {
            .inset_px = 10,
            .border_width_px = 2,
            .color_hex = 0x9FE8FF,
            .border_opa = LV_OPA_COVER,
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
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

esp_err_t LUNASHELL_boot_ui_show(void)
{
    if (s_screen != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Boot UI: acquiring LVGL lock");
    const esp_err_t lock_result = bsp_display_lock(1000);
    if (lock_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Boot UI: LVGL lock failed: %s",
            esp_err_to_name(lock_result)
        );
        return lock_result;
    }

    ESP_LOGI(TAG, "Boot UI: creating screen");
    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "Boot UI: screen allocation failed");
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 466, 466);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(
        s_screen,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE
    );

    ESP_LOGI(TAG, "Boot UI: attaching RGB565 branding");
    lv_obj_t *image = lv_image_create(s_screen);
    if (image == NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
        bsp_display_unlock();
        ESP_LOGE(TAG, "Boot UI: image allocation failed");
        return ESP_ERR_NO_MEM;
    }
    lv_image_set_src(image, &s_boot_image);
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

    create_boot_glow_ring(s_screen);

    s_continue_label = lv_label_create(s_screen);
    if (s_continue_label == NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
        bsp_display_unlock();
        ESP_LOGE(TAG, "Boot UI: label allocation failed");
        return ESP_ERR_NO_MEM;
    }
    lv_obj_align(s_continue_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_text_color(s_continue_label, lv_color_hex(0x2F80ED), 0);
    lv_obj_set_style_text_font(s_continue_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(s_continue_label, "INITIALIZING");

    ESP_LOGI(TAG, "Boot UI: loading screen");
    lv_screen_load(s_screen);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Minimal boot screen online; canonical logs remain off-screen");
    return ESP_OK;
}

esp_err_t LUNASHELL_boot_ui_mark_ready(void)
{
    if (s_screen == NULL || s_continue_label == NULL) {
        ESP_LOGE(TAG, "Boot UI cannot transition before it is initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result = bsp_display_lock(1000);
    if (lock_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Boot UI ready transition lock failed: %s",
            esp_err_to_name(lock_result)
        );
        return lock_result;
    }

    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "Boot UI ready transition has no default display");
        return ESP_ERR_INVALID_STATE;
    }
    lv_label_set_text(s_continue_label, "OPENING WELCOME");
    lv_refr_now(display);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Startup complete; opening Welcome");
    return ESP_OK;
}

esp_err_t LUNASHELL_boot_ui_hide(void)
{
    if (s_screen == NULL) {
        ESP_LOGE(TAG, "Boot UI cannot hide before it is initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result = bsp_display_lock(1000);
    if (lock_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Boot UI hide lock failed: %s",
            esp_err_to_name(lock_result)
        );
        return lock_result;
    }

    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        bsp_display_unlock();
        ESP_LOGE(TAG, "Boot UI hide has no default display");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Brookesia replaces this screen immediately after this call. Clean and
     * repaint it first so the AMOLED does not retain untouched strips from the
     * full-screen RGB565 boot artwork around the round app area.
     */
    lv_obj_clean(s_screen);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 466, 466);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_invalidate(s_screen);
    lv_refr_now(display);
    s_screen = NULL;
    s_continue_label = NULL;
    bsp_display_unlock();
    return ESP_OK;
}
