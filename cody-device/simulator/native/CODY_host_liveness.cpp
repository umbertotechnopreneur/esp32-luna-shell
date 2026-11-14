// Host-side operation ownership only. This is not an ESP Task WDT and never
// publishes a synthetic watchdog reset as a physical-device observation.
#include "LUNASHELL_liveness.h"

#include <array>
#include <mutex>

namespace {
std::mutex s_mutex;
std::array<bool, LUNASHELL_LIVENESS_POINT_COUNT> s_active{};
bool s_initialized = false;

// point: Original firmware domain, validated before using it as an array index.
// Exception: Returns false for an unknown or negative identity.
bool valid_point(LUNASHELL_liveness_point_t point)
{
    return static_cast<int>(point) >= 0 && point < LUNASHELL_LIVENESS_POINT_COUNT;
}
}

// No parameters: initializes host-only ownership tracking; no hardware WDT is armed.
extern "C" esp_err_t LUNASHELL_liveness_init(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_initialized = true;
    return ESP_OK;
}

// point: The original firmware progress domain requesting exclusive ownership.
// Exception: Rejects missing initialization, an invalid point or duplicate admission.
extern "C" esp_err_t LUNASHELL_liveness_watch_begin(LUNASHELL_liveness_point_t point)
{
    if (!valid_point(point)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_active[point]) return ESP_ERR_INVALID_STATE;
    s_active[point] = true;
    return ESP_OK;
}

// point: A currently admitted domain making observable progress.
// Exception: Rejects a missing or invalid owner; does not claim hardware WDT feeding.
extern "C" esp_err_t LUNASHELL_liveness_watch_progress(LUNASHELL_liveness_point_t point)
{
    if (!valid_point(point)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_initialized && s_active[point] ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// point: Previously admitted firmware progress domain.
// operation_result: Original result, returned unchanged after ownership is released.
// Exception: Rejects an invalid or inactive owner without altering other domains.
extern "C" esp_err_t LUNASHELL_liveness_watch_end(LUNASHELL_liveness_point_t point, esp_err_t operation_result)
{
    if (!valid_point(point)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || !s_active[point]) return ESP_ERR_INVALID_STATE;
    s_active[point] = false;
    return operation_result;
}

// status: Unchanged because the host cannot supply a physical watchdog snapshot.
// Exception: Returns NOT_SUPPORTED instead of publishing synthetic hardware health.
extern "C" esp_err_t LUNASHELL_liveness_get_status(LUNASHELL_liveness_status_t *status)
{
    return status ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
}
