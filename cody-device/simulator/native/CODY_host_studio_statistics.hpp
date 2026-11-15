#pragma once

#include "LUNASHELL_device_snapshot.hpp"
#include "cody_host_studio.h"

// Synthetic account/interval observations live in the host service owner. The
// production renderer consumes only DeviceSnapshotService projections.
namespace CODY::host_studio_statistics {

// request: Explicit bounded counter input, with a stable textual target.
// Exception: Rejects unrelated payload, invalid fields, dates and unavailable clocks before mutation.
esp_err_t validate(const CodyHostStudioCase &request);

// request: Validated case published after the canonical host link owner.
// Exception: Preserves validation, clock, allocation and snapshot publication failures.
esp_err_t publish(const CodyHostStudioCase &request);

// account: Receives a copied synthetic observation qualified by the canonical link lease.
// Exception: Returns NOT_SUPPORTED without modifying output outside account fixture cases.
esp_err_t read_account(LUNASHELL::CodexAccountSnapshot &account);

// intervals: Receives the explicit synthetic UTC-quarter series and its actual source state.
// Exception: Returns NOT_SUPPORTED without modifying output outside the history fixture.
esp_err_t read_intervals(LUNASHELL::CodexTokenIntervalsSnapshot &intervals);

// vouchers: Remains unchanged because expiry observations are not part of this fixture contract.
// Exception: Returns NOT_SUPPORTED rather than inventing expiry dates.
esp_err_t read_vouchers(LUNASHELL::CodexVoucherSnapshot &vouchers);

} // namespace CODY::host_studio_statistics
