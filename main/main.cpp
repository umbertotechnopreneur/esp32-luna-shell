/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <atomic>
#include <exception>
#include <cstdlib>
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
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "LUNASHELL_app_identity.hpp"
#include "../components/LUNASHELL_boot/include/LUNASHELL_boot.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_logger.h"
#include "LUNASHELL_platform.h"

namespace {

constexpr char kTag[] = "LUNASHELL_main";
constexpr size_t kBrookesiaBootstrapStackSize = 16 * 1024;
constexpr uint32_t kBrookesiaReadyTimeoutMs = 10000;
static_assert(
    CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U >=
        kBrookesiaReadyTimeoutMs + 1000U,
    "Task WDT must outlive the UI readiness deadline with recovery margin"
);

std::unique_ptr<esp_brookesia::system::core::System> s_system;
lv_timer_t *s_display_liveness_timer;
StaticSemaphore_t s_brookesia_ready_signal_storage;
SemaphoreHandle_t s_brookesia_ready_signal;
std::atomic_bool s_brookesia_ready{false};

class StartupWatch final {
public:
    StartupWatch()
    {
        const esp_err_t result = LUNASHELL_liveness_watch_begin(
            LUNASHELL_LIVENESS_STARTUP
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Startup watchdog start failed: %s",
                esp_err_to_name(result)
            );
            std::abort();
        }
        active_ = true;
    }

    ~StartupWatch()
    {
        if (!active_) {
            return;
        }
        const esp_err_t result = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_STARTUP,
            ESP_FAIL
        );
        ESP_LOGE(
            kTag,
            "Startup exited before the application became ready: %s",
            esp_err_to_name(result)
        );
        esp_system_abort("LUNASHELL startup incomplete");
    }

    void progress(const char *stage) const
    {
        const esp_err_t result = LUNASHELL_liveness_watch_progress(
            LUNASHELL_LIVENESS_STARTUP
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Startup watchdog progress failed after %s: %s",
                stage,
                esp_err_to_name(result)
            );
            std::abort();
        }
    }

    void complete()
    {
        const esp_err_t result = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_STARTUP,
            ESP_OK
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Startup watchdog completion failed: %s",
                esp_err_to_name(result)
            );
            std::abort();
        }
        active_ = false;
    }

    StartupWatch(const StartupWatch &) = delete;
    StartupWatch &operator=(const StartupWatch &) = delete;

private:
    bool active_ = false;
};

void display_liveness_timer_callback(lv_timer_t *timer)
{
    (void)timer;
    const esp_err_t result = LUNASHELL_liveness_watch_progress(
        LUNASHELL_LIVENESS_DISPLAY
    );
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "LVGL worker watchdog progress failed: %s",
            esp_err_to_name(result)
        );
        std::abort();
    }
}

esp_err_t start_display_liveness_watch()
{
    if (s_display_liveness_timer != nullptr) {
        ESP_LOGE(kTag, "LVGL worker watchdog timer is already installed");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_DISPLAY
    );
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "LVGL worker watchdog start failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }

    result = bsp_display_lock(1000);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "LVGL worker watchdog timer lock failed: %s",
            esp_err_to_name(result)
        );
        return LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_DISPLAY,
            result
        );
    }
    s_display_liveness_timer = lv_timer_create(
        display_liveness_timer_callback,
        1000,
        nullptr
    );
    bsp_display_unlock();
    if (s_display_liveness_timer == nullptr) {
        ESP_LOGE(kTag, "LVGL worker watchdog timer allocation failed");
        return LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_DISPLAY,
            ESP_ERR_NO_MEM
        );
    }
    ESP_LOGI(kTag, "LVGL worker watchdog heartbeat installed");
    return ESP_OK;
}

void report_touch_activity(void *user_data)
{
    (void)user_data;
    LUNASHELL::DeviceSnapshotService::instance().reportTouchActivity();
    LUNASHELL_power_manager_note_activity();
}

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
#if CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI
    // Leave a fully repainted black frame immediately before System Core
    // replaces the boot screen with the single LUNASHELL application surface.
    const esp_err_t hide_result = LUNASHELL_boot_ui_hide();
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
            if (app.manifest.id != LUNASHELL::kAppManifestId) {
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
        s_brookesia_ready.store(false, std::memory_order_release);
        if (xSemaphoreGive(s_brookesia_ready_signal) != pdTRUE) {
            ESP_LOGE(kTag, "Brookesia failure signal could not be published");
            std::abort();
        }
        return;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    const esp_err_t rotation_result =
        LUNASHELL_display_apply_saved_auto_rotation();
    if (rotation_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Saved display auto-rotation could not start: %s",
            esp_err_to_name(rotation_result)
        );
        s_brookesia_ready.store(false, std::memory_order_release);
        if (xSemaphoreGive(s_brookesia_ready_signal) != pdTRUE) {
            ESP_LOGE(kTag, "Brookesia rotation failure signal could not be published");
            std::abort();
        }
        return;
    }
#else
    ESP_LOGI(kTag, "Automatic rotation unavailable: IMU not compiled into this image");
#endif

    // Publish readiness only after every startup-critical UI stage succeeds.
    s_brookesia_ready.store(true, std::memory_order_release);
    if (xSemaphoreGive(s_brookesia_ready_signal) != pdTRUE) {
        ESP_LOGE(kTag, "Brookesia ready signal could not be published");
        std::abort();
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE
    // System Core owns the critical internal-memory reservations. Voice is
    // optional and starts only after the single-app UI is fully operational.
    report_optional_startup_failure("voice AFE", LUNASHELL_voice_afe_init());
#endif

    const UBaseType_t stack_remaining_words = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(
        kTag,
        "Brookesia bootstrap stack remaining=%luB internal-heap=%uB",
        static_cast<unsigned long>(stack_remaining_words * sizeof(StackType_t)),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
    );
}

}  // namespace

// No parameters: restore wake pins, then start the selected Brookesia firmware.
extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(LUNASHELL_power_prepare_boot());
    ESP_ERROR_CHECK(LUNASHELL_memory_telemetry_init());
    const esp_err_t logger_error = LUNASHELL_logger_init();
    if (logger_error != ESP_OK) {
        // Logging is optional: preserve its failure without stopping UI/hardware startup.
        ESP_LOGE(kTag, "Optional logger initialization failed (%s); startup continues",
            esp_err_to_name(logger_error));
        LUNASHELL::LoggerCaptureReport failure;
        failure.query_error = logger_error;
        failure.sampled_at_us = esp_timer_get_time();
        const esp_err_t publication_error =
            LUNASHELL::DeviceSnapshotService::instance().reportLoggerCapture(failure);
        if (publication_error != ESP_OK) {
            ESP_LOGE(kTag, "Optional logger startup failure publication also failed (%s)",
                esp_err_to_name(publication_error));
        }
    }
    LUNASHELL::build_mode::log_startup_mode();
    ESP_ERROR_CHECK(LUNASHELL_liveness_init());
    StartupWatch startup_watch;
    ESP_LOGI(kTag, "Firmware boot");
    ESP_LOGI(kTag, "Display contract: round 466x466, safe square 320x320 at (73,73)");

    ESP_LOGI(kTag, "Display: starting official Waveshare BSP");
    lv_display_t *display = nullptr;
    const esp_err_t display_result = bsp_display_start(&display);
    if (display_result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
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
    startup_watch.progress("display initialization");
    if (display == nullptr) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
            false,
            ESP_ERR_INVALID_STATE
        );
        ESP_LOGE(kTag, "Display startup returned no display handle");
        return;
    }
    LUNASHELL::DeviceSnapshotService::instance().reportDisplay(true, ESP_OK);
    const esp_err_t display_watch_result = start_display_liveness_watch();
    if (display_watch_result != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
            false,
            display_watch_result
        );
        return;
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI
    const esp_err_t boot_ui_result = LUNASHELL_boot_ui_show();
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
    LUNASHELL::DeviceSnapshotService::instance().reportTouch(
        touch != nullptr,
        touch != nullptr ? ESP_OK : ESP_ERR_NOT_FOUND
    );
    ESP_LOGI(kTag, "Touch CST9217: %s", touch != nullptr ? "ready" : "unavailable");
    if (touch != nullptr) {
        const esp_err_t touch_observer_result =
            bsp_display_set_touch_activity_callback(
                report_touch_activity,
                nullptr
            );
        if (touch_observer_result != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportTouch(
                false,
                touch_observer_result
            );
            ESP_LOGE(
                kTag,
                "Touch activity observer failed: %s",
                esp_err_to_name(touch_observer_result)
            );
            return;
        }
    }
    startup_watch.progress("display and touch setup");

    const esp_err_t hardware_result = LUNASHELL_hardware_init();
    report_optional_startup_failure("hardware inventory", hardware_result);
    startup_watch.progress("hardware inventory");
    if (hardware_result == ESP_OK) {
        // An unavailable calendar must not prevent the later first network sync.
        report_optional_startup_failure("initial hardware snapshot", LUNASHELL_hardware_refresh("boot"));
        startup_watch.progress("initial hardware snapshot");
    }

    const esp_err_t brightness_result = LUNASHELL_display_apply_saved_brightness();
    if (brightness_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Saved brightness could not be applied: %s",
            esp_err_to_name(brightness_result)
        );
        return;
    }
    startup_watch.progress("display settings");
    const esp_err_t wake_display_error = LUNASHELL_power_restore_wake_display();
    if (wake_display_error != ESP_OK) {
        ESP_LOGE(kTag, "Wake display fade failed (%s)", esp_err_to_name(wake_display_error));
        return;
    }

    report_optional_startup_failure("time", LUNASHELL_time_init("UTC0"));
    startup_watch.progress("time service");
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    const esp_err_t power_manager_result = LUNASHELL_power_manager_init();
    if (power_manager_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Power-management service failed: %s",
            esp_err_to_name(power_manager_result)
        );
        return;
    }
#else
    ESP_LOGI(kTag, "Power input service disabled by build profile");
#endif
    startup_watch.progress("power-management service");
#if CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_AUDIO
    report_optional_startup_failure("boot audio", LUNASHELL_boot_audio_play());
    startup_watch.progress("boot audio");
#endif
    report_optional_startup_failure("removable storage", LUNASHELL_storage_init());
    startup_watch.progress("removable storage");
    // Wi-Fi and BLE reserve a large part of internal SRAM on this S3. The
    // Settings action starts them explicitly after the UI is already usable.
    ESP_LOGI(kTag, "Wi-Fi/BLE startup deferred until Settings");
    report_optional_startup_failure(
        "pre-UI hardware refresh",
        LUNASHELL_hardware_refresh("pre-ui")
    );
    startup_watch.progress("pre-UI hardware refresh");

#if CONFIG_LUNASHELL_BUILD_ENABLE_BOOT_UI
    const esp_err_t ready_result = LUNASHELL_boot_ui_mark_ready();
    if (ready_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Boot UI ready transition failed: %s",
            esp_err_to_name(ready_result)
        );
        return;
    }
#endif

    // This temporary worker can commit flash-backed runtime state, so its stack
    // must stay internal. Measured peak use is below 10 KiB; 16 KiB preserves a
    // generous margin without requiring an unavailable 22 KiB contiguous block.
    s_brookesia_ready_signal = xSemaphoreCreateBinaryStatic(
        &s_brookesia_ready_signal_storage
    );
    if (s_brookesia_ready_signal == nullptr) {
        ESP_LOGE(kTag, "Brookesia ready signal creation failed");
        return;
    }
    s_brookesia_ready.store(false, std::memory_order_release);

    try {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = kBrookesiaBootstrapStackSize,
            .stack_in_ext = false,
        });
        boost::thread(brookesia_task).detach();
        startup_watch.progress("Brookesia task creation");
    } catch (const std::exception &error) {
        ESP_LOGE(
            kTag,
            "Brookesia bootstrap task creation failed: %s "
            "(internal-free=%uB largest=%uB requested=%uB)",
            error.what(),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            static_cast<unsigned int>(kBrookesiaBootstrapStackSize)
        );
        return;
    }

    if (xSemaphoreTake(
            s_brookesia_ready_signal,
            pdMS_TO_TICKS(kBrookesiaReadyTimeoutMs)
        ) != pdTRUE) {
        ESP_LOGE(
            kTag,
            "Brookesia readiness exceeded %lu ms",
            static_cast<unsigned long>(kBrookesiaReadyTimeoutMs)
        );
        return;
    }
    if (!s_brookesia_ready.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Brookesia task reported application startup failure");
        return;
    }
#if CONFIG_LUNASHELL_BUILD_PROFILE_UI_MINIMAL && \
    (CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY || \
     CONFIG_LUNASHELL_BUILD_ENABLE_GNSS || \
     CONFIG_LUNASHELL_BUILD_ENABLE_IMU || \
     (CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE))
    // Board services outlive the shell's GUI lifecycle. Start their sampling
    // here, after hardware and application readiness, even with no device pages.
    const esp_err_t sampling_result = LUNASHELL_hardware_start_full_snapshot_monitor();
    if (sampling_result != ESP_OK) {
        ESP_LOGE(kTag, "Board service sampling failed: %s", esp_err_to_name(sampling_result));
        return;
    }
    startup_watch.progress("board service sampling");
#endif
    startup_watch.complete();
    ESP_LOGI(kTag, "Firmware startup completed with application ready");
    report_optional_startup_failure("boot log archival", LUNASHELL_logger_finish_boot_capture());
}
