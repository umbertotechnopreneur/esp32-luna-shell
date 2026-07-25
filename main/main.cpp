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

#include <memory>
#include <string_view>
#include <utility>

#include "brookesia/gui_lvgl.hpp"
#include "brookesia/system_super.hpp"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_app.hpp"
#include "qsee_boot.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {

constexpr char kTag[] = "qsee_main";

std::unique_ptr<esp_brookesia::system::super::System> s_system;

bool start_brookesia()
{
    s_system = std::make_unique<esp_brookesia::system::super::System>();

    esp_brookesia::system::super::System::Config config;
    config.core_config.gui_backend =
        std::make_unique<esp_brookesia::gui::lvgl::Backend>();
    config.core_config.environment.width_px = 466;
    config.core_config.environment.height_px = 466;
    config.core_config.environment.density = 1.0F;
    config.core_config.environment.font_scale = 1.0F;
    config.core_config.environment.language = "en";
    config.core_config.install_registered_apps = true;
    config.core_config.install_package_apps = false;
    config.core_config.storage.default_install_target =
        esp_brookesia::system::core::StorageInstallTarget::Internal;

    auto init_result = s_system->init(std::move(config));
    if (!init_result) {
        ESP_LOGE(kTag, "Brookesia System Super init failed: %s", init_result.error().c_str());
        s_system.reset();
        return false;
    }

    auto start_result = s_system->start();
    if (!start_result) {
        ESP_LOGE(kTag, "Brookesia System Super start failed: %s", start_result.error().c_str());
        s_system->deinit();
        s_system.reset();
        return false;
    }

    for (const auto &app : s_system->list_apps()) {
        if (app.manifest.id != qsee::kAppManifestId) {
            continue;
        }
        auto app_start_result = s_system->start_app(app.app_id);
        if (!app_start_result) {
            ESP_LOGE(kTag, "QSee.ai app start failed: %s", app_start_result.error().c_str());
            return false;
        }
        ESP_LOGI(
            kTag,
            "Brookesia System Super ready; QSee.ai app id=%lu",
            static_cast<unsigned long>(app.app_id)
        );
        return true;
    }

    ESP_LOGE(kTag, "QSee.ai provider was not installed by Brookesia System Core");
    return false;
}

void brookesia_task(void *)
{
    if (!start_brookesia()) {
        ESP_LOGE(kTag, "Brookesia launch failed; canonical logger remains active");
    }
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(qsee_logger_init());
    ESP_LOGI(kTag, "QSee.ai firmware boot");
    ESP_LOGI(kTag, "Display contract: round 466x466, safe square 320x320 at (73,73)");

    ESP_LOGI(kTag, "Display: starting official Waveshare BSP");
    if (bsp_display_start() == nullptr) {
        qsee::DeviceSnapshotService::instance().reportDisplay(false, ESP_FAIL);
        ESP_LOGE(kTag, "Display: initialization failed");
        return;
    }
    qsee::DeviceSnapshotService::instance().reportDisplay(true, ESP_OK);

    if (qsee_boot_ui_show() != ESP_OK) {
        ESP_LOGE(kTag, "Boot UI: initialization failed");
        return;
    }

    lv_indev_t *touch = bsp_display_get_input_dev();
    qsee::DeviceSnapshotService::instance().reportTouch(
        touch != nullptr,
        touch != nullptr ? ESP_OK : ESP_ERR_NOT_FOUND
    );
    ESP_LOGI(kTag, "Touch CST9217: %s", touch != nullptr ? "ready" : "unavailable");

    (void)qsee_hardware_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_time_init("UTC0"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_power_inputs_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_boot_audio_play());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_storage_init());
    (void)qsee_radios_init();
    if (!qsee_provisioning_active()) {
        const esp_err_t wifi_scan_result = qsee_wifi_start_scan();
        ESP_LOGI(kTag, "Wi-Fi: boot scan request %s", esp_err_to_name(wifi_scan_result));
    } else {
        ESP_LOGI(
            kTag,
            "Wi-Fi: scan deferred during BLE provisioning (%s, PoP=%s)",
            qsee_provisioning_service_name(),
            qsee_provisioning_pop()
        );
    }
    (void)qsee_hardware_refresh("post-radio");

    qsee_boot_ui_mark_ready();
    qsee_boot_ui_hide();

    const BaseType_t task_result = xTaskCreatePinnedToCore(
        brookesia_task,
        "qsee_brookesia",
        40U * 1024U,
        nullptr,
        5,
        nullptr,
        1
    );
    if (task_result != pdPASS) {
        ESP_LOGE(kTag, "Brookesia task creation failed; canonical logger remains active");
    }
}
