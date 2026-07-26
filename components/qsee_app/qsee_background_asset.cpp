#include "qsee_background_asset.hpp"

#include <algorithm>
#include <cmath>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "qsee_background";
constexpr uint16_t kStride = qsee::BackgroundAsset::kWidth * 2U;
constexpr uint32_t kPayloadBytes =
    static_cast<uint32_t>(kStride) * qsee::BackgroundAsset::kHeight;

// This fingerprint is only a review marker: change it with the geometry or
// palette so visual changes remain easy to identify in source history.
constexpr uint64_t kThemeFingerprint = UINT64_C(0x9F1AE61D2173B6C4);

constexpr uint8_t kBayer8x8[64] = {
     0, 48, 12, 60,  3, 51, 15, 63,
    32, 16, 44, 28, 35, 19, 47, 31,
     8, 56,  4, 52, 11, 59,  7, 55,
    40, 24, 36, 20, 43, 27, 39, 23,
     2, 50, 14, 62,  1, 49, 13, 61,
    34, 18, 46, 30, 33, 17, 45, 29,
    10, 58,  6, 54,  9, 57,  5, 53,
    42, 26, 38, 22, 41, 25, 37, 21,
};

uint8_t quantize_channel(float value, uint8_t maximum, uint8_t threshold)
{
    const float scaled =
        std::clamp(value, 0.0F, 255.0F) * static_cast<float>(maximum) / 255.0F;
    const float ordered_threshold =
        (static_cast<float>(threshold) + 0.5F) / 64.0F;
    const int quantized =
        static_cast<int>(std::floor(scaled + ordered_threshold));
    return static_cast<uint8_t>(
        std::clamp(quantized, 0, static_cast<int>(maximum))
    );
}

void generate_background(uint8_t *pixels)
{
    constexpr float kCenterX = 233.0F;
    constexpr float kCenterY = 158.0F;
    constexpr float kRadius = 386.0F;
    constexpr float kStartRed = 11.0F;
    constexpr float kStartGreen = 39.0F;
    constexpr float kStartBlue = 51.0F;
    constexpr float kEndRed = 1.0F;
    constexpr float kEndGreen = 2.0F;
    constexpr float kEndBlue = 5.0F;

    for (uint16_t y = 0; y < qsee::BackgroundAsset::kHeight; ++y) {
        for (uint16_t x = 0; x < qsee::BackgroundAsset::kWidth; ++x) {
            const float dx = static_cast<float>(x) - kCenterX;
            const float dy = static_cast<float>(y) - kCenterY;
            const float progress = std::min(
                std::sqrt(dx * dx + dy * dy) / kRadius,
                1.0F
            );
            const float red = kStartRed + (kEndRed - kStartRed) * progress;
            const float green =
                kStartGreen + (kEndGreen - kStartGreen) * progress;
            const float blue = kStartBlue + (kEndBlue - kStartBlue) * progress;
            const uint8_t threshold =
                kBayer8x8[(static_cast<size_t>(y & 7U) * 8U) + (x & 7U)];
            const uint16_t rgb565 =
                (static_cast<uint16_t>(quantize_channel(red, 31, threshold)) << 11U) |
                (static_cast<uint16_t>(quantize_channel(green, 63, threshold)) << 5U) |
                static_cast<uint16_t>(quantize_channel(blue, 31, threshold));
            const size_t offset =
                static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 2U;
            pixels[offset] = static_cast<uint8_t>(rgb565 & 0xFFU);
            pixels[offset + 1U] = static_cast<uint8_t>(rgb565 >> 8U);
        }
        if ((y & 7U) == 7U) {
            vTaskDelay(1);
        }
    }
}

}  // namespace

namespace qsee {

BackgroundAsset &BackgroundAsset::instance()
{
    static BackgroundAsset asset;
    return asset;
}

void BackgroundAsset::request()
{
    State expected = State::Idle;
    if (!state_.compare_exchange_strong(
            expected,
            State::Working,
            std::memory_order_acq_rel
        )) {
        return;
    }

    if (xTaskCreate(
            worker_entry,
            "qsee_bg_asset",
            6144,
            this,
            tskIDLE_PRIORITY + 1,
            nullptr
        ) != pdPASS) {
        state_.store(State::Failed, std::memory_order_release);
        ESP_LOGE(kTag, "Background generator task could not start");
    }
}

const lv_image_dsc_t *BackgroundAsset::image() const
{
    return state_.load(std::memory_order_acquire) == State::Ready
        ? &image_
        : nullptr;
}

void BackgroundAsset::worker_entry(void *argument)
{
    static_cast<BackgroundAsset *>(argument)->run_worker();
    vTaskDelete(nullptr);
}

void BackgroundAsset::run_worker()
{
    auto *pixels = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(
            64,
            kPayloadBytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        )
    );
    if (pixels == nullptr || !esp_ptr_external_ram(pixels)) {
        if (pixels != nullptr) {
            heap_caps_free(pixels);
        }
        state_.store(State::Failed, std::memory_order_release);
        ESP_LOGE(kTag, "Background requires a %lu-byte PSRAM block",
                 static_cast<unsigned long>(kPayloadBytes));
        return;
    }

    generate_background(pixels);
    publish(pixels);
    ESP_LOGI(kTag, "Dithered RGB565 background ready (%ux%u, theme=0x%llx)",
             static_cast<unsigned>(kWidth),
             static_cast<unsigned>(kHeight),
             static_cast<unsigned long long>(kThemeFingerprint));
}

void BackgroundAsset::publish(uint8_t *pixels)
{
    image_ = {};
    image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_.header.cf = LV_COLOR_FORMAT_RGB565;
    image_.header.flags = 0;
    image_.header.w = kWidth;
    image_.header.h = kHeight;
    image_.header.stride = kStride;
    image_.data_size = kPayloadBytes;
    image_.data = pixels;
    state_.store(State::Ready, std::memory_order_release);
}

}  // namespace qsee
