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

// Storage and filesystem helpers for SD-backed data paths.
// Tracks mount state and availability for the application snapshot.

#include "qsee_platform.h"

#include <atomic>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_settings.hpp"

namespace {

const char *TAG = "qsee_storage";
TaskHandle_t s_storage_task;
uint32_t s_mount_attempt_count;
bool s_mounted;
bool s_logger_mirror_preference = true;
std::atomic_bool s_format_requested;
constexpr char kLoggerMirrorSetting[] = "sd_mirror";

void report_failure(esp_err_t error, bool card_responded, bool format_attempted)
{
    qsee::DeviceSnapshotService::instance().reportStorage(
        false,
        card_responded,
        format_attempted,
        false,
        s_mount_attempt_count,
        0,
        0,
        0,
        false,
        error
    );
}

// Probe/mount once and format only when the one-shot authorization is armed.
esp_err_t mount_once()
{
    ++s_mount_attempt_count;
    ESP_LOGI(
        TAG,
        "SD attempt #%lu: probing 1-bit SDMMC at %s without formatting",
        static_cast<unsigned long>(s_mount_attempt_count),
        BSP_SD_MOUNT_POINT
    );
    esp_err_t result = bsp_sdcard_mount_with_format(false);
    bool format_attempted = false;
    bool formatted = false;

    // ESP_FAIL is returned after card initialization when no mountable FAT
    // filesystem exists. Transport/OCR errors retain their specific codes.
    if (result == ESP_FAIL && s_format_requested.exchange(false)) {
        format_attempted = true;
        ESP_LOGW(
            TAG,
            "SD transport responded but FAT mount failed; controlled format requested"
        );
        ++s_mount_attempt_count;
        result = bsp_sdcard_mount_with_format(true);
        formatted = result == ESP_OK;
    }

    if (result != ESP_OK) {
        const bool card_responded = result == ESP_FAIL;
        report_failure(result, card_responded, format_attempted);
        ESP_LOGW(
            TAG,
            "SD unavailable: %s (%s)",
            esp_err_to_name(result),
            card_responded ? "card responded" : "no valid SDMMC response/card missing"
        );
        return result;
    }

    const uint64_t capacity =
        static_cast<uint64_t>(bsp_sdcard->csd.capacity) *
        bsp_sdcard->csd.sector_size;
    uint64_t filesystem_total = 0;
    uint64_t filesystem_free = 0;
    const esp_err_t info_result =
        esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &filesystem_total, &filesystem_free);
    if (info_result != ESP_OK) {
        ESP_LOGW(TAG, "SD space query failed: %s", esp_err_to_name(info_result));
    }
    const esp_err_t logger_result = s_logger_mirror_preference
        ? qsee_logger_enable_sd(BSP_SD_MOUNT_POINT)
        : ESP_OK;
    qsee::DeviceSnapshotService::instance().reportStorage(
        true,
        true,
        format_attempted,
        formatted,
        s_mount_attempt_count,
        capacity,
        filesystem_total,
        filesystem_free,
        s_logger_mirror_preference && logger_result == ESP_OK,
        ESP_OK
    );
    s_mounted = true;
    ESP_LOGI(
        TAG,
        "SD ready: capacity=%llu MB free=%llu MB formatted=%s logger=%s",
        static_cast<unsigned long long>(capacity / (1024ULL * 1024ULL)),
        static_cast<unsigned long long>(filesystem_free / (1024ULL * 1024ULL)),
        formatted ? "yes" : "no",
        !s_logger_mirror_preference ? "disabled by user" :
        logger_result == ESP_OK ? "mirrored" : esp_err_to_name(logger_result)
    );
    return ESP_OK;
}

// Retry mounting with backoff; task notifications interrupt the wait.
void storage_task(void *argument)
{
    (void)argument;
    uint32_t retry_seconds = 15;
    while (!s_mounted) {
        const esp_err_t result = mount_once();
        if (result == ESP_OK) {
            break;
        }
        ESP_LOGI(
            TAG,
            "SD retry scheduled in %lu seconds; application remains available",
            static_cast<unsigned long>(retry_seconds)
        );
        (void)ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(retry_seconds * 1000U)
        );
        if (retry_seconds < 120) {
            retry_seconds *= 2;
        }
    }
    s_storage_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" esp_err_t qsee_storage_init(void)
{
    if (s_mounted || s_storage_task != nullptr) {
        return ESP_OK;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        (void)settings.initialize();
    }
    bool saved_mirror = true;
    if (settings.isInitialized() &&
        settings.getBool(kLoggerMirrorSetting, true, &saved_mirror) == ESP_OK) {
        s_logger_mirror_preference = saved_mirror;
    }
    if (xTaskCreate(
            storage_task,
            "qsee_storage",
            6144,
            nullptr,
            3,
            &s_storage_task
        ) != pdPASS) {
        s_storage_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        TAG,
        "SD service started: non-blocking detect/mount retries; automatic format disabled"
    );
    return ESP_OK;
}

// Arm one format attempt for the next responding card with invalid FAT.
extern "C" esp_err_t qsee_storage_request_format(void)
{
    if (s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    s_format_requested.store(true);
    ESP_LOGW(
        TAG,
        "Explicit user-authorized FAT format armed for the next responding card"
    );
    if (s_storage_task != nullptr) {
        xTaskNotifyGive(s_storage_task);
    }
    return ESP_OK;
}

extern "C" bool qsee_storage_mounted(void)
{
    return s_mounted;
}

// Persist the mirror preference and update the mounted logger immediately.
extern "C" esp_err_t qsee_storage_set_logger_mirror(bool enabled)
{
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) return init_error;
    }
    const esp_err_t persist_error =
        settings.setBool(kLoggerMirrorSetting, enabled);
    if (persist_error != ESP_OK) return persist_error;
    s_logger_mirror_preference = enabled;

    esp_err_t apply_error = ESP_OK;
    if (s_mounted) {
        if (enabled) {
            apply_error = qsee_logger_enable_sd(BSP_SD_MOUNT_POINT);
        } else {
            qsee_logger_disable_sd();
        }
        const qsee::DeviceSnapshot snapshot =
            qsee::DeviceSnapshotService::instance().snapshot();
        qsee::DeviceSnapshotService::instance().reportStorage(
            s_mounted,
            snapshot.storage.card_present,
            snapshot.storage.format_attempted,
            snapshot.storage.formatted,
            snapshot.storage.mount_attempt_count,
            snapshot.storage.capacity_bytes,
            snapshot.storage.filesystem_total_bytes,
            snapshot.storage.free_bytes,
            enabled && apply_error == ESP_OK,
            snapshot.storage.last_error
        );
    }
    ESP_LOGI(TAG, "SD log mirror preference: %s", enabled ? "enabled" : "disabled");
    return apply_error;
}

extern "C" bool qsee_storage_get_logger_mirror_preference(void)
{
    return s_logger_mirror_preference;
}
