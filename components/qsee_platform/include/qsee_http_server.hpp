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

}  // namespace qsee
