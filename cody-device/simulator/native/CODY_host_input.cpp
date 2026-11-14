#include "LUNASHELL_platform.h"
#include "esp_timer.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>

// No parameters: the host power adapter wakes the native UI before touch admission.
// Exception: Returns pending lifecycle or wake errors without submitting input.
extern "C" esp_err_t cody_host_power_touch();

#if defined(_WIN32)
#define CODY_HOST_INPUT_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_INPUT_API extern "C" __attribute__((visibility("default")))
#endif

namespace {
constexpr std::size_t kTouchCapacity = 64;
std::mutex s_touch_mutex;
std::condition_variable s_touch_ready;
std::array<LUNASHELL_touch_point_t, kTouchCapacity> s_touch{};
std::size_t s_head = 0;
std::size_t s_count = 0;
bool s_pressed = false;
bool s_studio_owned = false;
std::size_t s_external_touch_inflight = 0;
}

// pressed: One for contact or movement, zero for the corresponding release.
// x: Logical x in the native 466-pixel display coordinate system.
// y: Logical y in the native 466-pixel display coordinate system.
// Exception: Rejects invalid coordinates, unmatched releases, full queues or unavailable time.
CODY_HOST_INPUT_API esp_err_t cody_host_touch_event(uint8_t pressed, uint16_t x, uint16_t y)
{
    if (pressed > 1 || x >= 466 || y >= 466) return ESP_ERR_INVALID_ARG;
    const int64_t timestamp = esp_timer_get_time();
    if (timestamp <= 0) return ESP_ERR_NOT_FINISHED;
    {
        std::lock_guard<std::mutex> lock(s_touch_mutex);
        if (s_studio_owned) return ESP_ERR_NOT_FINISHED;
        if (!pressed && !s_pressed) return ESP_ERR_INVALID_STATE;
        if (s_count == s_touch.size()) return ESP_ERR_NOT_FINISHED;
        if (s_external_touch_inflight == std::numeric_limits<std::size_t>::max()) return ESP_ERR_INVALID_STATE;
        ++s_external_touch_inflight;
    }
    const auto power_error = cody_host_power_touch();
    {
        std::lock_guard<std::mutex> lock(s_touch_mutex);
        --s_external_touch_inflight;
        if (power_error != ESP_OK) return power_error;
        if (s_studio_owned) return ESP_ERR_NOT_FINISHED;
        if (!pressed && !s_pressed) return ESP_ERR_INVALID_STATE;
        if (s_count == s_touch.size()) return ESP_ERR_NOT_FINISHED;
        auto &event = s_touch[(s_head + s_count) % s_touch.size()];
        event = {true, pressed != 0, x, y, ESP_OK, timestamp};
        s_pressed = pressed != 0;
        ++s_count;
    }
    s_touch_ready.notify_one();
    return ESP_OK;
}

// pressed: One for the route contact, zero for its matching release.
// x, y: Logical native coordinates chosen by the host route contract.
// Exception: Rejects an occupied queue or conflicting contact before enqueueing.
// The deterministic display owner immediately consumes each event itself.
extern "C" esp_err_t cody_host_studio_touch_event(uint8_t pressed, uint16_t x, uint16_t y)
{
    if (pressed > 1 || x >= 466 || y >= 466) return ESP_ERR_INVALID_ARG;
    const int64_t timestamp = esp_timer_get_time();
    if (timestamp <= 0) return ESP_ERR_NOT_FINISHED;
    std::lock_guard<std::mutex> lock(s_touch_mutex);
    if (!s_studio_owned || s_count != 0 || (pressed != 0) == s_pressed) return ESP_ERR_INVALID_STATE;
    auto &event = s_touch[s_head];
    event = {true, pressed != 0, x, y, ESP_OK, timestamp};
    s_pressed = pressed != 0;
    s_count = 1;
    return ESP_OK;
}

// No parameters: reserve the empty touch owner for one deterministic gesture.
// Exception: Returns invalid state if any external contact or event already exists.
extern "C" esp_err_t cody_host_studio_touch_begin()
{
    std::lock_guard<std::mutex> lock(s_touch_mutex);
    if (s_external_touch_inflight != 0) return ESP_ERR_NOT_FINISHED;
    if (s_studio_owned || s_count != 0 || s_pressed) return ESP_ERR_INVALID_STATE;
    s_studio_owned = true;
    return ESP_OK;
}

// abort: True retires only the exclusively owned gesture after a failed operation.
// Exception: Refuses cleanup without ownership, or incomplete success without abort.
extern "C" esp_err_t cody_host_studio_touch_end(bool abort)
{
    std::lock_guard<std::mutex> lock(s_touch_mutex);
    if (!s_studio_owned) return ESP_ERR_INVALID_STATE;
    if (!abort && (s_count != 0 || s_pressed)) return ESP_ERR_INVALID_STATE;
    if (abort) {
        s_count = 0;
        s_pressed = false;
    }
    s_studio_owned = false;
    return ESP_OK;
}

// point: Receives the oldest complete synthetic input event, with its original host timestamp.
// Exception: Returns NOT_FOUND when no event is pending and leaves the destination unchanged.
extern "C" esp_err_t LUNASHELL_touch_read_point(LUNASHELL_touch_point_t *point)
{
    if (!point) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_touch_mutex);
    if (!s_count) return ESP_ERR_NOT_FOUND;
    *point = s_touch[s_head];
    s_head = (s_head + 1) % s_touch.size();
    --s_count;
    return ESP_OK;
}

// timeout_ms: Maximum wait for an explicitly submitted host input event.
// Exception: Returns NOT_FOUND on expiry; no synthetic touch is created by waiting.
extern "C" esp_err_t LUNASHELL_touch_wait_for_report(uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(s_touch_mutex);
    if (!s_count && timeout_ms != 0)
        s_touch_ready.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] { return s_count != 0; });
    return s_count ? ESP_OK : ESP_ERR_NOT_FOUND;
}
