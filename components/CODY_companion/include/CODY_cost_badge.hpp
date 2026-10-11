#pragma once

#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

// band: Typed host-classified Standard reference; labels never imply a chat bill or model capability.
constexpr const char *cost_band_text(LUNASHELL::CodexCostBand band)
{
    using Band = LUNASHELL::CodexCostBand;
    switch (band) {
    case Band::Economy: return "Cheap";
    case Band::Balanced: return "Reasonable price";
    case Band::Expensive: return "Expensive";
    default: return "Unavailable";
    }
}

// band: Current typed reference; green/orange/red communicate the qualitative cost progression.
constexpr uint16_t cost_band_color(LUNASHELL::CodexCostBand band)
{
    using Band = LUNASHELL::CodexCostBand;
    switch (band) {
    case Band::Economy: return 0x87EF;
    case Band::Balanced: return 0xFD20;
    case Band::Expensive: return 0xF986;
    default: return 0xBDF7;
    }
}

struct ObservedModelCost {
    const char *model = "";
    LUNASHELL::CodexCostBand band = LUNASHELL::CodexCostBand::Unknown;
};

// link: Coherent canonical link/activity with original validity and freshness.
// technical: Canonical observation already aged by its acquisition/snapshot owner.
// Returns borrowed current model metadata and its independent catalog band; absence stays Unknown.
inline ObservedModelCost observed_model_cost(const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical)
{
    if (!link.data_valid || link.data_stale || link.state != LUNASHELL::CodexLinkState::Connected ||
        !link.activity.data_valid || link.activity.data_stale || !technical.data_valid || technical.data_stale ||
        technical.revision != link.activity.revision || technical.chat_id[0] == '\0' ||
        std::strcmp(technical.chat_id, link.activity.chat_id) != 0 ||
        !technical.model_valid || technical.model_stale ||
        technical.model_source != LUNASHELL::CodexModelSource::HookMetadata) {
        return {};
    }
    ObservedModelCost result{technical.model, LUNASHELL::CodexCostBand::Unknown};
    if (!technical.cost_stale && technical.cost_valid_for_us != 0 &&
        technical.cost_source == LUNASHELL::CodexCostSource::CodexStandardCatalog) {
        result.band = technical.cost_band;
    }
    return result;
}

} // namespace CODY
