/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <cstdlib>
#include <string_view>

#include "esp_log.h"
#include "esp_system.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_logger.h"
#include "LUNASHELL_platform.h"
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
#include "CODY_companion.h"
#include "CODY_poc.h"
#endif
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
        const esp_err_t result = finish();
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Startup watchdog release failed: %s",
                esp_err_to_name(result)
            );
            std::abort();
        }
    }

    // Finish the startup watchdog before the consumer displays a completed boot.
    esp_err_t finish()
    {
        if (!active_) {
            ESP_LOGE(kTag, "Startup watchdog finish rejected: %s",
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        const esp_err_t result = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_STARTUP, ESP_OK
        );
        if (result == ESP_OK) {
            active_ = false;
        } else {
            ESP_LOGE(kTag, "Startup watchdog finish failed: %s", esp_err_to_name(result));
        }
        return result;
    }

    // stage: Startup stage completed before renewing the startup watchdog.
    void progress(const char *stage) const
    {
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
        CODY_poc_boot_checkpoint(stage);
#endif
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

// No parameters: start the profile's owners in watchdog-tracked, fail-fast stages.
extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(LUNASHELL_power_prepare_boot());
    ESP_ERROR_CHECK(LUNASHELL_memory_telemetry_init());
    ESP_ERROR_CHECK(LUNASHELL_logger_init());
    LUNASHELL::build_mode::log_startup_mode();
    ESP_ERROR_CHECK(LUNASHELL_liveness_init());
    StartupWatch startup_watch;
    ESP_LOGI(kTag, "Firmware boot");
    ESP_LOGI(kTag, "Build profile: standalone hardware runtime without the UI backend");
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(CODY_poc_prepare_boot());
#endif

    ESP_ERROR_CHECK(LUNASHELL_hardware_init());
    startup_watch.progress("hardware inventory");
    // Driver startup is mandatory; sensor values (notably an unset RTC) are
    // optional observations with real errors. NTP can establish UTC later.
    report_optional_startup_failure("initial hardware snapshot", LUNASHELL_hardware_refresh("boot"));
    startup_watch.progress("initial hardware snapshot");
    // Start the optional SD owner before display/Cody startup can fail. Its
    // worker mounts asynchronously and replays the logger's bounded boot buffer.
    report_optional_startup_failure("removable storage", LUNASHELL_storage_init());
    startup_watch.progress("removable storage");
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
#if CONFIG_LUNASHELL_CODY_START_AUTO_ROTATION
    if (!LUNASHELL_display_get_auto_rotation_enabled()) {
        // This is an explicit selected Cody startup policy, not a missing-key
        // default or a repair: saved state has already passed strict validation.
        ESP_LOGI(kTag, "Cody startup policy: enabling shared cardinal auto-rotation");
        ESP_ERROR_CHECK(LUNASHELL_display_set_auto_rotation_enabled(true));
    }
#endif
    startup_watch.progress("saved display settings");
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        // The frame owner requires panel output enabled, but Wake must remain
        // invisible until Cody has completed its first real frame.
        ESP_ERROR_CHECK(LUNASHELL_display_prepare_dark_frame());
    }
#endif
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(CODY_companion_start());
    startup_watch.progress("Cody boot frame and touch consumer");
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    // Cody start returns only after its first real Wake/Idle frame retires.
    // Keep that ordering: restoring brightness earlier would expose stale pixels.
    ESP_ERROR_CHECK(LUNASHELL_power_restore_wake_display());
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
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    report_optional_startup_failure("diagnostics SD mirror", CODY_poc_configure_diagnostics());
    ESP_ERROR_CHECK(CODY_companion_boot_midpoint());
    startup_watch.progress("Cody local-services boot checkpoint");
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(LUNASHELL_radios_init());
#else
    report_optional_startup_failure("Wi-Fi/BLE", LUNASHELL_radios_init());
#endif
    startup_watch.progress("Wi-Fi/BLE services");
#elif CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(LUNASHELL_wifi_init());
#else
    report_optional_startup_failure("Wi-Fi", LUNASHELL_wifi_init());
#endif
    startup_watch.progress("Wi-Fi service");
#elif CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(LUNASHELL_ble_init());
#else
    report_optional_startup_failure("BLE", LUNASHELL_ble_init());
#endif
    startup_watch.progress("BLE service");
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_HTTP_API
    report_optional_startup_failure("HTTP API", LUNASHELL::httpServerInit());
    startup_watch.progress("HTTP API service");
#endif
    // The OOBE renderer submits actions later; this worker never opens BLE at boot.
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING
    ESP_ERROR_CHECK(LUNASHELL_pairing_service_init());
    startup_watch.progress("Cody pairing controller");
#endif
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && \
    CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
    report_optional_startup_failure(
        "Cody Codex link",
        LUNASHELL_codex_link_service_init()
    );
    startup_watch.progress("Cody Codex link");
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
    ESP_ERROR_CHECK(startup_watch.finish());
#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION
    ESP_ERROR_CHECK(CODY_companion_boot_completed());
    CODY_poc_boot_checkpoint("ready frame / mandatory startup complete");
    ESP_ERROR_CHECK(CODY_companion_demo_start());
    ESP_ERROR_CHECK(CODY_poc_play_eyes_open());
#endif
    ESP_LOGI(kTag, "Standalone hardware startup completed");
}
