/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>
#include <limits>

namespace CODY::Domain {

// Session facts only. A tap is an observed interaction, not a mood or need.
struct PetStatus {
    uint64_t tap_count = 0;
    uint64_t last_tap_at_us = 0; // Device monotonic clock; zero means no tap.
};

enum class PetUpdateResult : uint8_t { Ok, InvalidTime, OutOfOrder, CapacityExceeded };

// The owner serializes mutation and copying; this model knows no renderer or hardware.
class PetModel {
public:
    // timestamp_us: Original monotonic time of a recognized tap.
    // Exception: Rejects invalid, repeated or overflowing observations without mutation.
    PetUpdateResult record_tap(uint64_t timestamp_us)
    {
        if (timestamp_us == 0) return PetUpdateResult::InvalidTime;
        if (timestamp_us <= status_.last_tap_at_us) return PetUpdateResult::OutOfOrder;
        if (status_.tap_count == std::numeric_limits<uint64_t>::max()) return PetUpdateResult::CapacityExceeded;
        ++status_.tap_count;
        status_.last_tap_at_us = timestamp_us;
        return PetUpdateResult::Ok;
    }

    // No parameters: returns a bounded copy; reading never advances the model.
    PetStatus snapshot() const { return status_; }

private:
    PetStatus status_{};
};

} // namespace CODY::Domain
