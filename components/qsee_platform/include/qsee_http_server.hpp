/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

// Read-only HTTP API entry point for device diagnostics.
// Owns server startup and shutdown around the station lifecycle.

#pragma once

#include <cstddef>

#include "esp_err.h"

namespace qsee {

constexpr unsigned short kHttpServerPort = 8080;
constexpr std::size_t kHttpApiKeyMaxLength = 63;

/** Start the read-only device HTTP API and bind it to the station lifecycle. */
esp_err_t httpServerInit();
/** Persist the HTTP preference and apply it immediately when Wi-Fi is usable. */
esp_err_t httpServerSetEnabled(bool enabled);

}  // namespace qsee
