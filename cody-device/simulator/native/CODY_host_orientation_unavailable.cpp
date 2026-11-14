// The physical rotation owner remains unavailable. Static Studio sessions can
// explicitly set synthetic logical orientation through the canonical host owner.
#include "LUNASHELL_platform.h"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "CODY_host_onboarding_snapshot.hpp"
#include "esp_log.h"
#include "esp_timer.h"

// enabled: Requested firmware auto-rotation policy.
// Exception: Live sessions return NOT_SUPPORTED; static sessions preserve NVS/readback errors.
extern "C" esp_err_t LUNASHELL_display_set_auto_rotation_enabled(bool enabled)
{
    if (cody_host_manual_time_us.load(std::memory_order_acquire) > 0) {
        auto &store = LUNASHELL::UserSettingsService::instance();
        auto error = store.setBool("auto_rotate", enabled);
        bool readback = !enabled;
        if (error == ESP_OK) error = store.getBool("auto_rotate", &readback);
        if (error == ESP_OK && readback != enabled) error = ESP_ERR_INVALID_RESPONSE;
        LUNASHELL::CodyOptionsObservation options{};
        if (error == ESP_OK) error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
        if (error == ESP_OK) error = CODY::host_onboarding::publish_static_orientation(enabled,
            options.orientation.rendering_rotation_degrees);
        return error;
    }
    ESP_LOGW("CODY_host_orientation", "Auto-rotation request %u unavailable in host hardware owner", enabled);
    return ESP_ERR_NOT_SUPPORTED;
}

// degrees: Requested physical panel/touch rotation in cardinal degrees.
// Exception: Returns the original invalid-argument class or NOT_SUPPORTED;
// static sessions preserve NVS/readback errors and publish only synthetic orientation.
extern "C" esp_err_t LUNASHELL_display_set_rotation_degrees(uint32_t degrees)
{
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) return ESP_ERR_INVALID_ARG;
    if (cody_host_manual_time_us.load(std::memory_order_acquire) > 0) {
        if (degrees != 0 && degrees != 180) return ESP_ERR_NOT_SUPPORTED;
        auto &store = LUNASHELL::UserSettingsService::instance();
        auto error = store.setUInt32("display_rot", degrees);
        uint32_t readback = UINT32_MAX;
        if (error == ESP_OK) error = store.getUInt32("display_rot", &readback);
        if (error == ESP_OK && readback != degrees) error = ESP_ERR_INVALID_RESPONSE;
        LUNASHELL::CodyOptionsObservation options{};
        if (error == ESP_OK) error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(options);
        if (error == ESP_OK) error = CODY::host_onboarding::publish_static_orientation(
            options.orientation.auto_rotation_enabled, static_cast<uint16_t>(degrees));
        return error;
    }
    ESP_LOGW("CODY_host_orientation", "Display rotation %lu unavailable in host hardware owner",
        static_cast<unsigned long>(degrees));
    return ESP_ERR_NOT_SUPPORTED;
}
