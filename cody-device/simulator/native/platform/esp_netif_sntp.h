#pragma once
#include <sys/time.h>
#include "esp_err.h"

// Host transport contract only; these values do not represent a network sync.
struct esp_sntp_config_t {
    bool start;
    void (*sync_cb)(struct timeval *);
};
#define ESP_SNTP_SERVER_LIST(...) 0
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(count, servers) esp_sntp_config_t{false, nullptr}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) esp_sntp_config_t{false, nullptr}
extern "C" esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
extern "C" esp_err_t esp_netif_sntp_start();
extern "C" void esp_netif_sntp_deinit();
