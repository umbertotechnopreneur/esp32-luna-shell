#include "CODY_screenshot.hpp"

#include <atomic>
#include <cstring>

#include "LUNASHELL_platform.h"
#include "LUNASHELL_build_mode.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_screenshot";
std::atomic<ScreenshotState> s_state{ScreenshotState::Idle};
std::atomic<esp_err_t> s_error{ESP_OK};
std::atomic<bool> s_request_pending{false};
std::atomic<uint32_t> s_requested_capture_id{0};
std::atomic<uint32_t> s_next_capture_id{0};

// capture_id: Storage-owner completion identity.
// error: Original BMP or SD operation result.
// context: Reserved callback context; this component uses static atomic state.
void screenshot_completed(uint32_t capture_id, esp_err_t error, void *context)
{
    (void)context;
    const uint32_t expected = s_requested_capture_id.load(std::memory_order_acquire);
    if (capture_id != expected || s_state.load(std::memory_order_acquire) != ScreenshotState::Saving) {
        ESP_LOGE(kTag, "Screenshot completion rejected: capture=%lu expected=%lu state=%u (%s)",
                 static_cast<unsigned long>(capture_id),
                 static_cast<unsigned long>(expected),
                 static_cast<unsigned>(s_state.load(std::memory_order_relaxed)),
                 esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        s_error.store(ESP_ERR_INVALID_RESPONSE, std::memory_order_release);
        s_state.store(ScreenshotState::Failed, std::memory_order_release);
        return;
    }
    s_error.store(error, std::memory_order_release);
    s_state.store(
        error == ESP_OK ? ScreenshotState::Saved : ScreenshotState::Failed,
        std::memory_order_release
    );
    if (error == ESP_OK) {
        ESP_LOGI(kTag, "Screenshot saved: capture=%lu", static_cast<unsigned long>(capture_id));
    } else {
        ESP_LOGE(kTag, "Screenshot save failed: capture=%lu error=%s",
                 static_cast<unsigned long>(capture_id), esp_err_to_name(error));
    }
}

// error: Original failure to retain for visible control feedback.
void record_failure(esp_err_t error)
{
    s_error.store(error, std::memory_order_release);
    s_state.store(ScreenshotState::Failed, std::memory_order_release);
}

}  // namespace

// No parameters: return the current asynchronous capture/write feedback state.
ScreenshotState screenshot_state()
{
    return s_state.load(std::memory_order_acquire);
}

// No parameters: return the original error from the latest failed request.
esp_err_t screenshot_error()
{
    return s_error.load(std::memory_order_acquire);
}

// No parameters: admit one new on-demand request when no capture or write is active.
// Exception: Returns ESP_ERR_NOT_SUPPORTED outside Debug or ESP_ERR_INVALID_STATE while busy.
esp_err_t request_screenshot()
{
    if constexpr (!LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
        ESP_LOGE(kTag, "Screenshot request rejected: Debug only (%s)",
                 esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    ScreenshotState current = s_state.load(std::memory_order_acquire);
    while (current != ScreenshotState::Capturing && current != ScreenshotState::Saving) {
        if (s_state.compare_exchange_weak(
                current, ScreenshotState::Capturing,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            const uint32_t capture_id =
                s_next_capture_id.fetch_add(1, std::memory_order_relaxed) + 1U;
            if (capture_id == 0) {
                record_failure(ESP_ERR_INVALID_SIZE);
                ESP_LOGE(kTag, "Screenshot identity exhausted (%s)",
                         esp_err_to_name(ESP_ERR_INVALID_SIZE));
                return ESP_ERR_INVALID_SIZE;
            }
            s_requested_capture_id.store(capture_id, std::memory_order_release);
            s_error.store(ESP_OK, std::memory_order_release);
            s_request_pending.store(true, std::memory_order_release);
            ESP_LOGI(kTag, "Screenshot requested: capture=%lu",
                     static_cast<unsigned long>(capture_id));
            return ESP_OK;
        }
    }
    ESP_LOGE(kTag, "Screenshot request rejected: state=%u (%s)",
             static_cast<unsigned>(current), esp_err_to_name(ESP_ERR_INVALID_STATE));
    return ESP_ERR_INVALID_STATE;
}

// width: Complete logical canvas width.
// height: Complete logical canvas height.
// capture: Receives temporary PSRAM ownership when a request is pending.
// Exception: Returns ESP_ERR_NOT_FOUND when idle, or the real allocation/geometry error.
esp_err_t begin_screenshot_capture(uint16_t width, uint16_t height,
                                   ScreenshotCapture &capture)
{
    capture = {};
    if constexpr (!LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
        // No capture request is admitted in Release, so normal frames own no capture buffer.
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_request_pending.exchange(false, std::memory_order_acq_rel)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (width == 0 || height == 0 || width > 1024 || height > 1024) {
        record_failure(ESP_ERR_INVALID_SIZE);
        ESP_LOGE(kTag, "Screenshot canvas rejected: %ux%u (%s)",
                 width, height, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t pixel_count = static_cast<size_t>(width) * height;
    void *storage = heap_caps_malloc(
        pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (storage == nullptr) {
        record_failure(ESP_ERR_NO_MEM);
        ESP_LOGE(kTag, "Screenshot PSRAM allocation failed: bytes=%u (%s)",
                 static_cast<unsigned>(pixel_count * sizeof(uint16_t)),
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    capture = {
        .pixels = static_cast<uint16_t *>(storage),
        .width = width,
        .height = height,
        .stride = width,
        .capture_id = s_requested_capture_id.load(std::memory_order_acquire),
    };
    return ESP_OK;
}

// capture: Active temporary frame receiving this composed strip.
// strip: Complete-width panel-order RGB565 strip after every visual layer.
// first_row: Logical destination row in the complete capture.
// first_column: Logical destination column; captures require zero.
// Exception: Returns the real surface, geometry or state error.
esp_err_t capture_screenshot_rows(const ScreenshotCapture &capture,
                                  CODY2D::Surface strip, uint16_t first_row,
                                  uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (capture.pixels == nullptr || capture.capture_id == 0 || first_column != 0 ||
        strip.width != capture.width || first_row + strip.height > capture.height ||
        strip.byte_order != CODY2D::Rgb565ByteOrder::ByteSwapped) {
        ESP_LOGE(
            kTag,
            "Screenshot strip rejected: active=%u id=%lu column=%u width=%u/%u row=%u height=%u/%u order=%u (%s)",
            static_cast<unsigned>(capture.pixels != nullptr),
            static_cast<unsigned long>(capture.capture_id),
            static_cast<unsigned>(first_column),
            static_cast<unsigned>(strip.width),
            static_cast<unsigned>(capture.width),
            static_cast<unsigned>(first_row),
            static_cast<unsigned>(strip.height),
            static_cast<unsigned>(capture.height),
            static_cast<unsigned>(strip.byte_order),
            esp_err_to_name(ESP_ERR_INVALID_SIZE)
        );
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint16_t row = 0; row < strip.height; ++row) {
        std::memcpy(
            capture.pixels + static_cast<size_t>(first_row + row) * capture.stride,
            strip.pixels + static_cast<size_t>(row) * strip.stride,
            static_cast<size_t>(strip.width) * sizeof(uint16_t)
        );
    }
    return ESP_OK;
}

// capture: Active temporary frame; always cleared and released or transferred.
// frame_error: Completed-frame result; a failed frame can never become a screenshot.
// Exception: Returns the frame error or real storage queue failure.
esp_err_t finish_screenshot_capture(ScreenshotCapture &capture, esp_err_t frame_error)
{
    if (capture.pixels == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    uint16_t *pixels = capture.pixels;
    const uint16_t width = capture.width;
    const uint16_t height = capture.height;
    const uint16_t stride = capture.stride;
    const uint32_t capture_id = capture.capture_id;
    capture = {};
    if (frame_error != ESP_OK) {
        heap_caps_free(pixels);
        record_failure(frame_error);
        ESP_LOGE(kTag, "Screenshot frame failed: capture=%lu error=%s",
                 static_cast<unsigned long>(capture_id), esp_err_to_name(frame_error));
        return frame_error;
    }

    s_state.store(ScreenshotState::Saving, std::memory_order_release);
    const esp_err_t queue_error = LUNASHELL_storage_queue_screenshot_rgb565(
        pixels, width, height, stride, true, capture_id,
        screenshot_completed, nullptr
    );
    if (queue_error != ESP_OK) {
        heap_caps_free(pixels);
        record_failure(queue_error);
        ESP_LOGE(kTag, "Screenshot storage queue failed: capture=%lu error=%s",
                 static_cast<unsigned long>(capture_id), esp_err_to_name(queue_error));
        return queue_error;
    }
    return ESP_OK;
}

}  // namespace CODY
