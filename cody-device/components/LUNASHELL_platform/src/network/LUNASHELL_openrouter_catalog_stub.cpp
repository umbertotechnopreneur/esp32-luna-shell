#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// No parameters: this build profile has no catalog network worker.
// Exception: Explicitly reports that the feature is unavailable.
esp_err_t start_openrouter_catalog() { return ESP_ERR_NOT_SUPPORTED; }

// status: Caller-owned metadata, marked unavailable for this build.
// Exception: Explicitly reports that the feature is unavailable.
esp_err_t DeviceSnapshotService::readOpenRouterCatalogStatus(OpenRouterCatalogStatus &status) const
{
    status = {};
    status.last_error = ESP_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

// generation, offset, output, capacity: Unused in a build without the worker.
// written: Set to zero because no data was read.
// Exception: Explicitly reports that the feature is unavailable.
esp_err_t DeviceSnapshotService::readOpenRouterCatalogPage(uint32_t generation, size_t offset,
    OpenRouterModelQuote *output, size_t capacity, size_t &written) const
{
    (void)generation; (void)offset; (void)output; (void)capacity;
    written = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
} // namespace LUNASHELL
