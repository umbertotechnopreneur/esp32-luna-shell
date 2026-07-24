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
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_time_init("UTC0"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_power_inputs_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_boot_audio_play());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_storage_init());
    (void)qsee_radios_init();
    if (!qsee_provisioning_active()) {
        const esp_err_t wifi_scan_result = qsee_wifi_start_scan();
        ESP_LOGI(TAG, "Wi-Fi: boot scan request %s", esp_err_to_name(wifi_scan_result));
    } else {
        ESP_LOGI(
            TAG,
            "Wi-Fi: scan deferred during BLE provisioning (%s, PoP=%s)",
            qsee_provisioning_service_name(),
            qsee_provisioning_pop()
        );
    }
    (void)qsee_hardware_refresh("post-radio");

    qsee_boot_ui_mark_ready();
    qsee_boot_ui_wait_for_continue();
    qsee_boot_ui_hide();

    if (!start_brookesia()) {
        ESP_LOGE(TAG, "ESP-Brookesia launch failed; canonical logger remains active");
    }
}
