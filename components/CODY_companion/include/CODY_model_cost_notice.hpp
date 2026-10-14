#pragma once

#include <cstring>
#include "CODY_technical_panels.hpp"
#include "esp_log.h"

namespace CODY {

enum class ModelCostAudioCue : uint32_t { Expensive = 0x43434D01U };

// Display-owned identity, not a sensor cache; source loss never rearms an attempted notice.
struct ModelCostNoticeState {
    char model[129]{};
    bool classified = false;
    bool pending = false;
};
static_assert(sizeof(ModelCostNoticeState) <= 132, "Model notice retains one bounded identity only");

// inputs: Current canonical projection with independently admitted model and cost facts.
// price_retired_visible: A complete, unpaused Home Model panel displayed the current price.
// state: Last observed model and its single pending notice, preserved across carousel and standby.
// notify: Receives one claim; busy, muted or failed audio must not rearm it.
// Exception: Logs and returns malformed current model/cost metadata without changing the latch.
inline esp_err_t advance_model_cost_notice(const PanelInputs &inputs, bool price_retired_visible,
    ModelCostNoticeState &state, bool &notify)
{
    notify = false;
    if (inputs.model_state != PanelValueState::Current) { return ESP_OK; }
    size_t length = 0;
    if (inputs.model != nullptr) {
        while (length < sizeof(state.model) && inputs.model[length] != '\0') {
            const unsigned char character = static_cast<unsigned char>(inputs.model[length]);
            if (character < 32 || character > 126) { break; }
            ++length;
        }
    }
    if (inputs.model == nullptr || length == 0 || length >= sizeof(state.model) ||
        inputs.model[length] != '\0' || inputs.cost_band > LUNASHELL::CodexCostBand::Expensive) {
        ESP_LOGE("CODY_model_cost", "Current model/cost rejected: length=%u band=%u (%s)",
            static_cast<unsigned>(length), static_cast<unsigned>(inputs.cost_band),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(state.model, inputs.model) != 0) {
        // A newer model supersedes any unseen notice, even before its catalog price arrives.
        std::memcpy(state.model, inputs.model, length + 1);
        state.classified = false;
        state.pending = false;
    }
    const bool cost_current = inputs.cost_state == PanelValueState::Current &&
        inputs.cost_band != LUNASHELL::CodexCostBand::Unknown;
    if (!cost_current) { return ESP_OK; }
    if (!state.classified) {
        state.classified = true;
        state.pending = inputs.cost_band == LUNASHELL::CodexCostBand::Expensive;
    } else if (inputs.cost_band != LUNASHELL::CodexCostBand::Expensive) {
        state.pending = false;
    }
    if (price_retired_visible && state.pending) {
        state.pending = false;
        notify = true;
    }
    return ESP_OK;
}

} // namespace CODY
