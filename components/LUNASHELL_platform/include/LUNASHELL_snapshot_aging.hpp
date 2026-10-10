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

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// component: Original owner-provided measurement whose numeric history remains untouched.
// now_us: Explicit monotonic observation time, independent of acquisition.
// ttl_us: Maximum age admitted by the caller's acquisition policy.
void age_measured_sample(ComponentSnapshot &component, int64_t now_us, int64_t ttl_us);

// snapshot: Canonical cadence and validity used by full and bounded projections.
int64_t measured_sample_ttl_us(const DeviceSnapshot &snapshot);

// snapshot: Owner-provided observations; original sample timestamps remain unchanged.
// now_us: Explicit monotonic observation time; no clock or hardware access occurs here.
void apply_measured_sample_aging(DeviceSnapshot &snapshot, int64_t now_us);

// technical: Owner projection retaining original source ages and receipt timestamp.
// now_us: Device observation clock, used only for elapsed time since receipt.
void age_codex_technical(CodexTechnicalSnapshot &technical, int64_t now_us);
}
