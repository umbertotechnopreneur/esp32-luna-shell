/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 */

#pragma once

#include <cstddef>

#include "esp_err.h"

namespace qsee {

constexpr unsigned short kHttpServerPort = 8080;
constexpr std::size_t kHttpApiKeyMaxLength = 63;

/** Start the read-only device HTTP API and bind it to the station lifecycle. */
esp_err_t httpServerInit();

/** Copy the persisted API key for an already-authorized diagnostic surface. */
bool httpServerGetApiKey(char *buffer, std::size_t capacity);

/** Reload the key after the user settings surface changes `ApiKey`. */
esp_err_t httpServerReloadApiKey();

}  // namespace qsee
