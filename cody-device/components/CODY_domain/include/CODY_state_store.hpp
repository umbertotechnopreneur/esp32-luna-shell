
/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_domain_capacity.hpp"
#include "CODY_state_receive.hpp"

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

struct StateSummary {
    SourceId source{};
    EpochId epoch{};
    uint64_t revision = 0;
    uint32_t chats = 0;
    uint32_t turns = 0;
    uint32_t active_calls = 0;
    uint32_t terminal_calls = 0;
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

private:
    struct Storage;
    Storage *storage_ = nullptr;
};

} // namespace CODY::Domain
