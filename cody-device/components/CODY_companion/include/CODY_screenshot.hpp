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

#include <cstdint>

#include "CODY2D.hpp"
#include "esp_err.h"

namespace CODY {

enum class ScreenshotState : uint8_t {
    Idle,
    Capturing,
    Saving,
    Saved,
    Failed,
};

// One temporary full frame assembled from the renderer's bounded DMA strips.
struct ScreenshotCapture {
    uint16_t *pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t stride = 0;
    uint32_t capture_id = 0;
};

// No parameters: return the current asynchronous capture/write feedback state.
ScreenshotState screenshot_state();

// No parameters: return the original error from the latest failed request.
esp_err_t screenshot_error();

// No parameters: admit one new on-demand request when no capture or write is active.
// Exception: Returns ESP_ERR_NOT_SUPPORTED outside Debug or ESP_ERR_INVALID_STATE while busy.
esp_err_t request_screenshot();

// width: Complete logical canvas width.
// height: Complete logical canvas height.
// capture: Receives temporary PSRAM ownership when a request is pending.
// Exception: Returns ESP_ERR_NOT_FOUND when idle, or the real allocation/geometry error.
esp_err_t begin_screenshot_capture(
    uint16_t width,
    uint16_t height,
    ScreenshotCapture &capture
);

// capture: Active temporary frame receiving this composed strip.
// strip: Complete-width panel-order RGB565 strip after every visual layer.
// first_row: Logical destination row in the complete capture.
// first_column: Logical destination column; captures require zero.
// Exception: Returns the real surface, geometry or state error.
esp_err_t capture_screenshot_rows(
    const ScreenshotCapture &capture,
    CODY2D::Surface strip,
    uint16_t first_row,
    uint16_t first_column
);

// capture: Active temporary frame; always cleared and released or transferred.
// frame_error: Completed-frame result; a failed frame can never become a screenshot.
// Exception: Returns the frame error or real storage queue failure.
esp_err_t finish_screenshot_capture(
    ScreenshotCapture &capture,
    esp_err_t frame_error
);

}  // namespace CODY
