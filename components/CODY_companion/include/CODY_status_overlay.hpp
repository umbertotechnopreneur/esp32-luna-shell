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
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class StatusTone : uint8_t {
    Ready,
    Warning,
    Missing,
    Fault,
    Muted,
    Active,
};

// Prepared text and icon semantics contain no renderer or hardware ownership.
struct StatusOverlay {
    char date[16] = "TIME";
    char time[16] = "UNAVAILABLE"; // Current local HH:MM, or an explicit unavailable state.
    char battery[16] = "UNAVAILABLE";
    StatusTone clock_tone = StatusTone::Muted;
    StatusTone battery_tone = StatusTone::Muted;
    StatusTone wifi_tone = StatusTone::Muted;
    StatusTone bluetooth_tone = StatusTone::Muted;
    uint8_t battery_percent = 0;
    uint8_t wifi_bars = 0;
    bool battery_percent_valid = false;
    bool battery_present = false;
    bool battery_charging = false;
    bool bluetooth_active = false;
};

// No parameters: return the complete half-open footprint shared by damage planning.
DamageRect status_overlay_damage_bounds();

// observation: Coherent canonical battery, local calendar, Wi-Fi and BLE state.
// overlay: Receives a complete truthful presentation only after bounded formatting.
// Exception: Returns a formatting or snapshot-contract error without plausible fallback values.
esp_err_t prepare_status_overlay(
    const LUNASHELL::DeviceStatusObservation &observation,
    StatusOverlay &overlay
);

// left: Previously completed presentation.
// right: Current prepared presentation.
bool status_overlay_equal(const StatusOverlay &left, const StatusOverlay &right);

// overlay: Prepared immutable text and semantic icon state.
// opacity: Shared touch-chrome fade from transparent zero to opaque 255.
// background_rgb565: Authored scene color beneath the informational chrome.
// strip: Compact RGB565 destination owned by the standalone renderer.
// first_row: Logical row represented by the strip's first pixel row.
// first_column: Logical column represented by the strip's first pixel.
// Exception: Returns the real surface, geometry or glyph error.
esp_err_t draw_status_overlay_rows(
    const StatusOverlay &overlay,
    uint8_t opacity,
    uint16_t background_rgb565,
    CODY2D::Surface strip,
    uint16_t first_row,
    uint16_t first_column = 0
);

}  // namespace CODY
