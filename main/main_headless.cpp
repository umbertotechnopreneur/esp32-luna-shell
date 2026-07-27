/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <string_view>

#include "esp_log.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {

constexpr char kTag[] = "qsee_main";

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
    ESP_ERROR_CHECK(qsee_memory_telemetry_init());
    ESP_ERROR_CHECK(qsee_logger_init());
    ESP_LOGI(kTag, "Firmware boot");
    ESP_LOGI(kTag, "Build profile: headless runtime");

    report_optional_startup_failure("hardware inventory", qsee_hardware_init());
    qsee::DeviceSnapshotService::instance().reportDisplay(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
    qsee::DeviceSnapshotService::instance().reportTouch(
        false,
        ESP_ERR_NOT_SUPPORTED
    );
#if CONFIG_QSEE_BUILD_ENABLE_VOICE_AFE
    report_optional_startup_failure("voice AFE", qsee_voice_afe_init());
#endif
    report_optional_startup_failure("time", qsee_time_init("UTC0"));
    report_optional_startup_failure(
        "power-management service",
        qsee_power_manager_init()
    );
    report_optional_startup_failure("removable storage", qsee_storage_init());
    ESP_LOGI(kTag, "Wi-Fi/BLE startup deferred until Settings");
    report_optional_startup_failure(
        "headless hardware refresh",
        qsee_hardware_refresh("headless")
    );
}
