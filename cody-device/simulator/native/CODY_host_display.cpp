#include "CODY_host_display.hpp"
#include "cody_host_studio.h"
#include "LUNASHELL_platform.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace {
constexpr size_t kSide = 466;
constexpr size_t kPixels = kSide * kSide;
constexpr size_t kStripRows = 16;
std::array<uint16_t, kPixels> s_committed{};
std::array<uint16_t, kPixels> s_staging{};
std::array<uint16_t, kSide * kStripRows> s_strip{};
std::mutex s_mutex;
std::thread::id s_owner{};
std::chrono::steady_clock::time_point s_deadline{};
uint32_t s_revision = 0;
uint32_t s_content_revision = 0;
bool s_initialized = false;
bool s_active = false;
bool s_borrowed = false;
bool s_suspended = false;
bool s_quiescent = false;
CodyHostStudioFrameIdentity s_next_identity{};
CodyHostStudioFrameIdentity s_staging_identity{};
CodyHostStudioFrameIdentity s_committed_identity{};
CodyHostStudioWorkerReceipt s_committed_worker{};
CodyHostStudioRouteReceipt s_next_route{};
CodyHostStudioRouteReceipt s_staging_route{};
CodyHostStudioRouteReceipt s_committed_route{};

// No parameters: validates the currently held frame on its rendering thread.
// Exception: Preserves missing-owner and deadline errors without committing a frame.
esp_err_t check_locked()
{
    if (!s_initialized || !s_active || s_owner != std::this_thread::get_id()) return ESP_ERR_INVALID_STATE;
    return std::chrono::steady_clock::now() >= s_deadline ? ESP_ERR_TIMEOUT : ESP_OK;
}
}

// No parameters: initializes the host panel without publishing synthetic device data.
extern "C" esp_err_t LUNASHELL_display_runtime_init(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_active) return ESP_ERR_INVALID_STATE;
    if (!s_initialized) ++s_content_revision;
    s_initialized = true;
    return ESP_OK;
}

// suspended: Closes or reopens the host panel after any active frame finishes.
extern "C" esp_err_t LUNASHELL_display_runtime_set_suspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_active) return ESP_ERR_INVALID_STATE;
    s_suspended = suspended;
    return ESP_OK;
}

// quiescent: Closes or reopens the startup write gate after any active frame finishes.
extern "C" esp_err_t LUNASHELL_display_runtime_set_startup_quiescent(bool quiescent)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_active) return ESP_ERR_INVALID_STATE;
    s_quiescent = quiescent;
    return ESP_OK;
}

// No parameters: reports whether a frame may currently be admitted.
extern "C" bool LUNASHELL_display_runtime_writes_allowed(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_initialized && !s_suspended && !s_quiescent;
}

// No parameters: returns the panel invalidation revision, independent of frame count.
extern "C" uint32_t LUNASHELL_display_runtime_content_revision(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_content_revision;
}

// budget_ms: Whole-frame deadline in milliseconds, matching the production admission range.
// parked: Receives whether a deliberate display gate rejected admission.
// Exception: Returns invalid arguments or a busy display without changing the visible frame.
extern "C" esp_err_t LUNASHELL_display_frame_begin(uint32_t budget_ms, bool *parked)
{
    if (!parked || budget_ms == 0 || budget_ms > 1000) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    *parked = s_suspended || s_quiescent;
    if (!s_initialized || s_active || *parked) return ESP_ERR_INVALID_STATE;
    s_staging = s_committed;
    s_staging_identity = s_next_identity;
    s_staging_route = s_next_route;
    s_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    s_owner = std::this_thread::get_id();
    s_active = true;
    s_borrowed = false;
    return ESP_OK;
}

// No parameters: checks the original deadline without renewing the frame budget.
extern "C" esp_err_t LUNASHELL_display_frame_check_deadline(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return check_locked();
}

// pixels: Receives aligned, borrowed strip storage until submission or frame end.
// capacity_bytes: Receives the actual strip capacity, in bytes.
// Exception: Rejects invalid pointers, a duplicate lease or an expired frame.
extern "C" esp_err_t LUNASHELL_display_frame_buffer(void **pixels, size_t *capacity_bytes)
{
    if (!pixels || !capacity_bytes) return ESP_ERR_INVALID_ARG;
    *pixels = nullptr;
    *capacity_bytes = 0;
    std::lock_guard<std::mutex> lock(s_mutex);
    const auto error = check_locked();
    if (error != ESP_OK) return error;
    if (s_borrowed) return ESP_ERR_INVALID_STATE;
    s_borrowed = true;
    *pixels = s_strip.data();
    *capacity_bytes = sizeof(s_strip);
    return ESP_OK;
}

// x_start, y_start: Inclusive logical display origin of the submitted rectangle.
// x_end, y_end: Exclusive logical end within the physical 466 x 466 canvas.
// pixels: The current borrowed strip, in the same byte-swapped RGB565 order as the panel.
// Exception: Rejects invalid ownership, dimensions, capacity or an expired frame.
extern "C" esp_err_t LUNASHELL_display_frame_submit_rgb565(int x_start, int y_start,
    int x_end, int y_end, const void *pixels)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    const auto error = check_locked();
    if (error != ESP_OK) return error;
    if (!s_borrowed || pixels != s_strip.data()) return ESP_ERR_INVALID_STATE;
    if (x_start < 0 || y_start < 0 || x_end > static_cast<int>(kSide) || y_end > static_cast<int>(kSide) ||
        x_start >= x_end || y_start >= y_end) return ESP_ERR_INVALID_SIZE;
    const auto width = static_cast<size_t>(x_end - x_start);
    const auto height = static_cast<size_t>(y_end - y_start);
    if (width * height > s_strip.size()) return ESP_ERR_INVALID_SIZE;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const auto word = s_strip[y * width + x];
            s_staging[(static_cast<size_t>(y_start) + y) * kSide + static_cast<size_t>(x_start) + x] =
                static_cast<uint16_t>((word >> 8U) | (word << 8U));
        }
    }
    s_borrowed = false;
    return ESP_OK;
}

// frame_error: First error from the rendering owner; failures never publish partial pixels.
// Exception: Returns the first original error, or the frame ownership/deadline error.
extern "C" esp_err_t LUNASHELL_display_frame_end(esp_err_t frame_error)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_active || s_owner != std::this_thread::get_id()) return ESP_ERR_INVALID_STATE;
    const auto status = frame_error == ESP_OK ? check_locked() : frame_error;
    auto result = status;
    if (result == ESP_OK && s_revision == std::numeric_limits<uint32_t>::max()) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        s_committed.swap(s_staging);
        ++s_revision;
        s_committed_identity = s_staging_identity;
        s_committed_identity.frame_revision = s_revision;
        s_committed_worker = {};
        s_committed_route = s_staging_route;
        if (s_committed_route.route_id[0] != '\0') s_committed_route.destination_frame_revision = s_revision;
    }
    s_active = false;
    s_borrowed = false;
    s_owner = {};
    return result;
}

// pixels: Receives a complete committed logical framebuffer in native-endian RGB565.
// count: Exact output capacity, in pixels.
// revision: Receives the completed-frame identity.
// Exception: Rejects invalid buffers or access before the first successful frame.
CODY_HOST_DISPLAY_API esp_err_t cody_host_display_snapshot(uint16_t *pixels, size_t count, uint32_t *revision)
{
    if (!pixels || !revision) return ESP_ERR_INVALID_ARG;
    if (count != kPixels) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_revision == 0) return ESP_ERR_INVALID_STATE;
    std::copy(s_committed.begin(), s_committed.end(), pixels);
    *revision = s_revision;
    return ESP_OK;
}

// identity: Owner-verified route and scenario for the next frame, or null to clear.
// Exception: No exceptions; staging does not publish or modify a completed frame.
void cody_host_display_stage_studio_identity(const CodyHostStudioFrameIdentity *identity)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_next_identity = identity ? *identity : CodyHostStudioFrameIdentity{};
}

// receipt: Owner-verified gesture information for the next frame, or null to clear.
// Exception: No exceptions; nothing is published before a successful frame end.
void cody_host_display_stage_studio_route(const CodyHostStudioRouteReceipt *receipt)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_next_route = receipt ? *receipt : CodyHostStudioRouteReceipt{};
}

// identity: Receives the last retired Studio identity before a route gesture.
// Exception: Returns the original unavailable state without writing the output.
esp_err_t cody_host_display_read_studio_identity(CodyHostStudioFrameIdentity *identity)
{
    if (!identity) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_revision == 0 || s_committed_identity.target[0] == '\0') return ESP_ERR_INVALID_STATE;
    *identity = s_committed_identity;
    return ESP_OK;
}

// receipt: Worker result identifying the already committed native frame.
// Exception: Refuses an unrelated frame, identity, time or malformed receipt.
esp_err_t cody_host_display_ack_studio_worker(const CodyHostStudioWorkerReceipt *receipt)
{
    if (!receipt) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || !s_revision || receipt->abi != 1 || receipt->frame_revision != s_revision ||
        receipt->frame_at_ms != s_committed_identity.at_ms || receipt->requested_ms < receipt->frame_at_ms ||
        std::strcmp(receipt->target, s_committed_identity.target) != 0 ||
        std::strcmp(receipt->scenario_revision, s_committed_identity.scenario_revision) != 0)
        return ESP_ERR_INVALID_STATE;
    s_committed_worker = *receipt;
    return ESP_OK;
}

// pixels, count: Complete native framebuffer and exact capacity.
// identity, identity_bytes: Completed frame identity and exact capacity.
// receipt, receipt_bytes: Same-frame worker acknowledgement and exact capacity.
// Exception: Rejects unavailable evidence or any invalid capacity without writing.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_worker_snapshot(uint16_t *pixels, size_t count,
    CodyHostStudioFrameIdentity *identity, size_t identity_bytes,
    CodyHostStudioWorkerReceipt *receipt, size_t receipt_bytes)
{
    if (!pixels || !identity || !receipt) return ESP_ERR_INVALID_ARG;
    if (count != kPixels || identity_bytes != sizeof(*identity) || receipt_bytes != sizeof(*receipt))
        return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || !s_revision || s_committed_worker.frame_revision != s_revision ||
        s_committed_identity.frame_revision != s_revision) return ESP_ERR_INVALID_STATE;
    std::copy(s_committed.begin(), s_committed.end(), pixels);
    *identity = s_committed_identity;
    *receipt = s_committed_worker;
    return ESP_OK;
}

// No parameters: reports the exact compiled worker receipt capacity.
// Exception: No exceptions.
CODY_HOST_STUDIO_API size_t cody_host_studio_worker_receipt_size()
{
    return sizeof(CodyHostStudioWorkerReceipt);
}

// pixels: Exact native-endian logical framebuffer destination.
// count: Capacity must equal the full logical panel.
// identity: Same-frame route, scenario revision, time and retirement revision.
// identity_bytes: Exact writable metadata capacity; never assumed from the pointer.
// Exception: Returns unavailable when the committed frame has no Studio identity.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_snapshot(uint16_t *pixels,
    size_t count, CodyHostStudioFrameIdentity *identity, size_t identity_bytes)
{
    if (!pixels || !identity) return ESP_ERR_INVALID_ARG;
    if (count != kPixels || identity_bytes != sizeof(CodyHostStudioFrameIdentity)) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_revision == 0 || s_committed_identity.target[0] == '\0')
        return ESP_ERR_INVALID_STATE;
    std::copy(s_committed.begin(), s_committed.end(), pixels);
    *identity = s_committed_identity;
    return ESP_OK;
}

// No parameters: exposes metadata allocation size without touching a frame or pointer.
// Exception: No exceptions; ctypes checks this before passing writable metadata.
CODY_HOST_STUDIO_API size_t cody_host_studio_frame_identity_size()
{
    return sizeof(CodyHostStudioFrameIdentity);
}

// pixels, count: Exact logical framebuffer and capacity.
// identity, identity_bytes: Matching completed identity and exact capacity.
// route, route_bytes: Matching observed route evidence and exact capacity.
// Exception: Rejects invalid buffers, sizes or absent route evidence without writing.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_route_snapshot(uint16_t *pixels, size_t count,
    CodyHostStudioFrameIdentity *identity, size_t identity_bytes, CodyHostStudioRouteReceipt *route, size_t route_bytes)
{
    if (!pixels || !identity || !route) return ESP_ERR_INVALID_ARG;
    if (count != kPixels || identity_bytes != sizeof(*identity) || route_bytes != sizeof(*route)) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized || s_revision == 0 || s_committed_identity.target[0] == '\0' ||
        s_committed_route.route_id[0] == '\0' || s_committed_route.destination_frame_revision != s_revision)
        return ESP_ERR_INVALID_STATE;
    std::copy(s_committed.begin(), s_committed.end(), pixels);
    *identity = s_committed_identity;
    *route = s_committed_route;
    return ESP_OK;
}

// No parameters: provides the exact route receipt capacity to the native transport.
CODY_HOST_STUDIO_API size_t cody_host_studio_route_receipt_size()
{
    return sizeof(CodyHostStudioRouteReceipt);
}
