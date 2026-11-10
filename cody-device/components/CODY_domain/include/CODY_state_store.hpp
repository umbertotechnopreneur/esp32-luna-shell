
/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_capacity.hpp"
#include "CODY_state_receive.hpp"
#include "CODY_state_query.hpp"

namespace CODY::Domain {

struct StateRequest {
    SourceId source{};
    EpochId epoch{};
    uint64_t request_id = 0;
    uint64_t base_revision = 0;
    bool snapshot = false;
    // Null-terminated manifest strings, copied from the authenticated offer.
    std::array<char, 65> provider{};
    std::array<char, 129> native_namespace{};
    std::array<Capability, 8> capabilities{};
};

using StateAllocate = void *(*)(std::size_t);
using StateRelease = void (*)(void *);

// One network owner serializes configuration, mutation and bounded reads.
// The public views copy data; no pointer into a generation escapes.
class StateStore {
public:
    StateStore() = default;
    ~StateStore();
    StateStore(const StateStore &) = delete;
    StateStore &operator=(const StateStore &) = delete;

    // limits: Negotiated aggregate capacities for the current single-source profile.
    // allocate: Aligned allocator for owner-held working storage and both generations.
    // release: Matching deallocator. Failed configuration preserves the live view.
    Protocol::RejectCode configure(const CapacityLimits &limits, StateAllocate allocate, StateRelease release);

    // request: Exact outstanding request bound to the authenticated session.
    Protocol::RejectCode begin(const StateRequest &request);

    // bytes: Contiguous bytes borrowed from the authenticated receive window.
    // length: Byte count for this slice. Errors require cancellation by the owner.
    Protocol::RejectCode consume(const uint8_t *bytes, uint16_t length);

    // identity: Authenticated repeated transfer metadata after full SHA-256 verification.
    // request_us: Time of the original request in the receiver monotonic clock.
    // now_us: Time of admission in the same clock.
    Protocol::RejectCode commit(const Protocol::TransferIdentity &identity, uint64_t request_us,
                                uint64_t now_us);

    // No parameters: releases staging admission while retaining the committed view.
    void cancel();

    // No parameters: releases both banks on explicit association replacement.
    void clear();

    // source: Authenticated source ID for the current negotiation.
    // epoch: Bound epoch of that source. Returns zero when no matching view exists.
    uint64_t revision(SourceId source, EpochId epoch) const;

    // No parameters: copies a bounded summary of the last committed generation.
    StateSummary summary() const;

    // request: Bounded page or single entity in one committed generation.
    // now_us: Current receiver clock, used to age original evidence conservatively.
    // link_ready: True only for a confirmed lease and the queried source/epoch.
    // result: Caller-owned copied projection. No pointers into either bank escape.
    Protocol::RejectCode query(const StateQuery &request, uint64_t now_us, bool link_ready,
                               StateQueryResult &result) const;

private:
    struct Storage;
    Storage *storage_ = nullptr;
    // Never reset on association replacement within this owner lifetime.
    uint64_t generation_ = 0;
};

} // namespace CODY::Domain
