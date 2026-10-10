#pragma once

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// component: Original owner-provided measurement whose numeric history remains untouched.
// now_us: Explicit monotonic observation time, independent of acquisition.
// ttl_us: Maximum age admitted by the caller's acquisition policy.
void age_measured_sample(ComponentSnapshot &component, int64_t now_us, int64_t ttl_us);

// snapshot: Canonical cadence and validity used by full and bounded projections.
int64_t measured_sample_ttl_us(const DeviceSnapshot &snapshot);

// snapshot: Owner-provided observations; original sample timestamps remain unchanged.
// now_us: Explicit monotonic observation time; no clock or hardware access occurs here.
void apply_measured_sample_aging(DeviceSnapshot &snapshot, int64_t now_us);
}
