#pragma once

#include "LUNASHELL_snapshot_internal.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace LUNASHELL::snapshot_detail {
// No parameters: borrow the acquisition owner's static refresh barrier.
// Exception: Returns null if FreeRTOS cannot create the mutex.
SemaphoreHandle_t ensure_refresh_mutex();
// mutex: Borrowed refresh barrier held by the calling acquisition task.
// operation: Diagnostic identity for an ownership failure.
// Exception: A rejected release stops execution because ownership is uncertain.
esp_err_t give_refresh_mutex(SemaphoreHandle_t mutex, const char *operation);
// component: Hardware component whose canonical publication was attempted.
// phase: Acquisition label; null retains the existing unknown-phase diagnostic.
// sample_error: Original acquisition result, which has precedence.
// publication_error: Result of the synchronous canonical publication.
esp_err_t sample_publication_result(const char *component, const char *phase,
                                  esp_err_t sample_error, esp_err_t publication_error);
// error: Original operation failure to retain.
// aggregate: Caller-owned result; the first error is never replaced.
void record_first_error(esp_err_t error, esp_err_t *aggregate);
// phase: Existing cadence label selecting quiet high-frequency diagnostics.
bool high_frequency_sample_phase(const char *phase);
// No parameters: observe completed owner initialization without acquiring sensors.
bool hardware_initialized();
}  // namespace LUNASHELL::snapshot_detail
