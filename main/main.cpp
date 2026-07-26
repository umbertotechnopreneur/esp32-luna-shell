/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <exception>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/system_core.hpp"
#include "bsp/esp-bsp.h"
#include "boost/thread.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../components/qsee_app/include/qsee_app.hpp"
#include "../components/qsee_boot/include/qsee_boot.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {

constexpr char kTag[] = "qsee_main";

std::unique_ptr<esp_brookesia::system::core::System> s_system;

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
#if CONFIG_QSEE_BUILD_ENABLE_BOOT_UI
    // Leave a fully repainted black frame immediately before System Core
    // replaces the boot screen with the single QSee application surface.
    const esp_err_t hide_result = qsee_boot_ui_hide();
    if (hide_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI handoff failed: %s",
            esp_err_to_name(hide_result)
        );
        return false;
    }
#endif

    try {
        s_system = std::make_unique<esp_brookesia::system::core::System>();

        esp_brookesia::system::core::System::Config config;
        config.gui_backend =
            std::make_unique<esp_brookesia::gui::lvgl::Backend>();
        config.environment.width_px = 466;
        config.environment.height_px = 466;
        config.environment.density = 1.0F;
        config.environment.font_scale = 1.0F;
        config.environment.language = "en";
        config.startup_overlay.enabled = false;
        config.app_launch_transition.enabled = false;
        config.install_registered_apps = true;
        config.install_package_apps = false;
        config.storage.default_install_target =
            esp_brookesia::system::core::StorageInstallTarget::Internal;

        auto init_result = s_system->init(std::move(config));
        if (!init_result) {
            ESP_LOGE(kTag, "Brookesia System Core init failed: %s", init_result.error().c_str());
            s_system.reset();
            return false;
        }

        auto start_result = s_system->start();
        if (!start_result) {
            ESP_LOGE(kTag, "Brookesia System Core start failed: %s", start_result.error().c_str());
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
                "Brookesia System Core single-app runtime ready; application id=%lu",
                static_cast<unsigned long>(app.app_id)
            );
            return true;
        }

        ESP_LOGE(kTag, "Application provider was not installed by Brookesia System Core");
        s_system->deinit();
        s_system.reset();
        return false;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(
            kTag,
            "Brookesia launch aborted: memory allocation failed"
        );
    } catch (const std::exception &error) {
        ESP_LOGE(
            kTag,
            "Brookesia launch aborted by exception: %s",
            error.what()
        );
    } catch (...) {
        ESP_LOGE(kTag, "Brookesia launch aborted by an unknown exception");
    }

    if (s_system != nullptr) {
        s_system->deinit();
        s_system.reset();
    }
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

#if CONFIG_QSEE_BUILD_ENABLE_BOOT_UI
    const esp_err_t boot_ui_result = qsee_boot_ui_show();
    if (boot_ui_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI initialization failed: %s",
            esp_err_to_name(boot_ui_result)
        );
        return;
    }
#endif

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
#if CONFIG_QSEE_BUILD_ENABLE_BOOT_AUDIO
    report_optional_startup_failure("boot audio", qsee_boot_audio_play());
#endif
    report_optional_startup_failure("removable storage", qsee_storage_init());
    // Wi-Fi and BLE reserve a large part of internal SRAM on this S3. The
    // Settings action starts them explicitly after the UI is already usable.
    ESP_LOGI(kTag, "Wi-Fi/BLE startup deferred until Settings");
    report_optional_startup_failure(
        "pre-UI hardware refresh",
        qsee_hardware_refresh("pre-ui")
    );

#if CONFIG_QSEE_BUILD_ENABLE_BOOT_UI
    const esp_err_t ready_result = qsee_boot_ui_mark_ready();
    if (ready_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI ready transition failed: %s",
            esp_err_to_name(ready_result)
        );
        return;
    }
#endif

    // Keep the temporary bootstrap stack internal for the flash-backed runtime
    // initialization. The large declarative GUI worker stays in PSRAM; QSee
    // settings and Brookesia Storage own separate internal-stack executors.
    {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = 24 * 1024,
            .stack_in_ext = false,
        });
        boost::thread(brookesia_task).detach();
    }
}
