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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t duration_ms;
    uint16_t palette_count;
    const uint16_t *palette;
    const uint32_t *row_offsets;
    const uint8_t *runs;
    uint32_t runs_size;
} VIBEWARE_boot_frame_t;

// No parameters: accept only complete, bounded flash resources before animation starts.
// Exception: Returns and logs the actual malformed resource error.
esp_err_t VIBEWARE_boot_validate(void);

// frame_index: GIF frame ordinal at this resource/API boundary.
// first_row: First included local artwork row.
// row_count: Number of consecutive artwork rows to decode.
// pixels: Caller-owned destination, readable/writable for capacity_pixels words.
// stride: Destination row spacing in RGB565 pixels, at least the artwork width.
// capacity_pixels: Actual destination capacity from pixels onward.
// panel_order: Exchange RGB565 bytes for the native panel DMA path when true.
// Exception: Returns and logs source, bounds or destination errors without substituting pixels.
esp_err_t VIBEWARE_boot_decode_rows(uint16_t frame_index, uint16_t first_row,
    uint16_t row_count, uint16_t *pixels, uint16_t stride,
    size_t capacity_pixels, bool panel_order);

#ifdef __cplusplus
}
#endif
