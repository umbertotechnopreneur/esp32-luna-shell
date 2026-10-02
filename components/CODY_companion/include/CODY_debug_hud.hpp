#pragma once

#include <cstdint>

#include "CODY2D.hpp"

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
    char frame_rate[20] = "FPS N/A";
    char processors[32] = "CPU1 N/A CPU2 N/A";
};

// measurement: Completed-frame counters owned by the single rendering worker.
// started_at_us: Start of the frame that actually reached the panel.
// completed_at_us: Completion timestamp after the complete frame succeeds.
void record_frame_rate(FrameRateMeasurement &measurement,
                       int64_t started_at_us, int64_t completed_at_us);

// measurement: Measured completed frames; no requested refresh rate is displayed.
// now_us: Observation timestamp used to reject an old FPS window.
// readout: Receives text from the canonical processor snapshot and renderer timing.
esp_err_t prepare_debug_readout(const FrameRateMeasurement &measurement,
                               int64_t now_us, DebugReadout &readout);

// readout: Text prepared once for this whole frame, shared by all strips.
// strip: Full-width RGB565 destination in the canonical 466-pixel canvas.
// first_row: Logical row of the strip, before the display owner's rotation.
esp_err_t draw_debug_readout_rows(const DebugReadout &readout,
                                CODY2D::Surface strip, uint16_t first_row);

}  // namespace CODY
