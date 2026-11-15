#pragma once

#include "LUNASHELL_device_snapshot.hpp"
#include "cody_host_studio.h"

// Simulator-only observation boundary for the unmodified onboarding owner.
// These records never enter the physical device or an authenticated CCP link.
namespace CODY::host_onboarding {

// snapshot: Explicit host-only scenario, never an observation of attached hardware.
// now_us: Monotonic scenario time in the same domain as the original samples.
// Exception: Rejects invalid time before replacing any onboarding projection.
esp_err_t publish_device_snapshot(const LUNASHELL::DeviceSnapshot &snapshot, int64_t now_us);

// observation: Receives the actual synthetic host playback publication, if any.
// Exception: Returns the original unavailable or publication error.
esp_err_t read_playback(LUNASHELL::PlaybackSnapshot &observation);

// automatic: Explicit synthetic preference; no gravity measurement is invented.
// degrees: Manual logical orientation, 0 or 180.
// Exception: Rejects unsupported geometry or an unseeded canonical host owner.
esp_err_t publish_static_orientation(bool automatic, uint16_t degrees);

// request: ABI-checked explicit page and synthetic service input, without mutation.
// Exception: Rejects invalid page fields or unsupported service outcomes.
esp_err_t validate_studio_case(const CodyHostStudioCase &request);

// request: Validated synthetic case; replaces all optional Studio page observations.
// Exception: Preserves unavailable canonical owner errors; no host hardware or network is read.
esp_err_t publish_studio_case(const CodyHostStudioCase &request);

// available: Explicit static Studio pairing-code source, independent of any link action.
// Exception: No exceptions; retiring it prevents the next case from copying a stale code.
void configure_studio_codex_pairing_code(bool available);

} // namespace CODY::host_onboarding
