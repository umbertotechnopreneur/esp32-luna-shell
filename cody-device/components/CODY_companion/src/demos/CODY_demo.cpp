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

#include "CODY_demo.hpp"
#include "runtime_settings.hpp"

namespace CODY {
// elapsed_us: Monotonic active animation time, never a synthetic hardware sample.
// face: Authored targets; the regular face transition owns interpolation.
// contour: Authored targets; the regular contour transition owns phase continuity.
// phase: Receives the current authored phase on success.
esp_err_t demo_targets(int64_t elapsed_us, Channels &face, ContourControls &contour, DemoPhase &phase)
{
    if (elapsed_us < 0 || RuntimeData::cycle_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint64_t elapsed_ms = static_cast<uint64_t>(elapsed_us) / 1000U;
    uint32_t phase_ms = elapsed_ms % RuntimeData::cycle_ms;
    for (const DemoStep &step : RuntimeData::steps) {
        if (phase_ms < step.duration_ms) {
            face = step.face;
            contour = contour_defaults();
            contour[ContourControl::Amplitude] = step.amplitude;
            contour[ContourControl::Speed] = step.speed;
            phase = step.identity;
            return ESP_OK;
        }
        phase_ms -= step.duration_ms;
    }
    // Generated durations are strictly positive and sum exactly to cycle_ms.
    return ESP_ERR_INVALID_STATE;
}
} // namespace CODY
