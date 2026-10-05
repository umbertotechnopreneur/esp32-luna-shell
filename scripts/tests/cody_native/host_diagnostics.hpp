#pragma once

#include "LUNASHELL_device_snapshot.hpp"

namespace HostDiagnostics {

// Host-only fixture state; it is never linked into the firmware snapshot owner.
extern LUNASHELL::DiagnosticsObservation observation;
extern esp_err_t read_error;

}  // namespace HostDiagnostics
