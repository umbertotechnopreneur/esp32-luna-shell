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

#include <new>
#include <vector>

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include "qsee_app.hpp"
#include "qsee_boot.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

using esp_brookesia::gui::LvLock;
using esp_brookesia::gui::LvLockGuard;
using esp_brookesia::systems::phone::Phone;

namespace {
const char *TAG = "qsee_main";

void mount_sdcard()
{
    ESP_LOGI(TAG, "SD: mounting 1-bit SDMMC at %s", BSP_SD_MOUNT_POINT);
    const esp_err_t sd_result = bsp_sdcard_mount();
    if (sd_result != ESP_OK) {
        qsee::DeviceSnapshotService::instance().reportStorage(false, 0, 0, 0, false, sd_result);
        ESP_LOGW(
            TAG,
            "SD: mount failed (%s); OCR/transport failures happen before FAT formatting",
            esp_err_to_name(sd_result)
        );
        return;
    }

    const uint64_t sd_capacity =
        static_cast<uint64_t>(bsp_sdcard->csd.capacity) * bsp_sdcard->csd.sector_size;
    ESP_LOGI(TAG, "SD: mounted, capacity=%llu MB",
             static_cast<unsigned long long>(sd_capacity / (1024ULL * 1024ULL)));
    uint64_t filesystem_total = 0;
    uint64_t filesystem_free = 0;
    const esp_err_t space_result =
        esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &filesystem_total, &filesystem_free);
    if (space_result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "SD: filesystem total=%llu MB free=%llu MB",
            static_cast<unsigned long long>(filesystem_total / (1024ULL * 1024ULL)),
            static_cast<unsigned long long>(filesystem_free / (1024ULL * 1024ULL))
        );
    } else {
        ESP_LOGW(TAG, "SD: filesystem space query failed (%s)", esp_err_to_name(space_result));
    }
    const esp_err_t mirror_result = qsee_logger_enable_sd(BSP_SD_MOUNT_POINT);
    qsee::DeviceSnapshotService::instance().reportStorage(
        true,
        sd_capacity,
        filesystem_total,
        filesystem_free,
        mirror_result == ESP_OK,
        ESP_OK
    );
    if (mirror_result != ESP_OK) {
        ESP_LOGW(TAG, "SD: logger mirror unavailable (%s)", esp_err_to_name(mirror_result));
    }
}

bool start_brookesia()
{
    LvLock::registerCallbacks(
        [](int timeout_ms) {
            return bsp_display_lock(timeout_ms) == ESP_OK;
        },
        []() {
            bsp_display_unlock();
            return true;
        }
    );

    auto *phone = new (std::nothrow) Phone();
    if (phone == nullptr) {
        ESP_LOGE(TAG, "ESP-Brookesia: phone allocation failed");
        return false;
    }

    LvLockGuard gui_guard;
    if (!phone->begin()) {
        ESP_LOGE(TAG, "ESP-Brookesia: phone begin failed");
        return false;
    }

    std::vector<esp_brookesia::systems::base::Manager::RegistryAppInfo> apps;
    if (!phone->initAppFromRegistry(apps) || !phone->installAppFromRegistry(apps)) {
        ESP_LOGE(TAG, "ESP-Brookesia: QSee.ai app install failed");
        return false;
    }

    auto *qsee_app = qsee::App::requestInstance();
    const esp_brookesia::systems::base::Context::AppEventData start_event = {
        .id = qsee_app->getId(),
        .type = esp_brookesia::systems::base::Context::AppEventType::START,
        .data = nullptr,
    };
    if (!phone->sendAppEvent(&start_event)) {
        ESP_LOGE(TAG, "ESP-Brookesia: QSee.ai app auto-start failed");
        return false;
    }

    ESP_LOGI(TAG, "ESP-Brookesia ready with %u registered app(s)", static_cast<unsigned>(apps.size()));
    return true;
}

void brookesia_task(void *arg)
{
    (void)arg;
    if (!start_brookesia()) {
        ESP_LOGE(TAG, "ESP-Brookesia launch failed; canonical logger remains active");
    }
    vTaskDelete(nullptr);
}
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(qsee_logger_init());
    ESP_LOGI(TAG, "QSee.ai firmware boot");
    ESP_LOGI(TAG, "Display contract: round 466x466, safe square 320x320 at (73,73)");

    ESP_LOGI(TAG, "Display: starting official Waveshare BSP");
    if (bsp_display_start() == nullptr) {
        qsee::DeviceSnapshotService::instance().reportDisplay(false, ESP_FAIL);
        ESP_LOGE(TAG, "Display: initialization failed");
        return;
    }
    qsee::DeviceSnapshotService::instance().reportDisplay(true, ESP_OK);

    if (qsee_boot_ui_show() != ESP_OK) {
        ESP_LOGE(TAG, "Boot UI: initialization failed");
        return;
    }

    lv_indev_t *touch = bsp_display_get_input_dev();
    qsee::DeviceSnapshotService::instance().reportTouch(
        touch != nullptr,
        touch != nullptr ? ESP_OK : ESP_ERR_NOT_FOUND
    );
    ESP_LOGI(TAG, "Touch CST9217: %s", touch != nullptr ? "ready" : "unavailable");

    (void)qsee_hardware_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_boot_audio_play());
    mount_sdcard();
    (void)qsee_radios_init();
    const esp_err_t wifi_scan_result = qsee_wifi_start_scan();
    ESP_LOGI(TAG, "Wi-Fi: boot scan request %s", esp_err_to_name(wifi_scan_result));
    (void)qsee_hardware_refresh("post-radio");

    qsee_boot_ui_mark_ready();
    qsee_boot_ui_hide();

    const BaseType_t task_result = xTaskCreatePinnedToCore(
        brookesia_task,
        "qsee_brookesia",
        12U * 1024U,
        nullptr,
        5,
        nullptr,
        1
    );
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "ESP-Brookesia task creation failed; canonical logger remains active");
    }
}
