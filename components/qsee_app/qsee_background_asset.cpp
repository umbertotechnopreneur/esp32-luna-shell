/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#include "qsee_background_asset.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "qsee_background";
constexpr char kCacheDirectory[] = "/sdcard/qsee/assets";
constexpr char kCachePath[] =
    "/sdcard/qsee/assets/background-466x466-rgb565le-v1.qbg";
constexpr char kTemporaryPath[] =
    "/sdcard/qsee/assets/background-466x466-rgb565le-v1.qbg.tmp";
constexpr uint16_t kWidth = 466;
constexpr uint16_t kHeight = 466;
constexpr uint16_t kStride = kWidth * 2U;
constexpr uint32_t kPayloadBytes =
    static_cast<uint32_t>(kStride) * static_cast<uint32_t>(kHeight);
constexpr uint16_t kHeaderVersion = 1;
constexpr uint16_t kGeneratorVersion = 1;
constexpr uint16_t kPixelFormatRgb565Le = 1;
constexpr size_t kIoBlockBytes = 16U * 1024U;
constexpr uint8_t kCacheMagic[8] = {'Q', 'S', 'E', 'E', 'B', 'G', '0', '1'};
constexpr uint64_t kThemeFingerprint = UINT64_C(0x9F1AE61D2173B6C4);

#pragma pack(push, 1)
struct CacheHeader {
    uint8_t magic[8];
    uint16_t header_version;
    uint16_t generator_version;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t pixel_format;
    uint32_t payload_bytes;
    uint64_t theme_fingerprint;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint8_t reserved[24];
};
#pragma pack(pop)

static_assert(sizeof(CacheHeader) == 64, "Background cache header must stay fixed-width");
static_assert(kPayloadBytes == 434312U, "Unexpected 466x466 RGB565 payload size");

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

// Calculate the header checksum with its checksum field cleared.
uint32_t calculate_header_crc(CacheHeader header)
{
    header.header_crc32 = 0;
    return esp_crc32_le(
        0,
        reinterpret_cast<const uint8_t *>(&header),
        sizeof(header)
    );
}

// Reject stale or ABI-incompatible cache metadata before reading its payload.
bool validate_header(const CacheHeader &header)
{
    return std::memcmp(header.magic, kCacheMagic, sizeof(kCacheMagic)) == 0 &&
           header.header_version == kHeaderVersion &&
           header.generator_version == kGeneratorVersion &&
           header.width == kWidth &&
           header.height == kHeight &&
           header.stride == kStride &&
           header.pixel_format == kPixelFormatRgb565Le &&
           header.payload_bytes == kPayloadBytes &&
           header.theme_fingerprint == kThemeFingerprint &&
           header.header_crc32 == calculate_header_crc(header);
}

// Yield between bounded I/O chunks so cached background work stays unobtrusive.
bool read_payload(FILE *file, uint8_t *pixels, uint32_t *crc)
{
    size_t offset = 0;
    uint32_t running_crc = 0;
    while (offset < kPayloadBytes) {
        const size_t block =
            std::min(kIoBlockBytes, static_cast<size_t>(kPayloadBytes) - offset);
        if (std::fread(pixels + offset, 1, block, file) != block) {
            return false;
        }
        running_crc = esp_crc32_le(
            running_crc,
            pixels + offset,
            static_cast<uint32_t>(block)
        );
        offset += block;
        vTaskDelay(1);
    }
    *crc = running_crc;
    return true;
}

// Load only a cache whose fixed header, exact size, and payload CRC all match.
bool load_cache(uint8_t *pixels)
{
    FILE *file = std::fopen(kCachePath, "rb");
    if (file == nullptr) {
        if (errno != ENOENT) {
            ESP_LOGW(kTag, "Background cache open failed: errno=%d", errno);
        }
        return false;
    }

    bool valid = std::fseek(file, 0, SEEK_END) == 0;
    const long file_size = valid ? std::ftell(file) : -1;
    valid = valid &&
            file_size == static_cast<long>(sizeof(CacheHeader) + kPayloadBytes) &&
            std::fseek(file, 0, SEEK_SET) == 0;

    CacheHeader header{};
    valid = valid &&
            std::fread(&header, 1, sizeof(header), file) == sizeof(header) &&
            validate_header(header);

    uint32_t payload_crc = 0;
    valid = valid &&
            read_payload(file, pixels, &payload_crc) &&
            payload_crc == header.payload_crc32;
    if (std::fclose(file) != 0) {
        valid = false;
    }

    if (!valid) {
        ESP_LOGW(kTag, "Background cache rejected; regenerating it");
    }
    return valid;
}

// Quantize one channel with an ordered threshold to avoid visible RGB565 bands.
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

// Reproduce the original full-screen radial atmosphere as deterministic RGB565LE.
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

    for (uint16_t y = 0; y < kHeight; ++y) {
        for (uint16_t x = 0; x < kWidth; ++x) {
            const float dx = static_cast<float>(x) - kCenterX;
            const float dy = static_cast<float>(y) - kCenterY;
            const float progress =
                std::min(std::sqrt(dx * dx + dy * dy) / kRadius, 1.0F);
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
                (static_cast<size_t>(y) * kStride) +
                (static_cast<size_t>(x) * 2U);
            pixels[offset] = static_cast<uint8_t>(rgb565 & 0xFFU);
            pixels[offset + 1U] = static_cast<uint8_t>(rgb565 >> 8U);
        }
        if ((y & 7U) == 7U) {
            vTaskDelay(1);
        }
    }
}

// Create cache directories without ever requesting or attempting SD formatting.
bool ensure_cache_directory()
{
    if (::mkdir("/sdcard/qsee", 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(kTag, "Background cache root unavailable: errno=%d", errno);
        return false;
    }
    if (::mkdir(kCacheDirectory, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(kTag, "Background cache directory unavailable: errno=%d", errno);
        return false;
    }
    return true;
}

// Persist a complete cache through a durable temporary file and final rename.
bool save_cache(const uint8_t *pixels)
{
    if (!ensure_cache_directory()) {
        return false;
    }
    (void)std::remove(kTemporaryPath);

    CacheHeader header{};
    std::memcpy(header.magic, kCacheMagic, sizeof(kCacheMagic));
    header.header_version = kHeaderVersion;
    header.generator_version = kGeneratorVersion;
    header.width = kWidth;
    header.height = kHeight;
    header.stride = kStride;
    header.pixel_format = kPixelFormatRgb565Le;
    header.payload_bytes = kPayloadBytes;
    header.theme_fingerprint = kThemeFingerprint;

    size_t offset = 0;
    while (offset < kPayloadBytes) {
        const size_t block =
            std::min(kIoBlockBytes, static_cast<size_t>(kPayloadBytes) - offset);
        header.payload_crc32 = esp_crc32_le(
            header.payload_crc32,
            pixels + offset,
            static_cast<uint32_t>(block)
        );
        offset += block;
        vTaskDelay(1);
    }
    header.header_crc32 = calculate_header_crc(header);

    FILE *file = std::fopen(kTemporaryPath, "wb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Background cache temporary open failed: errno=%d", errno);
        return false;
    }

    bool complete =
        std::fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    offset = 0;
    while (complete && offset < kPayloadBytes) {
        const size_t block =
            std::min(kIoBlockBytes, static_cast<size_t>(kPayloadBytes) - offset);
        complete = std::fwrite(pixels + offset, 1, block, file) == block;
        offset += block;
        vTaskDelay(1);
    }
    complete = complete && std::fflush(file) == 0;
    const int file_descriptor = fileno(file);
    complete = complete && file_descriptor >= 0 &&
               ::fsync(file_descriptor) == 0;
    if (std::fclose(file) != 0) {
        complete = false;
    }

    if (!complete || std::rename(kTemporaryPath, kCachePath) != 0) {
        ESP_LOGW(kTag, "Background cache write failed: errno=%d", errno);
        (void)std::remove(kTemporaryPath);
        return false;
    }
    return true;
}

}  // namespace

namespace qsee {

// Return the cache whose PSRAM storage and image descriptor are never destroyed.
BackgroundAssetCache &BackgroundAssetCache::instance()
{
    static BackgroundAssetCache cache;
    return cache;
}

// Start the cache worker once; later requests are intentionally no-ops.
void BackgroundAssetCache::request()
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
            "qsee_bg_cache",
            6144,
            this,
            tskIDLE_PRIORITY + 1,
            nullptr
        ) != pdPASS) {
        state_.store(State::Failed, std::memory_order_release);
        ESP_LOGE(kTag, "Background cache worker could not start");
    }
}

// Expose the descriptor only after the worker has published all of its fields.
const lv_image_dsc_t *BackgroundAssetCache::image() const
{
    return state_.load(std::memory_order_acquire) == State::Ready
        ? &image_
        : nullptr;
}

// Distinguish a terminal worker failure from a background still being prepared.
bool BackgroundAssetCache::failed() const
{
    return state_.load(std::memory_order_acquire) == State::Failed;
}

// Run the worker on its owning singleton and release only the task resources.
void BackgroundAssetCache::worker_entry(void *argument)
{
    static_cast<BackgroundAssetCache *>(argument)->run_worker();
    ESP_LOGI(
        kTag,
        "Background worker complete (stack-free-min=%lu B)",
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr))
    );
    vTaskDelete(nullptr);
}

// Prefer the validated SD cache, then generate into the same mandatory PSRAM buffer.
void BackgroundAssetCache::run_worker()
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
        ESP_LOGE(kTag, "Background cache requires a 424 KiB PSRAM block");
        return;
    }

    if (load_cache(pixels)) {
        publish(pixels);
        ESP_LOGI(kTag, "Background cache loaded from SD into PSRAM");
        return;
    }

    (void)std::remove(kCachePath);
    generate_background(pixels);
    const bool saved = save_cache(pixels);
    publish(pixels);
    ESP_LOGI(
        kTag,
        "Background generated in PSRAM; persistent cache %s",
        saved ? "ready" : "unavailable this boot"
    );
}

// Publish the process-lifetime raw image after every byte is ready.
void BackgroundAssetCache::publish(uint8_t *pixels)
{
    pixels_ = pixels;
    image_ = {};
    image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_.header.cf = LV_COLOR_FORMAT_RGB565;
    image_.header.flags = 0;
    image_.header.w = kWidth;
    image_.header.h = kHeight;
    image_.header.stride = kStride;
    image_.data_size = kPayloadBytes;
    image_.data = pixels_;
    state_.store(State::Ready, std::memory_order_release);
}

}  // namespace qsee
