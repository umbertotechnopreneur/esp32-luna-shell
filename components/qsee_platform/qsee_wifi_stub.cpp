/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include <cstring>

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

namespace {

void fill_disabled_status(qsee_wifi_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    std::memset(status, 0, sizeof(*status));
    status->last_error = ESP_ERR_NOT_SUPPORTED;
}

}  // namespace

extern "C" esp_err_t qsee_wifi_init(void)
{
    qsee::WifiSnapshot snapshot{};
    snapshot.last_error = ESP_ERR_NOT_SUPPORTED;
    snapshot.health = qsee::DeviceHealth::Missing;
    qsee::DeviceSnapshotService::instance().reportWifi(snapshot);
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_wifi_connect(
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

extern "C" esp_err_t qsee_wifi_disconnect(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t qsee_wifi_start_scan(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" size_t qsee_wifi_get_networks(
    qsee_wifi_network_t *networks,
    size_t capacity
)
{
    (void)networks;
    (void)capacity;
    return 0;
}

extern "C" void qsee_wifi_get_status(qsee_wifi_status_t *status)
{
    fill_disabled_status(status);
}
