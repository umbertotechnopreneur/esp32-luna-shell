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

#include "VIBEWARE_boot_animation.h"
#include "vibeware_boot_data.h"
#include "esp_log.h"

static const char *TAG = "VIBEWARE_boot";

// operation: Rejected resource operation.
// error: Original or specific structured error retained by the caller.
static esp_err_t reject(const char *operation, esp_err_t error)
{
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    return error;
}

// No parameters: validate the immutable package once before acquiring render resources.
// Exception: Returns a specific metadata, offset, run or palette error.
esp_err_t VIBEWARE_boot_validate(void)
{
    uint32_t duration_ms = 0;
    for (uint16_t index = 0; index < VIBEWARE_BOOT_FRAME_COUNT; ++index) {
        const VIBEWARE_boot_frame_t *frame = &VIBEWARE_boot_frames[index];
        if (frame->duration_ms == 0 || frame->palette_count == 0 ||
            frame->palette_count > 256 || frame->palette == NULL ||
            frame->row_offsets == NULL || frame->runs == NULL) {
            ESP_LOGE(TAG, "Rejected frame=%u duration=%u palette_count=%u", index,
                frame->duration_ms, frame->palette_count);
            return reject("Frame metadata", ESP_ERR_INVALID_ARG);
        }
        if (frame->row_offsets[0] != 0 ||
            frame->row_offsets[VIBEWARE_BOOT_HEIGHT] != frame->runs_size) {
            return reject("Frame terminal offsets", ESP_ERR_INVALID_SIZE);
        }
        for (uint16_t row = 0; row < VIBEWARE_BOOT_HEIGHT; ++row) {
            const uint32_t start = frame->row_offsets[row];
            const uint32_t end = frame->row_offsets[row + 1];
            if (end < start || end > frame->runs_size || (end - start) % 3 != 0) {
                return reject("Row bounds", ESP_ERR_INVALID_SIZE);
            }
            uint32_t width = 0;
            for (uint32_t offset = start; offset < end; offset += 3) {
                const uint16_t count = frame->runs[offset] | ((uint16_t)frame->runs[offset + 1] << 8);
                if (count == 0 || count > VIBEWARE_BOOT_WIDTH - width ||
                    frame->runs[offset + 2] >= frame->palette_count) {
                    ESP_LOGE(TAG, "Rejected frame=%u row=%u count=%u width=%lu palette_index=%u",
                        index, row, count, (unsigned long)width, frame->runs[offset + 2]);
                    return reject("Row run", ESP_ERR_INVALID_RESPONSE);
                }
                width += count;
            }
            if (width != VIBEWARE_BOOT_WIDTH) {
                return reject("Decoded row width", ESP_ERR_INVALID_SIZE);
            }
        }
        duration_ms += frame->duration_ms;
    }
    if (duration_ms != VIBEWARE_BOOT_DURATION_MS) {
        return reject("Cycle duration", ESP_ERR_INVALID_SIZE);
    }
    return ESP_OK;
}

// frame_index: GIF resource ordinal; never a character or route identity.
// first_row: First included artwork row.
// row_count: Local rows to write.
// pixels: Exclusively borrowed destination storage.
// stride: Actual destination spacing in pixels.
// capacity_pixels: Available words from the provided destination address.
// panel_order: True only for already leased panel-order DMA storage.
// Exception: Returns real bounds/resource errors before reporting a successful decode.
esp_err_t VIBEWARE_boot_decode_rows(uint16_t frame_index, uint16_t first_row,
    uint16_t row_count, uint16_t *pixels, uint16_t stride,
    size_t capacity_pixels, bool panel_order)
{
    if (frame_index >= VIBEWARE_BOOT_FRAME_COUNT || pixels == NULL || row_count == 0 ||
        first_row >= VIBEWARE_BOOT_HEIGHT || row_count > VIBEWARE_BOOT_HEIGHT - first_row ||
        stride < VIBEWARE_BOOT_WIDTH ||
        capacity_pixels < (size_t)(row_count - 1) * stride + VIBEWARE_BOOT_WIDTH) {
        ESP_LOGE(TAG, "Rejected frame=%u first_row=%u rows=%u stride=%u capacity=%u pixels=%p",
            frame_index, first_row, row_count, stride, (unsigned)capacity_pixels, (void *)pixels);
        return reject("Decode destination bounds", ESP_ERR_INVALID_ARG);
    }
    const VIBEWARE_boot_frame_t *frame = &VIBEWARE_boot_frames[frame_index];
    for (uint16_t row = 0; row < row_count; ++row) {
        uint16_t *destination = pixels + (size_t)row * stride;
        const uint32_t start = frame->row_offsets[first_row + row];
        const uint32_t end = frame->row_offsets[first_row + row + 1];
        uint32_t width = 0;
        if (end < start || end > frame->runs_size || (end - start) % 3 != 0) {
            return reject("Decode source bounds", ESP_ERR_INVALID_SIZE);
        }
        for (uint32_t offset = start; offset < end; offset += 3) {
            const uint16_t count = frame->runs[offset] | ((uint16_t)frame->runs[offset + 1] << 8);
            const uint8_t palette_index = frame->runs[offset + 2];
            if (count == 0 || count > VIBEWARE_BOOT_WIDTH - width || palette_index >= frame->palette_count) {
                return reject("Decode run", ESP_ERR_INVALID_RESPONSE);
            }
            uint16_t color = frame->palette[palette_index];
            if (panel_order) {
                color = (uint16_t)((color << 8) | (color >> 8));
            }
            for (uint16_t pixel = 0; pixel < count; ++pixel) {
                destination[width++] = color;
            }
        }
        if (width != VIBEWARE_BOOT_WIDTH) {
            return reject("Decode row width", ESP_ERR_INVALID_SIZE);
        }
    }
    return ESP_OK;
}
