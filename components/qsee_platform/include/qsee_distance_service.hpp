#pragma once

#include <cstdint>

#include "esp_err.h"

namespace qsee {

/**
 * Initialize persisted precision-distance settings, the dual TOF400F
 * transport, and the single acquisition/calibration worker.
 */
esp_err_t distanceServiceInit();

/** Schedule one non-blocking calibration with the currently saved settings. */
esp_err_t distanceStartCalibration();

/** Persist and apply the number of accepted readings in each robust window. */
esp_err_t distanceSetSampleCount(uint16_t sample_count);

/** Persist and apply the physical interval between TOF400F requests. */
esp_err_t distanceSetSampleIntervalMs(uint32_t interval_ms);

/** Persist the physically measured calibration distance. */
esp_err_t distanceSetReferenceMm(uint16_t reference_mm);

/** Quiesce acquisition and release UART ownership while power is suspended. */
esp_err_t distanceSetPowerSuspended(bool suspended);

}  // namespace qsee
