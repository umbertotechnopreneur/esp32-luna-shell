#pragma once

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL::snapshot_detail {
inline constexpr uint32_t kDiagnosticPeriodMinMs = 100;
inline constexpr uint32_t kDiagnosticPeriodMaxMs = 2000;
inline constexpr uint32_t kImuSmoothingMaxPercent = 90;

using SnapshotMutation = void (*)(DeviceSnapshot &, void *);
enum class SnapshotPublicationPolicy { Required, Optional };
// mutation: Synchronous update applied under the canonical snapshot owner's lock.
// context: Borrowed stack state, valid only for this synchronous call; never retained.
// policy: Required publication retains fatal invariants; optional logging returns real failures.
// Exception: Returns actual mutation/ownership errors for an optional component.
esp_err_t apply_snapshot_mutation(
    SnapshotMutation mutation, void *context, SnapshotPublicationPolicy policy);

// update: Stack-owned callable; this boundary performs no type-erasure allocation.
// policy: Whether the component can continue after returning its publication failure.
// Exception: Returns actual optional publication errors without stopping the device.
template <typename Update>
esp_err_t update_snapshot(
    Update update, SnapshotPublicationPolicy policy = SnapshotPublicationPolicy::Required)
{
    return apply_snapshot_mutation(
        [](DeviceSnapshot &snapshot, void *context) {
            (*static_cast<Update *>(context))(snapshot);
        },
        &update,
        policy
    );
}

// ready: Hardware readiness confirmed by its owner.
// error: Real operation result, distinguishing missing and disabled components.
DeviceHealth health_for(bool ready, esp_err_t error);
// component: Canonical published component state.
// ready: Hardware readiness confirmed by its owner.
// error: Original operation result.
void set_component(ComponentSnapshot &component, bool ready, esp_err_t error);
// component: Successful current sample to publish.
// attempted_at_us: Real acquisition start timestamp.
// completed_at_us: Real successful acquisition completion timestamp.
void mark_sample_success(ComponentSnapshot &component, int64_t attempted_at_us, int64_t completed_at_us);
// component: Successful current sample to publish.
// sampled_at_us: Hardware sample timestamp shared by attempt and success.
void mark_sample_success(ComponentSnapshot &component, int64_t sampled_at_us);
// component: Failed sample retaining the timestamp of any previous success.
// error: Original hardware or service error.
// attempted_at_us: Real timestamp of the rejected current attempt.
void mark_sample_failure(ComponentSnapshot &component, esp_err_t error, int64_t attempted_at_us);
// operation: Component operation that could not publish its canonical result.
// error: Real publication failure; failed publication is a fatal invariant.
void log_snapshot_publication_failure(const char *operation, esp_err_t error);
}  // namespace LUNASHELL::snapshot_detail
