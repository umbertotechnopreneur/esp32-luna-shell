#pragma once

#include <algorithm>
#include "CODY_response.hpp"

namespace CODY {

// away: True only for an explicitly confirmed Away preference or a labeled host preview.
// config: Artistic response settings; sensor gains, freshness and blink timing remain intact.
// contour: Authored outline targets, independent from acquired measurements.
// inputs: Presentation eligibility; actual sensor values and stamps remain intact.
inline void apply_away_rhythm(bool away, ResponseConfig &config,
    ContourControls &contour, ResponseInputs &inputs)
{
    if (!away) {
        return;
    }
    // Keep the regular gesture service cadence and all semantic activity overlays.
    // This preference calms optional character motion; it makes no power-service promise.
    inputs.idle_scene_allowed = false;
    config.expression.base_breath_hz = std::min(config.expression.base_breath_hz, .15f);
    config.expression.stress_breath_gain *= .25f;
    contour[ContourControl::Amplitude] *= .35f;
    contour[ContourControl::Speed] *= .5f;
}

} // namespace CODY
