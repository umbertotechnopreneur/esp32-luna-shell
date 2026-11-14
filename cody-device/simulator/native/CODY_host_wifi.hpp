#pragma once

// Transitional, non-parity host scenario backend. This file does not run the
// production LUNASHELL_wifi.cpp owner. Replace it with ESP-IDF radio/event/NVS
// adapters under that unchanged owner before presenting Wi-Fi as device-faithful.

#include "LUNASHELL_device_snapshot.hpp"

namespace CODY::host_onboarding {

// result: Explicit synthetic terminal scan, with each SSID prefixed SIM-.
// Exception: Rejects invalid state, names or scan size without changing the scenario.
esp_err_t configure_wifi_scan(const LUNASHELL::WifiScanSnapshot &result);

// result: Explicit synthetic open-network outcome, including SIM- SSID and AP identity.
// error: Original simulated admission result, which may describe failure.
// Exception: Rejects an invalid connected scenario without changing the previous one.
esp_err_t configure_wifi_connection(const LUNASHELL::WifiSnapshot &result, esp_err_t error);

} // namespace CODY::host_onboarding

#if defined(_WIN32)
#define CODY_HOST_WIFI_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_WIFI_API extern "C" __attribute__((visibility("default")))
#endif

// No parameters: explicitly select a synthetic scenario with no access points.
// Exception: Returns the original scan-configuration or snapshot publication error.
CODY_HOST_WIFI_API esp_err_t cody_host_wifi_seed_empty_scan();

// No parameters: explicitly select one isolated SIM-CODY open-network scenario.
// Exception: Returns the first real configuration or host-storage publication error.
CODY_HOST_WIFI_API esp_err_t cody_host_wifi_seed_open_network();
