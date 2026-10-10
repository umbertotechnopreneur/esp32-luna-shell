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

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// health: Explicit canonical health identity; unknown remains unknown.
const char *deviceHealthName(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Ready:
        return "ready";
    case DeviceHealth::Degraded:
        return "degraded";
    case DeviceHealth::Missing:
        return "missing";
    case DeviceHealth::Unknown:
    default:
        return "unknown";
    }
}

}
