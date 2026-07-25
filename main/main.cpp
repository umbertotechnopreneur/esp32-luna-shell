/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <memory>
#include <string_view>
#include <utility>

#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/system_super.hpp"
#include "bsp/esp-bsp.h"
#include "boost/thread.hpp"
#include "esp_heap_caps.h"
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

void report_optional_startup_failure(
    std::string_view stage,
    esp_err_t result
)
{
    if (result == ESP_OK) {
        return;
    }
    ESP_LOGE(
        kTag,
        "Optional startup stage '%.*s' unavailable: %s; "
        "continuing by explicit startup policy",
        static_cast<int>(stage.size()),
        stage.data(),
        esp_err_to_name(result)
    );
}

bool start_brookesia()
{
    // Leave a fully repainted black frame immediately before System Super
    // replaces the boot screen with its own shell and application surfaces.
    const esp_err_t hide_result = qsee_boot_ui_hide();
    if (hide_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI handoff failed: %s",
            esp_err_to_name(hide_result)
        );
        return false;
    }

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
            ESP_LOGE(kTag, "Application start failed: %s", app_start_result.error().c_str());
            s_system->deinit();
            s_system.reset();
            return false;
        }
        ESP_LOGI(
            kTag,
            "Brookesia System Super ready; application id=%lu",
            static_cast<unsigned long>(app.app_id)
        );
        return true;
    }

    ESP_LOGE(kTag, "Application provider was not installed by Brookesia System Core");
    s_system->deinit();
    s_system.reset();
    return false;
}

void brookesia_task()
{
    if (!start_brookesia()) {
        ESP_LOGE(kTag, "Brookesia launch failed; canonical logger remains active");
        return;
    }

    const esp_err_t rotation_result =
        qsee_display_apply_saved_auto_rotation();
    if (rotation_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Saved display auto-rotation could not start: %s",
            esp_err_to_name(rotation_result)
        );
        return;
    }

    const UBaseType_t stack_remaining_words = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(
        kTag,
        "Brookesia bootstrap stack remaining=%luB internal-heap=%uB",
        static_cast<unsigned long>(stack_remaining_words * sizeof(StackType_t)),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
    );
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(qsee_logger_init());
    ESP_LOGI(kTag, "Firmware boot");
    ESP_LOGI(kTag, "Display contract: round 466x466, safe square 320x320 at (73,73)");

    ESP_LOGI(kTag, "Display: starting official Waveshare BSP");
    lv_display_t *display = nullptr;
    const esp_err_t display_result = bsp_display_start(&display);
    if (display_result != ESP_OK) {
        qsee::DeviceSnapshotService::instance().reportDisplay(
            false,
            display_result
        );
        ESP_LOGE(
            kTag,
            "Display initialization failed: %s",
            esp_err_to_name(display_result)
        );
        return;
    }
    if (display == nullptr) {
        qsee::DeviceSnapshotService::instance().reportDisplay(
            false,
            ESP_ERR_INVALID_STATE
        );
        ESP_LOGE(kTag, "Display startup returned no display handle");
        return;
    }
    qsee::DeviceSnapshotService::instance().reportDisplay(true, ESP_OK);

    const esp_err_t boot_ui_result = qsee_boot_ui_show();
    if (boot_ui_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI initialization failed: %s",
            esp_err_to_name(boot_ui_result)
        );
        return;
    }

    lv_indev_t *touch = bsp_display_get_input_dev();
    qsee::DeviceSnapshotService::instance().reportTouch(
        touch != nullptr,
        touch != nullptr ? ESP_OK : ESP_ERR_NOT_FOUND
    );
    ESP_LOGI(kTag, "Touch CST9217: %s", touch != nullptr ? "ready" : "unavailable");

    const esp_err_t hardware_result = qsee_hardware_init();
    report_optional_startup_failure("hardware inventory", hardware_result);

    const esp_err_t brightness_result = qsee_display_apply_saved_brightness();
    if (brightness_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Saved brightness could not be applied: %s",
            esp_err_to_name(brightness_result)
        );
        return;
    }

    report_optional_startup_failure("time", qsee_time_init("UTC0"));
    report_optional_startup_failure("power inputs", qsee_power_inputs_init());
    report_optional_startup_failure("boot audio", qsee_boot_audio_play());
    report_optional_startup_failure("removable storage", qsee_storage_init());
    // Wi-Fi and BLE reserve a large part of internal SRAM on this S3. The
    // Settings action starts them explicitly after the UI is already usable.
    ESP_LOGI(kTag, "Wi-Fi/BLE startup deferred until Settings");
    report_optional_startup_failure(
        "pre-UI hardware refresh",
        qsee_hardware_refresh("pre-ui")
    );

    const esp_err_t ready_result = qsee_boot_ui_mark_ready();
    if (ready_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI ready transition failed: %s",
            esp_err_to_name(ready_result)
        );
        return;
    }

    // Match the upstream System Super setup: reserve its deep C++ stack in PSRAM.
    {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = 40 * 1024,
        });
        boost::thread(brookesia_task).detach();
    }
}
