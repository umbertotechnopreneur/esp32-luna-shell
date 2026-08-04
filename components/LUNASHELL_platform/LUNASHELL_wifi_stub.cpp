/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <cstring>

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

namespace {

void fill_disabled_status(LUNASHELL_wifi_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    std::memset(status, 0, sizeof(*status));
    status->last_error = ESP_ERR_NOT_SUPPORTED;
}

}  // namespace

extern "C" esp_err_t LUNASHELL_wifi_init(void)
{
    LUNASHELL::WifiSnapshot snapshot{};
    snapshot.last_error = ESP_ERR_NOT_SUPPORTED;
    snapshot.health = LUNASHELL::DeviceHealth::Missing;
    LUNASHELL::DeviceSnapshotService::instance().reportWifi(snapshot);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_connect(
    const char *ssid,
    const char *password,
    bool persist
)
{
    (void)ssid;
    (void)password;
    (void)persist;
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_disconnect(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t LUNASHELL_wifi_start_scan(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" size_t LUNASHELL_wifi_get_networks(
    LUNASHELL_wifi_network_t *networks,
    size_t capacity
)
{
    (void)networks;
    (void)capacity;
    return 0;
}

extern "C" void LUNASHELL_wifi_get_status(LUNASHELL_wifi_status_t *status)
{
    fill_disabled_status(status);
}
