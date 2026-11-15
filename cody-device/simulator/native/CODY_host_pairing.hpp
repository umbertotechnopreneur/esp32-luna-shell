#pragma once

#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"

namespace CODY::host_onboarding {

// available: Explicit static Studio QR source, independent of simulated radio operation.
// Exception: No exceptions; retiring the source prevents reuse by the next case.
void configure_studio_pairing_qr(bool available);

// start_error: Exact result that the synthetic radio should return at admission.
// cleanup: Separate synthetic cleanup and DMA-reservation outcomes.
// ready: Explicit provisioning observation to publish after successful start.
// qr: Synthetic payload beginning SIMULATED:, never a real device credential.
// Exception: Rejects an invalid observation or QR before changing the current scenario.
esp_err_t configure_pairing(esp_err_t start_error, LUNASHELL_radio_cleanup_result_t cleanup,
    const LUNASHELL::ProvisioningSnapshot &ready, const char *qr);

} // namespace CODY::host_onboarding
