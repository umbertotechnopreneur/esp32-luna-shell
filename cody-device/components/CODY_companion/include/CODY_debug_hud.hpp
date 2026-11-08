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
#include "CODY_damage.hpp"
#include "CODY_debug_views.hpp"

namespace CODY {

// These are renderer measurements, not an independent hardware snapshot.
struct FrameRateMeasurement {
    uint32_t completed_frames = 0;
    int64_t window_started_us = 0;
    uint32_t frames_per_second_tenths = 0;
    int64_t measured_at_us = 0;
    bool available = false;
};

struct DebugReadout {
    char frame_rate[20] = "FPS UNAVAILABLE";
    char processors[40] = "CPU1 UNAVAILABLE CPU2 UNAVAILABLE";
    char network[32] = "IP UNAVAILABLE";
    char memory[48] = "RAM UNAVAILABLE PSRAM UNAVAILABLE";
    char storage_state[40] = "SD UNCONFIRMED NO ACCESS";
    char storage_free[32] = "SD FREE UNAVAILABLE";
};

// No parameters: the HUD owns the lower arc below the automatic greeting title.
DamageRect debug_readout_damage_bounds();

// measurement: Completed-frame counters owned by the single rendering worker.
// started_at_us: Start of the frame that actually reached the panel.
// completed_at_us: Completion timestamp after the complete frame succeeds.
void record_frame_rate(FrameRateMeasurement &measurement,
                       int64_t started_at_us, int64_t completed_at_us);

// measurement: Measured completed frames; no requested refresh rate is displayed.
// now_us: Observation timestamp used to reject an old FPS window.
// readout: Receives bounded text from canonical diagnostics and renderer timing.
esp_err_t prepare_debug_readout(const FrameRateMeasurement &measurement,
                               int64_t now_us, DebugReadout &readout);

// readout: Receives only the existing canonical heap measurements and validity.
// Exception: Returns the original snapshot error or INVALID_RESPONSE for incoherent capacities.
esp_err_t prepare_memory_diagnostics(MemoryDiagnosticReadout &readout);

// readout: Text prepared once for this whole frame, shared by all strips.
// strip: Compact RGB565 destination inside the canonical 466-pixel canvas.
// first_row: Logical row of the strip, before the display owner's rotation.
// first_column: Logical column of the strip's first pixel, before rotation.
// background_rgb565: Scene background used to keep the diagnostic text legible.
esp_err_t draw_debug_readout_rows(const DebugReadout &readout,
                                CODY2D::Surface strip, uint16_t first_row,
                                uint16_t first_column, uint16_t background_rgb565);

}  // namespace CODY
