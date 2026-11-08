/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <stdarg.h>
#include "LUNASHELL_logger.h"

// No parameters: install the native USB driver and its bounded command receiver.
// Returns the original driver/task initialization error.
esp_err_t LUNASHELL_logger_usb_init(void);

// format is an ESP-IDF log format string.
// args contains the caller-owned formatting arguments.
// Normal logging never waits for an open terminal; dropped output is counted.
int LUNASHELL_logger_usb_vprintf(const char *format, va_list args);

// No parameters: return measured best-effort USB output loss since this boot.
uint32_t LUNASHELL_logger_usb_dropped_lines(void);
