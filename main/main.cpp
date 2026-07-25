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

bool start_brookesia(lv_indev_t *touch)
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

    // Bind Brookesia to the display already created by the Waveshare BSP.
    // Without this, the framework falls back to its generic display context.
    auto *phone = new (std::nothrow) Phone(lv_disp_get_default());
    if (phone == nullptr) {
        ESP_LOGE(TAG, "ESP-Brookesia: phone allocation failed");
        return false;
    }

    auto phone_stylesheet = ESP_BROOKESIA_PHONE_480_480_DARK_STYLESHEET();
    phone_stylesheet.core.screen_size = esp_brookesia::gui::StyleSize::RECT(466, 466);
    auto &status_bar = phone_stylesheet.display.status_bar.data;
    status_bar.main.size = esp_brookesia::gui::StyleSize::RECT(264, 30);
    status_bar.main.size.radius = 15;
    status_bar.main.offset_y = 43;
    status_bar.main.background_color = esp_brookesia::gui::StyleColor::COLOR(0x03070B);
    status_bar.main.background_gradient_color = esp_brookesia::gui::StyleColor::COLOR(0x0B222A);
    status_bar.main.enable_background_gradient = true;
    status_bar.main.text_font = esp_brookesia::gui::StyleFont::SIZE(12);
    status_bar.main.text_color = esp_brookesia::gui::StyleColor::COLOR(0xECF8FA);
    status_bar.area.num = 1;
    status_bar.area.data[0] = {
        .size = esp_brookesia::gui::StyleSize::RECT_PERCENT(100, 100),
        .layout_column_align = esp_brookesia::systems::phone::StatusBar::AreaAlign::CENTER,
        .layout_column_start_offset = 0,
        .layout_column_pad = 4,
    };
    // Text telemetry is denser and clearer than the stock phone icons at this
    // diameter; battery, radios, GPS and SD state share one compact line.
    status_bar.flags.enable_battery_icon = 0;
    status_bar.flags.enable_battery_icon_common_size = 0;
    status_bar.flags.enable_battery_label = 0;
    status_bar.flags.enable_wifi_icon = 0;
    status_bar.flags.enable_wifi_icon_common_size = 0;
    status_bar.flags.enable_clock = 0;
    // The circular device has no recents gesture surface. Disabling that
    // unused 480px layout also avoids allocating its snapshot layers.
    phone_stylesheet.display.flags.enable_recents_screen = 0;
    // The circular QSee UI keeps its own BACK control. Avoid allocating the
    // Brookesia bottom bar so a gesture cannot reveal it again.
    phone_stylesheet.display.flags.enable_navigation_bar = 0;
    phone_stylesheet.manager.flags.enable_recents_screen_snapshot_drag = 0;
    if (!phone->addStylesheet(phone_stylesheet) ||
        !phone->activateStylesheet(phone_stylesheet)) {
        ESP_LOGE(TAG, "ESP-Brookesia: compatible phone stylesheet setup failed");
        return false;
    }

    if (touch == nullptr || !phone->setTouchDevice(touch)) {
        ESP_LOGE(TAG, "ESP-Brookesia: touch device setup failed");
        return false;
    }

    // The boot task now owns the LVGL lock, so the overlay can be removed
    // reliably before Brookesia starts creating application objects.
    qsee_boot_ui_hide();

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
    if (!start_brookesia(static_cast<lv_indev_t *>(arg))) {
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
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_time_init("UTC0"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_power_inputs_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_boot_audio_play());
    ESP_ERROR_CHECK_WITHOUT_ABORT(qsee_storage_init());
    // Keep the display DMA path and Welcome task alive. Wi-Fi/BLE reserve most
    // internal SRAM on this S3 and are started later from the settings flow.
    ESP_LOGI(TAG, "Wi-Fi/BLE startup deferred until Settings");
    (void)qsee_hardware_refresh("pre-ui");

    qsee_boot_ui_mark_ready();

    const BaseType_t task_result = xTaskCreatePinnedToCore(
        brookesia_task,
        "qsee_brookesia",
        12U * 1024U,
        touch,
        5,
        nullptr,
        1
    );
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "ESP-Brookesia task creation failed; canonical logger remains active");
    }
}
