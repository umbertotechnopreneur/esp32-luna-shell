/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <cstdlib>
#include <string_view>

#include "esp_log.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_logger.h"
#include "LUNASHELL_platform.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API
#include "LUNASHELL_http_server.hpp"
#endif

namespace {

constexpr char kTag[] = "LUNASHELL_main";

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
            ESP_OK
        );
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Startup watchdog release failed: %s",
                esp_err_to_name(result)
            );
            std::abort();
        }
    }

    // stage: Startup stage completed before renewing the startup watchdog.
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

    StartupWatch(const StartupWatch &) = delete;
    StartupWatch &operator=(const StartupWatch &) = delete;

private:
    bool active_ = false;
};

// stage: Optional service whose startup failure is allowed by this orchestrator.
// result: Real startup result retained by the service and its snapshot.
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
        "Optional startup stage '%.*s' unavailable: %s; continuing by explicit startup policy",
        static_cast<int>(stage.size()),
        stage.data(),
        esp_err_to_name(result)
    );
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(LUNASHELL_memory_telemetry_init());
    ESP_ERROR_CHECK(LUNASHELL_logger_init());
    ESP_ERROR_CHECK(LUNASHELL_liveness_init());
    StartupWatch startup_watch;
    ESP_LOGI(kTag, "Firmware boot");
    ESP_LOGI(kTag, "Build profile: standalone hardware runtime without the UI backend");

    ESP_ERROR_CHECK(LUNASHELL_hardware_init());
    startup_watch.progress("hardware inventory");
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY || CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    ESP_ERROR_CHECK(LUNASHELL_display_runtime_init());
    startup_watch.progress("standalone display and touch");
#else
    LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    LUNASHELL::DeviceSnapshotService::instance().reportTouch(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    ESP_ERROR_CHECK(LUNASHELL_display_apply_saved_brightness());
    ESP_ERROR_CHECK(LUNASHELL_display_apply_saved_auto_rotation());
    startup_watch.progress("saved display settings");
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE
    report_optional_startup_failure("voice AFE", LUNASHELL_voice_afe_init());
    startup_watch.progress("voice AFE");
#endif
    report_optional_startup_failure("time", LUNASHELL_time_init("UTC0"));
    startup_watch.progress("time service");
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    ESP_ERROR_CHECK(LUNASHELL_power_manager_init());
    startup_watch.progress("power-management service");
#endif
    report_optional_startup_failure("removable storage", LUNASHELL_storage_init());
    startup_watch.progress("removable storage");
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    report_optional_startup_failure("Wi-Fi/BLE", LUNASHELL_radios_init());
    startup_watch.progress("Wi-Fi/BLE services");
#elif CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    report_optional_startup_failure("Wi-Fi", LUNASHELL_wifi_init());
    startup_watch.progress("Wi-Fi service");
#elif CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    report_optional_startup_failure("BLE", LUNASHELL_ble_init());
    startup_watch.progress("BLE service");
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API
    report_optional_startup_failure("HTTP API", LUNASHELL::httpServerInit());
    startup_watch.progress("HTTP API service");
#endif
    report_optional_startup_failure(
        "standalone hardware refresh",
        LUNASHELL_hardware_refresh("standalone")
    );
    startup_watch.progress("standalone hardware refresh");
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY || \
    CONFIG_LUNASHELL_BUILD_ENABLE_GNSS || \
    CONFIG_LUNASHELL_BUILD_ENABLE_IMU || \
    (CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && !CONFIG_LUNASHELL_BUILD_AUDIO_INPUT_DRIVER_NONE)
    ESP_ERROR_CHECK(LUNASHELL_hardware_start_full_snapshot_monitor());
    startup_watch.progress("background hardware sampling");
#endif
    ESP_LOGI(kTag, "Standalone hardware startup completed");
}
