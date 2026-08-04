#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace LUNASHELL {

inline constexpr std::array<uint32_t, 7> kFullSnapshotPeriodsMs = {
    300,
    500,
    1000,
    2000,
    3000,
    5000,
    10000,
};

constexpr bool isFullSnapshotPeriodSupported(uint32_t period_ms)
{
    return std::find(
               kFullSnapshotPeriodsMs.begin(),
               kFullSnapshotPeriodsMs.end(),
               period_ms
           ) != kFullSnapshotPeriodsMs.end();
}

}  // namespace LUNASHELL
