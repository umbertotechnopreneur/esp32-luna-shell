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
    ESP_LOGI(kTag, "Build profile: headless runtime");

    report_optional_startup_failure("hardware inventory", LUNASHELL_hardware_init());
    startup_watch.progress("hardware inventory");
    LUNASHELL::DeviceSnapshotService::instance().reportDisplay(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    LUNASHELL::DeviceSnapshotService::instance().reportTouch(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_AFE
    report_optional_startup_failure("voice AFE", LUNASHELL_voice_afe_init());
    startup_watch.progress("voice AFE");
#endif
    report_optional_startup_failure("time", LUNASHELL_time_init("UTC0"));
    startup_watch.progress("time service");
    report_optional_startup_failure(
        "power-management service",
        LUNASHELL_power_manager_init()
    );
    startup_watch.progress("power-management service");
    report_optional_startup_failure("removable storage", LUNASHELL_storage_init());
    startup_watch.progress("removable storage");
    ESP_LOGI(kTag, "Wi-Fi/BLE startup deferred until Settings");
    report_optional_startup_failure(
        "headless hardware refresh",
        LUNASHELL_hardware_refresh("headless")
    );
    startup_watch.progress("headless hardware refresh");
}
