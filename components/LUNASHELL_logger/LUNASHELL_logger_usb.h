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
