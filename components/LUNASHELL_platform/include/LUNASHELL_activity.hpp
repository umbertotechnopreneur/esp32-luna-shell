#pragma once
#include <cstdint>
#include "esp_err.h"

namespace LUNASHELL {
// These are the pretrained network's original labels, not Cody context claims.
enum class ActivityClass : uint8_t { Unknown, Downstairs, Jogging, Sitting, Standing, Upstairs, Walking };
enum class ActivityRejection : uint8_t { Disabled, Collecting, MissingSample, CadenceGap, Busy, ModelError, None, Suspended };
struct ActivitySnapshot {
    ActivityClass activity = ActivityClass::Unknown;
    ActivityRejection rejection = ActivityRejection::Disabled;
    bool scores_valid = false;
    // The demonstration has no Cody placement validation or calibrated probabilities.
    bool domain_validated = false;
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    uint32_t sequence = 0;
    uint32_t stable_windows = 0;
    int64_t window_start_us = 0;
    int64_t window_end_us = 0;
    int64_t inferred_at_us = 0;
    uint32_t inference_duration_us = 0;
    float scores[6] = {};
    char model_id[32] = "har-demo-bb27ee3-w16a16";
};
} // namespace LUNASHELL
