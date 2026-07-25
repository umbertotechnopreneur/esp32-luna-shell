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

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <atomic>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_logger.h"

namespace {

const char *TAG = "qsee_storage";
TaskHandle_t s_storage_task;
uint32_t s_mount_attempt_count;
bool s_mounted;
std::atomic_bool s_format_requested;

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

esp_err_t mount_once()
{
    ++s_mount_attempt_count;
    // A format approval is single-use and belongs only to this immediate
    // probe. A missing card or transport error must never carry it forward.
    const bool format_requested = s_format_requested.exchange(false);
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
    if (result == ESP_FAIL && format_requested) {
        format_attempted = true;
        ESP_LOGW(
            TAG,
            "SD transport responded but FAT mount failed; controlled format requested"
        );
        ++s_mount_attempt_count;
        result = bsp_sdcard_mount_with_format(true);
        formatted = result == ESP_OK;
    } else if (format_requested && result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Explicit SD format authorization expired without formatting: %s",
            esp_err_to_name(result)
        );
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

    s_format_requested.store(false);
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
    const esp_err_t logger_result = qsee_logger_enable_sd(BSP_SD_MOUNT_POINT);
    qsee::DeviceSnapshotService::instance().reportStorage(
        true,
        true,
        format_attempted,
        formatted,
        s_mount_attempt_count,
        capacity,
        filesystem_total,
        filesystem_free,
        logger_result == ESP_OK,
        ESP_OK
    );
    s_mounted = true;
    ESP_LOGI(
        TAG,
        "SD ready: capacity=%llu MB free=%llu MB formatted=%s logger=%s",
        static_cast<unsigned long long>(capacity / (1024ULL * 1024ULL)),
        static_cast<unsigned long long>(filesystem_free / (1024ULL * 1024ULL)),
        formatted ? "yes" : "no",
        logger_result == ESP_OK ? "mirrored" : esp_err_to_name(logger_result)
    );
    return ESP_OK;
}

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

extern "C" esp_err_t qsee_storage_request_format(void)
{
    if (s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    const qsee::StorageSnapshot storage =
        qsee::DeviceSnapshotService::instance().snapshot().storage;
    if (!storage.card_present || storage.formatted ||
        storage.last_error != ESP_FAIL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_format_requested.store(true);
    ESP_LOGW(
        TAG,
        "Explicit user-authorized FAT format armed for one immediate probe"
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
