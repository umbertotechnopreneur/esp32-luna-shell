/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

// LVGL boot owner: the approved VibeWare animation precedes System Core startup.
#include "LUNASHELL_boot.h"
#include "VIBEWARE_boot_animation.h"
#include "vibeware_boot_data.h"

#include <stdatomic.h>
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "LUNASHELL_boot";
static lv_obj_t *s_screen;
static lv_obj_t *s_image;
static lv_timer_t *s_animation_timer;
static uint16_t *s_pixels;
static uint16_t s_frame_index;
static bool s_services_ready;
static atomic_bool s_cycle_finished;
static atomic_int s_animation_error;

// error: Original asynchronous rendering failure published to the canonical snapshot.
void VIBEWARE_boot_publish_display_failure(esp_err_t error);

static lv_image_dsc_t s_boot_image = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = VIBEWARE_BOOT_WIDTH,
    .header.h = VIBEWARE_BOOT_HEIGHT,
    .header.stride = VIBEWARE_BOOT_WIDTH * 2,
    .data_size = VIBEWARE_BOOT_WIDTH * VIBEWARE_BOOT_HEIGHT * 2,
};

// operation: Failed boot operation named in component logs.
// error: Original failure retained for startup and the canonical display snapshot.
static esp_err_t report_failure(const char *operation, esp_err_t error)
{
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    atomic_store(&s_animation_error, error);
    VIBEWARE_boot_publish_display_failure(error);
    return error;
}

// No parameters: release only resources owned by this boot UI, with the LVGL lock held.
static void release_boot_resources(void)
{
    if (s_animation_timer != NULL) {
        lv_timer_delete(s_animation_timer);
        s_animation_timer = NULL;
    }
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_image = NULL;
    s_boot_image.data = NULL;
    heap_caps_free(s_pixels);
    s_pixels = NULL;
}

// timer: LVGL-owned callback; no parallel task can replace the displayed image here.
// Exception: Retains and publishes the real error, pauses animation and rejects later handoff.
static void advance_branding(lv_timer_t *timer)
{
    if (atomic_load(&s_animation_error) != ESP_OK) {
        lv_timer_pause(timer);
        return;
    }
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        lv_timer_pause(timer);
        report_failure("VibeWare default display", ESP_ERR_INVALID_STATE);
        return;
    }
    if (s_frame_index + 1 == VIBEWARE_BOOT_FRAME_COUNT && s_services_ready) {
        lv_timer_pause(timer);
        atomic_store(&s_cycle_finished, true);
        return;
    }
    const uint16_t candidate = (s_frame_index + 1) % VIBEWARE_BOOT_FRAME_COUNT;
    const esp_err_t error = VIBEWARE_boot_decode_rows(candidate, 0,
        VIBEWARE_BOOT_HEIGHT, s_pixels, VIBEWARE_BOOT_WIDTH,
        VIBEWARE_BOOT_WIDTH * VIBEWARE_BOOT_HEIGHT, false);
    if (error != ESP_OK) {
        lv_timer_pause(timer);
        report_failure("VibeWare animation decode", error);
        return;
    }
    // The descriptor/storage identity stays fixed; discard its cached decoded representation.
    lv_image_cache_drop(&s_boot_image);
    lv_obj_invalidate(s_image);
    lv_refr_now(display);
    s_frame_index = candidate;
    lv_timer_set_period(timer, VIBEWARE_boot_frames[candidate].duration_ms);
    lv_timer_reset(timer);
}

// No parameters: create the first visible logo and start its original frame cadence.
// Exception: Returns original validation/locking/allocation errors and publishes startup failure.
esp_err_t LUNASHELL_boot_ui_show(void)
{
    if (s_screen != NULL) {
        ESP_LOGE(TAG, "Duplicate boot UI rejected: %s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t asset_error = VIBEWARE_boot_validate();
    if (asset_error != ESP_OK) {
        return report_failure("VibeWare flash resources", asset_error);
    }
    const esp_err_t lock_error = bsp_display_lock(1000);
    if (lock_error != ESP_OK) {
        return report_failure("Boot LVGL lock", lock_error);
    }
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        bsp_display_unlock();
        return report_failure("Boot default display", ESP_ERR_INVALID_STATE);
    }
    const size_t bytes = VIBEWARE_BOOT_WIDTH * VIBEWARE_BOOT_HEIGHT * sizeof(uint16_t);
    s_pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_screen = lv_obj_create(NULL);
    if (s_pixels == NULL || s_screen == NULL) {
        release_boot_resources();
        bsp_display_unlock();
        return report_failure("VibeWare boot allocation", ESP_ERR_NO_MEM);
    }
    s_boot_image.data = (const uint8_t *)s_pixels;
    const esp_err_t decode_error = VIBEWARE_boot_decode_rows(0, 0,
        VIBEWARE_BOOT_HEIGHT, s_pixels, VIBEWARE_BOOT_WIDTH, bytes / sizeof(uint16_t), false);
    if (decode_error != ESP_OK) {
        release_boot_resources();
        bsp_display_unlock();
        return report_failure("VibeWare first frame", decode_error);
    }
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 466, 466);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_image = lv_image_create(s_screen);
    if (s_image == NULL) {
        release_boot_resources();
        bsp_display_unlock();
        return report_failure("VibeWare image allocation", ESP_ERR_NO_MEM);
    }
    lv_image_set_src(s_image, &s_boot_image);
    lv_obj_set_pos(s_image, VIBEWARE_BOOT_LEFT, VIBEWARE_BOOT_TOP);
    lv_obj_clear_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    s_services_ready = false;
    s_frame_index = 0;
    atomic_store(&s_cycle_finished, false);
    atomic_store(&s_animation_error, ESP_OK);
    s_animation_timer = lv_timer_create(advance_branding, VIBEWARE_boot_frames[0].duration_ms, NULL);
    if (s_animation_timer == NULL) {
        release_boot_resources();
        bsp_display_unlock();
        return report_failure("VibeWare timer allocation", ESP_ERR_NO_MEM);
    }
    lv_screen_load(s_screen);
    lv_refr_now(display);
    lv_timer_reset(s_animation_timer);
    bsp_display_unlock();
    ESP_LOGI(TAG, "VibeWare first image online: 42 frames, 3500 ms cycles, flash-owned source");
    return ESP_OK;
}

// No parameters: admit handoff after the current complete animation cycle.
// Exception: Returns the real asynchronous animation, lock or state failure.
esp_err_t LUNASHELL_boot_ui_mark_ready(void)
{
    const esp_err_t lock_error = bsp_display_lock(1000);
    if (lock_error != ESP_OK) {
        return report_failure("Boot readiness lock", lock_error);
    }
    esp_err_t result = atomic_load(&s_animation_error);
    if (result == ESP_OK && (s_screen == NULL || s_animation_timer == NULL)) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        s_services_ready = true;
    }
    bsp_display_unlock();
    return result == ESP_OK ? ESP_OK : report_failure("Boot readiness", result);
}

// No parameters: wait outside the LVGL lock, then repaint black before System Core takes over.
// Exception: Returns original animation/lock errors or an explicit bounded handoff timeout.
esp_err_t LUNASHELL_boot_ui_hide(void)
{
    const esp_err_t admission_error = bsp_display_lock(1000);
    if (admission_error != ESP_OK) {
        return report_failure("Boot handoff admission lock", admission_error);
    }
    const bool admitted = s_screen != NULL && s_services_ready && s_animation_timer != NULL;
    bsp_display_unlock();
    if (!admitted) {
        const esp_err_t original_error = atomic_load(&s_animation_error);
        return report_failure("Boot handoff admission",
            original_error != ESP_OK ? original_error : ESP_ERR_INVALID_STATE);
    }
    const int64_t deadline_us = esp_timer_get_time() + (VIBEWARE_BOOT_DURATION_MS + 5000LL) * 1000LL;
    while (!atomic_load(&s_cycle_finished)) {
        const esp_err_t animation_error = atomic_load(&s_animation_error);
        if (animation_error != ESP_OK) {
            return animation_error;
        }
        if (esp_timer_get_time() >= deadline_us) {
            return report_failure("VibeWare cycle handoff deadline", ESP_ERR_TIMEOUT);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const esp_err_t lock_error = bsp_display_lock(1000);
    if (lock_error != ESP_OK) {
        return report_failure("Boot handoff lock", lock_error);
    }
    const esp_err_t animation_error = atomic_load(&s_animation_error);
    lv_display_t *display = lv_display_get_default();
    if (animation_error != ESP_OK || s_screen == NULL || display == NULL) {
        bsp_display_unlock();
        return report_failure("Boot handoff state",
            animation_error != ESP_OK ? animation_error : ESP_ERR_INVALID_STATE);
    }
    lv_timer_delete(s_animation_timer);
    s_animation_timer = NULL;
    lv_obj_clean(s_screen);
    s_image = NULL;
    lv_obj_invalidate(s_screen);
    lv_refr_now(display);
    lv_image_cache_drop(&s_boot_image);
    s_boot_image.data = NULL;
    heap_caps_free(s_pixels);
    s_pixels = NULL;
    // System Core replaces this active black screen immediately after the handoff.
    s_screen = NULL;
    bsp_display_unlock();
    return ESP_OK;
}
