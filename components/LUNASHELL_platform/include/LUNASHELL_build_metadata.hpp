#pragma once
#include <cstdint>

namespace LUNASHELL {
enum class FirmwareBuildFieldState : uint8_t { Available, Unavailable, Unsupported, Error };
struct FirmwareBuildMetadata {
    char local_date_time[17]{}; // dd/MM/yy HH:mm in the compiling computer's captured local zone.
    char time_zone[64]{}; // Exact operating-system zone identity, never an inferred abbreviation.
    char computer[64]{}; // Exact local computer hostname, never a network lookup.
    FirmwareBuildFieldState clock_state = FirmwareBuildFieldState::Unavailable;
    FirmwareBuildFieldState computer_state = FirmwareBuildFieldState::Unavailable;
};
static_assert(sizeof(FirmwareBuildMetadata) <= 148, "Build identity is bounded immutable image data");

// No parameters: borrows immutable metadata captured by the public firmware build entrypoint.
const FirmwareBuildMetadata &firmware_build_metadata();
} // namespace LUNASHELL
