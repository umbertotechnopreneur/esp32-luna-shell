#include "qsee_distance_service.hpp"

#include "qsee_platform.h"

namespace qsee {

esp_err_t distanceServiceInit()
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t distanceStartCalibration()
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t distanceSetSampleCount(uint16_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t distanceSetSampleIntervalMs(uint32_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t distanceSetReferenceMm(uint16_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// Report the unavailable distance lifecycle when this build omits its driver.
esp_err_t distanceSetPowerSuspended(bool)
{
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace qsee

extern "C" esp_err_t qsee_distance_start_calibration(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_distance_set_sample_count(uint16_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_distance_set_sample_interval_ms(uint32_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_distance_set_reference_mm(uint16_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// Expose the same unsupported power lifecycle through the C platform API.
extern "C" esp_err_t qsee_distance_set_power_suspended(bool)
{
    return ESP_ERR_NOT_SUPPORTED;
}
