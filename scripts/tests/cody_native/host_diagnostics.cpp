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

#include "host_diagnostics.hpp"

namespace HostDiagnostics {
LUNASHELL::DiagnosticsObservation observation;
esp_err_t read_error = ESP_OK;
}  // namespace HostDiagnostics

namespace LUNASHELL {

// No parameters: this native check links a host-only canonical projection fixture.
DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService service;
    return service;
}

// observation: Receives the fixture only after its explicitly configured read succeeds.
// Exception: Returns the explicitly injected canonical read error without changing the caller's observation.
esp_err_t DeviceSnapshotService::readDiagnosticsObservation(
    DiagnosticsObservation &observation
) const
{
    if (HostDiagnostics::read_error != ESP_OK) {
        return HostDiagnostics::read_error;
    }
    observation = HostDiagnostics::observation;
    return ESP_OK;
}

}  // namespace LUNASHELL
